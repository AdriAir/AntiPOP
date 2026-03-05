// OnnxDetector.h : Detector de contenido basado en ONNX Runtime.
// Usa un modelo YOLOv8 exportado a formato ONNX para detectar
// pulpos y tentaculos en imagenes capturadas.
//
// OPTIMIZACIONES v2:
// - Soporte CUDA EP para inferencia en GPU (10-30x mas rapido que CPU)
// - Buffers pre-alocados: elimina allocations por frame en el hot loop
// - Warm-up de inferencia para evitar latencia en el primer frame

#pragma once

#include "IContentDetector.h"
#include <bitset>

namespace antipop::detector {

class OnnxDetector final : public IContentDetector {
public:
    OnnxDetector();
    ~OnnxDetector() override;

    // No copiable
    OnnxDetector(const OnnxDetector&) = delete;
    OnnxDetector& operator=(const OnnxDetector&) = delete;

    [[nodiscard]] bool Initialize(const std::filesystem::path& modelPath) override;
    [[nodiscard]] std::vector<Detection> Detect(
        const uint8_t* imageData,
        uint32_t width,
        uint32_t height,
        uint32_t stride
    ) override;
    void SetConfidenceThreshold(float threshold) override;
    void Shutdown() override;

    // Configura si se debe intentar usar GPU para inferencia.
    // Llamar ANTES de Initialize(). Si GPU no esta disponible, cae a CPU.
    void SetUseGpu(bool useGpu) { m_useGpu = useGpu; }
    void SetUseFP16(bool useFP16) { m_useFP16 = useFP16; }

private:
    // Preprocesa la imagen IN-PLACE en el buffer pre-alocado m_inputTensor.
    // Redimensiona a la entrada del modelo y normaliza (CHW, RGB, [0,1]).
    void Preprocess(
        const uint8_t* imageData,
        uint32_t width,
        uint32_t height,
        uint32_t stride
    );

    // Post-procesa la salida del modelo YOLOv8 y filtra por confianza y NMS.
    [[nodiscard]] std::vector<Detection> Postprocess(
        const float* outputData,
        size_t outputSize,
        uint32_t originalWidth,
        uint32_t originalHeight
    );

    // Non-Maximum Suppression para eliminar detecciones redundantes.
    [[nodiscard]] std::vector<Detection> ApplyNMS(
        std::vector<Detection>& detections,
        float iouThreshold
    ) const;

    // Ejecuta inferencia warm-up para evitar latencia JIT en el primer frame real
    void WarmUp();

    // Dimensiones de entrada del modelo (tipicamente 640x640 para YOLOv8)
    static constexpr uint32_t kModelInputWidth  = 640;
    static constexpr uint32_t kModelInputHeight = 640;
    static constexpr size_t kInputTensorSize = 3 * kModelInputHeight * kModelInputWidth;

    // IDs de clases que corresponden a contenido a censurar.
    // Se configuran segun el modelo entrenado.
    std::vector<int> m_targetClassIds;

    // Nombres de las clases del modelo
    std::vector<std::string> m_classNames;

    float m_confidenceThreshold = 0.5f;
    float m_nmsThreshold        = 0.45f;

    // Buffer pre-alocado para el tensor de entrada (evita allocation por frame)
    std::vector<float> m_inputTensor;

    // Buffer pre-alocado para candidatos de postproceso (evita allocation por frame)
    std::vector<Detection> m_postprocCandidates;

    // Recursos de ONNX Runtime (pimpl para ocultar dependencias del header publico)
    struct OnnxResources;
    std::unique_ptr<OnnxResources> m_onnx;

    bool m_initialized = false;
    bool m_useGpu = true;   // Intentar GPU por defecto
    bool m_useFP16 = false; // FP16 desactivado por defecto
};

} // namespace antipop::detector
