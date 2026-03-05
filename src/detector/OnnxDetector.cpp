// OnnxDetector.cpp : Implementacion del detector de pulpos basado en ONNX Runtime + YOLOv8.
//
// DEPENDENCIA: ONNX Runtime 1.24.2
//   - CPU:  NuGet Microsoft.ML.OnnxRuntime
//   - GPU:  NuGet Microsoft.ML.OnnxRuntime.Gpu (incluye CUDA EP)
//
// MODELO: Se espera un modelo YOLOv8 exportado a ONNX con:
//   - Input:  [1, 3, 640, 640] float32 (RGB normalizado [0,1])
//   - Output: [1, 84, 8400] formato YOLOv8 transpose (4 bbox + 80 classes, o custom)
//
// OPTIMIZACIONES v2:
//   - CUDA Execution Provider para inferencia en GPU (fallback a CPU si no disponible)
//   - Buffer pre-alocado para tensor de entrada (elimina ~5MB alloc/dealloc por frame)
//   - Buffer pre-alocado para candidatos de postproceso
//   - Warm-up de inferencia para evitar latencia JIT en el primer frame
//   - NMS con bitset en stack (cache-friendly)
//
// Para exportar un modelo custom entrenado con pulpos:
//   from ultralytics import YOLO
//   model = YOLO("octopus_detector.pt")
//   model.export(format="onnx", imgsz=640)

#include "OnnxDetector.h"
#include "../utils/Logger.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <numeric>
#include <cmath>

namespace antipop::detector {

// Estructura interna que encapsula los recursos de ONNX Runtime.
// Usa el patron pimpl para no exponer los headers de ONNX en la interfaz publica.
struct OnnxDetector::OnnxResources {
    Ort::Env env{ ORT_LOGGING_LEVEL_WARNING, "AntiPop" };
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator, OrtMemTypeDefault);

    // Nombres de entrada/salida del modelo
    std::vector<const char*> inputNames;
    std::vector<const char*> outputNames;
    std::vector<std::string> inputNamesOwned;
    std::vector<std::string> outputNamesOwned;

    // Indica si CUDA EP esta activo
    bool usingCudaEP = false;
};

OnnxDetector::OnnxDetector()
    : m_onnx(std::make_unique<OnnxResources>()) {
}

OnnxDetector::~OnnxDetector() {
    Shutdown();
}

bool OnnxDetector::Initialize(const std::filesystem::path& modelPath) {
    if (m_initialized) return true;

    if (!std::filesystem::exists(modelPath)) {
        LOG_ERROR("Modelo ONNX no encontrado: {}", modelPath.string());
        return false;
    }

    LOG_INFO("Cargando modelo ONNX: {}", modelPath.string());

    try {
        Ort::SessionOptions sessionOptions;

        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        sessionOptions.EnableCpuMemArena();
        sessionOptions.EnableMemPattern();

        bool cudaRequested = false;
        bool cudaAvailable = false;

        // Intentar habilitar CUDA si está activado en config
        if (m_useGpu) {
            try {
                OrtCUDAProviderOptions cudaOptions{};
                cudaOptions.device_id = 0;
                cudaOptions.arena_extend_strategy = 0;
                cudaOptions.gpu_mem_limit = SIZE_MAX;
                cudaOptions.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchDefault;
                cudaOptions.do_copy_in_default_stream = 1;

                sessionOptions.AppendExecutionProvider_CUDA(cudaOptions);
                cudaRequested = true;
                cudaAvailable = true;

                LOG_INFO("CUDA Execution Provider solicitado");
            }
            catch (const Ort::Exception& e) {
                LOG_WARN("No se pudo configurar CUDA EP: {}", e.what());
            }
        }
        else {
            LOG_INFO("GPU inference desactivada por configuracion (use_gpu_inference=false)");
        }

        // Configuración CPU fallback
        if (!cudaRequested) {
            sessionOptions.SetIntraOpNumThreads(4);
            sessionOptions.SetInterOpNumThreads(2);
        }

        // Crear sesión ONNX
        m_onnx->session = std::make_unique<Ort::Session>(
            m_onnx->env,
            modelPath.wstring().c_str(),
            sessionOptions);

        // CUDA está activo si lo solicitamos y la sesión se creó sin lanzar
        m_onnx->usingCudaEP = cudaAvailable;

        if (cudaRequested && !cudaAvailable) {
            LOG_WARN("CUDA solicitado pero no activo. Se está usando CPU.");
        }

        // Obtener nombres de entrada y salida
        Ort::AllocatorWithDefaultOptions allocator;

        for (size_t i = 0; i < m_onnx->session->GetInputCount(); ++i) {
            auto name = m_onnx->session->GetInputNameAllocated(i, allocator);
            m_onnx->inputNamesOwned.push_back(name.get());
        }

        for (const auto& name : m_onnx->inputNamesOwned) {
            m_onnx->inputNames.push_back(name.c_str());
        }

        for (size_t i = 0; i < m_onnx->session->GetOutputCount(); ++i) {
            auto name = m_onnx->session->GetOutputNameAllocated(i, allocator);
            m_onnx->outputNamesOwned.push_back(name.get());
        }

        for (const auto& name : m_onnx->outputNamesOwned) {
            m_onnx->outputNames.push_back(name.c_str());
        }

        LOG_INFO("Modelo cargado - Inputs: {}, Outputs: {}, EP: {}",
            m_onnx->inputNames.size(),
            m_onnx->outputNames.size(),
            cudaAvailable ? "CUDA (GPU)" : "CPU");

        // Log dimensiones entrada
        auto inputInfo = m_onnx->session->GetInputTypeInfo(0);
        auto tensorInfo = inputInfo.GetTensorTypeAndShapeInfo();
        auto inputShape = tensorInfo.GetShape();

        std::string shapeStr;

        for (auto dim : inputShape) {
            if (!shapeStr.empty()) shapeStr += "x";
            shapeStr += std::to_string(dim);
        }

        LOG_INFO("Forma de entrada del modelo: [{}]", shapeStr);

    }
    catch (const Ort::Exception& e) {
        LOG_ERROR("Error inicializando ONNX Runtime: {}", e.what());
        return false;
    }

    // Clases objetivo
    m_targetClassIds = { 0 };
    m_classNames = { "octopus" };

    // Buffers reutilizables
    m_inputTensor.resize(kInputTensorSize);
    m_postprocCandidates.reserve(64);

    m_initialized = true;

    LOG_INFO("Modelo ONNX cargado exitosamente");

    // Warm-up para inicializar kernels CUDA/cuDNN
    WarmUp();

    return true;
}

void OnnxDetector::WarmUp() {
    if (!m_initialized || !m_onnx->session) return;

    LOG_INFO("Ejecutando warm-up de inferencia...");

    try {
        // Llenar tensor con zeros y ejecutar una inferencia dummy
        std::fill(m_inputTensor.begin(), m_inputTensor.end(), 0.0f);

        std::array<int64_t, 4> inputShape = {
            1, 3,
            static_cast<int64_t>(kModelInputHeight),
            static_cast<int64_t>(kModelInputWidth)
        };

        auto ortInput = Ort::Value::CreateTensor<float>(
            m_onnx->memoryInfo,
            m_inputTensor.data(), m_inputTensor.size(),
            inputShape.data(), inputShape.size()
        );

        m_onnx->session->Run(
            Ort::RunOptions{ nullptr },
            m_onnx->inputNames.data(), &ortInput, 1,
            m_onnx->outputNames.data(), m_onnx->outputNames.size()
        );

        LOG_INFO("Warm-up completado (EP: {})",
                 m_onnx->usingCudaEP ? "CUDA" : "CPU");

    } catch (const Ort::Exception& e) {
        LOG_WARN("Warm-up fallo (no critico): {}", e.what());
    }
}

std::vector<Detection> OnnxDetector::Detect(
    const uint8_t* imageData,
    uint32_t width,
    uint32_t height,
    uint32_t stride
) {
    if (!m_initialized || !m_onnx->session) return {};

    // Paso 1: Preprocesar la imagen en el buffer pre-alocado (sin allocation)
    Preprocess(imageData, width, height, stride);

    try {
        // Paso 2: Crear tensor ONNX apuntando al buffer pre-alocado y ejecutar inferencia
        std::array<int64_t, 4> inputShape = {
            1, 3,
            static_cast<int64_t>(kModelInputHeight),
            static_cast<int64_t>(kModelInputWidth)
        };

        auto ortInput = Ort::Value::CreateTensor<float>(
            m_onnx->memoryInfo,
            m_inputTensor.data(), m_inputTensor.size(),
            inputShape.data(), inputShape.size()
        );

        auto outputTensors = m_onnx->session->Run(
            Ort::RunOptions{ nullptr },
            m_onnx->inputNames.data(), &ortInput, 1,
            m_onnx->outputNames.data(), m_onnx->outputNames.size()
        );

        // Paso 3: Post-procesar los resultados
        const float* outputData = outputTensors[0].GetTensorData<float>();
        auto outputInfo = outputTensors[0].GetTensorTypeAndShapeInfo();
        auto outputShape = outputInfo.GetShape();

        // Log de diagnostico del output shape (solo la primera vez)
        static bool shapeLogged = false;
        if (!shapeLogged) {
            std::string shapeStr;
            for (auto dim : outputShape) {
                if (!shapeStr.empty()) shapeStr += "x";
                shapeStr += std::to_string(dim);
            }
            LOG_INFO("Output del modelo: [{}] (esperado: [1, {}, 8400])",
                     shapeStr, m_classNames.size() + 4);
            shapeLogged = true;
        }

        // YOLOv8 output: [1, num_classes+4, num_detections]
        size_t numDetections = outputShape.back();

        auto detections = Postprocess(outputData, numDetections, width, height);
        auto result = ApplyNMS(detections, m_nmsThreshold);

        if (!result.empty()) {
            LOG_DEBUG("Detecciones: {} pre-NMS, {} post-NMS (mejor: {:.2f}% {})",
                      detections.size(), result.size(),
                      result[0].confidence * 100.0f, result[0].className);
        }

        return result;

    } catch (const Ort::Exception& e) {
        LOG_ERROR("Error durante inferencia ONNX: {}", e.what());
        return {};
    }
}

void OnnxDetector::SetConfidenceThreshold(float threshold) {
    m_confidenceThreshold = std::clamp(threshold, 0.0f, 1.0f);
}

void OnnxDetector::Shutdown() {
    m_onnx->inputNames.clear();
    m_onnx->outputNames.clear();
    m_onnx->inputNamesOwned.clear();
    m_onnx->outputNamesOwned.clear();
    m_onnx->session.reset();

    // Liberar buffers pre-alocados
    m_inputTensor.clear();
    m_inputTensor.shrink_to_fit();
    m_postprocCandidates.clear();
    m_postprocCandidates.shrink_to_fit();

    m_initialized = false;
    LOG_INFO("ONNX Detector liberado");
}

void OnnxDetector::Preprocess(
    const uint8_t* imageData,
    uint32_t width,
    uint32_t height,
    uint32_t stride
) {
    // Escribe directamente en m_inputTensor (pre-alocado, sin allocation)
    // Formato: CHW, RGB, [0,1], 640x640

    const float scaleX = static_cast<float>(width)  / kModelInputWidth;
    const float scaleY = static_cast<float>(height) / kModelInputHeight;

    const size_t planeSize = kModelInputHeight * kModelInputWidth;

    for (uint32_t y = 0; y < kModelInputHeight; ++y) {
        const uint32_t srcY = std::min(static_cast<uint32_t>(y * scaleY), height - 1);
        const uint8_t* srcRow = imageData + srcY * stride;

        for (uint32_t x = 0; x < kModelInputWidth; ++x) {
            const uint32_t srcX = std::min(static_cast<uint32_t>(x * scaleX), width - 1);
            const uint8_t* pixel = srcRow + srcX * 4;

            const size_t pixelIdx = y * kModelInputWidth + x;

            // BGRA -> RGB, normalizar a [0,1]
            m_inputTensor[0 * planeSize + pixelIdx] = pixel[2] * (1.0f / 255.0f);  // R
            m_inputTensor[1 * planeSize + pixelIdx] = pixel[1] * (1.0f / 255.0f);  // G
            m_inputTensor[2 * planeSize + pixelIdx] = pixel[0] * (1.0f / 255.0f);  // B
        }
    }
}

std::vector<Detection> OnnxDetector::Postprocess(
    const float* outputData,
    size_t numDetections,
    uint32_t originalWidth,
    uint32_t originalHeight
) {
    // Reutilizar buffer pre-alocado (sin allocation si capacity es suficiente)
    m_postprocCandidates.clear();

    const size_t numClasses = m_classNames.size();
    const float scaleX = static_cast<float>(originalWidth)  / kModelInputWidth;
    const float scaleY = static_cast<float>(originalHeight) / kModelInputHeight;

    for (size_t i = 0; i < numDetections; ++i) {
        // Encontrar la clase con mayor confianza (early exit)
        float maxScore = 0.0f;
        int   maxClassId = -1;

        for (size_t c = 0; c < numClasses; ++c) {
            const float score = outputData[(4 + c) * numDetections + i];
            if (score > maxScore) {
                maxScore = score;
                maxClassId = static_cast<int>(c);
            }
        }

        // Early exit: 99%+ de predicciones se descartan aqui
        if (maxScore < m_confidenceThreshold) continue;

        bool isTarget = std::find(
            m_targetClassIds.begin(), m_targetClassIds.end(), maxClassId
        ) != m_targetClassIds.end();

        if (!isTarget) continue;

        // Solo crear Detection para candidatos reales
        const float cx = outputData[0 * numDetections + i] * scaleX;
        const float cy = outputData[1 * numDetections + i] * scaleY;
        const float w  = outputData[2 * numDetections + i] * scaleX;
        const float h  = outputData[3 * numDetections + i] * scaleY;

        Detection det;
        det.box.x      = cx - w * 0.5f;
        det.box.y      = cy - h * 0.5f;
        det.box.width  = w;
        det.box.height = h;
        det.confidence  = maxScore;
        det.classId     = maxClassId;
        det.className   = (maxClassId >= 0 && maxClassId < static_cast<int>(m_classNames.size()))
                          ? m_classNames[maxClassId] : "unknown";

        m_postprocCandidates.push_back(std::move(det));
    }

    return m_postprocCandidates;
}

std::vector<Detection> OnnxDetector::ApplyNMS(
    std::vector<Detection>& detections,
    float iouThreshold
) const {
    if (detections.empty()) return {};

    // Ordenar por confianza descendente
    std::sort(detections.begin(), detections.end(),
        [](const Detection& a, const Detection& b) {
            return a.confidence > b.confidence;
        });

    // Usar bitset en stack (cache-friendly, sin allocation)
    std::bitset<256> suppressed;
    std::vector<Detection> result;
    result.reserve(detections.size());

    const size_t count = std::min(detections.size(), size_t{256});

    for (size_t i = 0; i < count; ++i) {
        if (suppressed[i]) continue;
        result.push_back(detections[i]);

        for (size_t j = i + 1; j < count; ++j) {
            if (suppressed[j]) continue;

            // Calcular IoU (Intersection over Union)
            const auto& a = detections[i].box;
            const auto& b = detections[j].box;

            const float x1 = std::max(a.x, b.x);
            const float y1 = std::max(a.y, b.y);
            const float x2 = std::min(a.x + a.width,  b.x + b.width);
            const float y2 = std::min(a.y + a.height, b.y + b.height);

            const float intersection = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
            const float unionArea = a.width * a.height + b.width * b.height - intersection;

            if (unionArea > 0.0f && (intersection / unionArea) > iouThreshold) {
                suppressed.set(j);
            }
        }
    }

    return result;
}

} // namespace antipop::detector
