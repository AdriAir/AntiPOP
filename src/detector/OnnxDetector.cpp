// OnnxDetector.cpp : Implementacion del detector de pulpos basado en ONNX Runtime + YOLOv8.
//
// DEPENDENCIA: ONNX Runtime 1.24.2 (instalado via NuGet: Microsoft.ML.OnnxRuntime)
//
// MODELO: Se espera un modelo YOLOv8 exportado a ONNX con:
//   - Input:  [1, 3, 640, 640] float32 (RGB normalizado [0,1])
//   - Output: [1, 84, 8400] formato YOLOv8 transpose (4 bbox + 80 classes, o custom)
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
        sessionOptions.SetIntraOpNumThreads(2);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Opcional: usar DirectML para aceleracion GPU en Windows.
        // Descomentar la siguiente linea si se instala Microsoft.ML.OnnxRuntime.DirectML:
        // OrtSessionOptionsAppendExecutionProvider_DML(sessionOptions, 0);

        m_onnx->session = std::make_unique<Ort::Session>(
            m_onnx->env, modelPath.wstring().c_str(), sessionOptions);

        // Obtener nombres de entrada y salida del modelo
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

        LOG_INFO("Modelo cargado - Inputs: {}, Outputs: {}",
                 m_onnx->inputNames.size(), m_onnx->outputNames.size());

        // Log de las dimensiones de entrada para verificacion
        auto inputInfo = m_onnx->session->GetInputTypeInfo(0);
        auto tensorInfo = inputInfo.GetTensorTypeAndShapeInfo();
        auto inputShape = tensorInfo.GetShape();
        std::string shapeStr;
        for (auto dim : inputShape) {
            if (!shapeStr.empty()) shapeStr += "x";
            shapeStr += std::to_string(dim);
        }
        LOG_INFO("Forma de entrada del modelo: [{}]", shapeStr);

    } catch (const Ort::Exception& e) {
        LOG_ERROR("Error inicializando ONNX Runtime: {}", e.what());
        return false;
    }

    // Configurar las clases objetivo.
    // Para un modelo custom entrenado solo con pulpos, classId 0 = "octopus".
    // Ajustar estos valores segun las clases de tu modelo entrenado.
    m_targetClassIds = { 0 };
    m_classNames = { "octopus" };

    m_initialized = true;
    LOG_INFO("Modelo ONNX cargado exitosamente");
    return true;
}

std::vector<Detection> OnnxDetector::Detect(
    const uint8_t* imageData,
    uint32_t width,
    uint32_t height,
    uint32_t stride
) {
    if (!m_initialized || !m_onnx->session) return {};

    // Paso 1: Preprocesar la imagen a tensor de entrada
    auto inputTensor = Preprocess(imageData, width, height, stride);

    try {
        // Paso 2: Crear tensor ONNX y ejecutar inferencia
        std::array<int64_t, 4> inputShape = {
            1, 3,
            static_cast<int64_t>(kModelInputHeight),
            static_cast<int64_t>(kModelInputWidth)
        };

        auto ortInput = Ort::Value::CreateTensor<float>(
            m_onnx->memoryInfo,
            inputTensor.data(), inputTensor.size(),
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

        // YOLOv8 output: [1, num_classes+4, num_detections]
        // outputShape[2] = numero de detecciones candidatas (tipicamente 8400)
        size_t numDetections = outputShape.back();

        auto detections = Postprocess(outputData, numDetections, width, height);
        return ApplyNMS(detections, m_nmsThreshold);

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
    m_initialized = false;
    LOG_INFO("ONNX Detector liberado");
}

std::vector<float> OnnxDetector::Preprocess(
    const uint8_t* imageData,
    uint32_t width,
    uint32_t height,
    uint32_t stride
) const {
    // Crear tensor de salida: [3, kModelInputHeight, kModelInputWidth]
    const size_t tensorSize = 3 * kModelInputHeight * kModelInputWidth;
    std::vector<float> tensor(tensorSize);

    // Escala para redimensionar la imagen al tamano del modelo (letterboxing simple)
    const float scaleX = static_cast<float>(width)  / kModelInputWidth;
    const float scaleY = static_cast<float>(height) / kModelInputHeight;

    // Llenar el tensor: convertir BGRA -> RGB, redimensionar con nearest neighbor,
    // normalizar a [0, 1] y reorganizar a formato CHW.
    for (uint32_t y = 0; y < kModelInputHeight; ++y) {
        for (uint32_t x = 0; x < kModelInputWidth; ++x) {
            // Coordenadas en la imagen original
            const uint32_t srcX = std::min(static_cast<uint32_t>(x * scaleX), width - 1);
            const uint32_t srcY = std::min(static_cast<uint32_t>(y * scaleY), height - 1);
            const uint32_t srcIdx = srcY * stride + srcX * 4;

            const float b = imageData[srcIdx + 0] / 255.0f;
            const float g = imageData[srcIdx + 1] / 255.0f;
            const float r = imageData[srcIdx + 2] / 255.0f;

            // Formato CHW: canal R, luego G, luego B
            const size_t pixelIdx = y * kModelInputWidth + x;
            tensor[0 * kModelInputHeight * kModelInputWidth + pixelIdx] = r;
            tensor[1 * kModelInputHeight * kModelInputWidth + pixelIdx] = g;
            tensor[2 * kModelInputHeight * kModelInputWidth + pixelIdx] = b;
        }
    }

    return tensor;
}

std::vector<Detection> OnnxDetector::Postprocess(
    const float* outputData,
    size_t numDetections,
    uint32_t originalWidth,
    uint32_t originalHeight
) const {
    std::vector<Detection> detections;

    // YOLOv8 output format: [1, 4+num_classes, 8400] (transposed)
    // Cada columna i: fila 0=cx, 1=cy, 2=w, 3=h, 4..=class_scores
    const size_t numClasses = m_classNames.size();
    const float scaleX = static_cast<float>(originalWidth)  / kModelInputWidth;
    const float scaleY = static_cast<float>(originalHeight) / kModelInputHeight;

    for (size_t i = 0; i < numDetections; ++i) {
        // Para cada deteccion, encontrar la clase con mayor confianza
        float maxScore = 0.0f;
        int   maxClassId = -1;

        for (size_t c = 0; c < numClasses; ++c) {
            // YOLOv8 transpose: fila (4+c), columna i
            const float score = outputData[(4 + c) * numDetections + i];
            if (score > maxScore) {
                maxScore = score;
                maxClassId = static_cast<int>(c);
            }
        }

        // Filtrar por confianza y por clases objetivo
        if (maxScore < m_confidenceThreshold) continue;

        bool isTarget = std::find(
            m_targetClassIds.begin(), m_targetClassIds.end(), maxClassId
        ) != m_targetClassIds.end();

        if (!isTarget) continue;

        // Extraer bounding box (centro x, centro y, ancho, alto)
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

        detections.push_back(std::move(det));
    }

    return detections;
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

    std::vector<bool> suppressed(detections.size(), false);
    std::vector<Detection> result;

    for (size_t i = 0; i < detections.size(); ++i) {
        if (suppressed[i]) continue;
        result.push_back(detections[i]);

        for (size_t j = i + 1; j < detections.size(); ++j) {
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
                suppressed[j] = true;
            }
        }
    }

    return result;
}

} // namespace antipop::detector
