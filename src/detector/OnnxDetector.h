// OnnxDetector.h : Detector de contenido basado en ONNX Runtime.
// Usa un modelo YOLOv8 exportado a formato ONNX para detectar
// pulpos y tentaculos en imagenes capturadas.

#pragma once

#include "IContentDetector.h"

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

private:
    // Preprocesa la imagen: redimensiona a la entrada del modelo y normaliza.
    // Devuelve el tensor de entrada como vector de floats (CHW, RGB, [0,1]).
    [[nodiscard]] std::vector<float> Preprocess(
        const uint8_t* imageData,
        uint32_t width,
        uint32_t height,
        uint32_t stride
    ) const;

    // Post-procesa la salida del modelo YOLOv8 y filtra por confianza y NMS.
    [[nodiscard]] std::vector<Detection> Postprocess(
        const float* outputData,
        size_t outputSize,
        uint32_t originalWidth,
        uint32_t originalHeight
    ) const;

    // Non-Maximum Suppression para eliminar detecciones redundantes.
    [[nodiscard]] std::vector<Detection> ApplyNMS(
        std::vector<Detection>& detections,
        float iouThreshold
    ) const;

    // Dimensiones de entrada del modelo (tipicamente 640x640 para YOLOv8)
    static constexpr uint32_t kModelInputWidth  = 640;
    static constexpr uint32_t kModelInputHeight = 640;

    // IDs de clases que corresponden a contenido a censurar.
    // Se configuran segun el modelo entrenado.
    std::vector<int> m_targetClassIds;

    // Nombres de las clases del modelo
    std::vector<std::string> m_classNames;

    float m_confidenceThreshold = 0.5f;
    float m_nmsThreshold        = 0.45f;

    // Recursos de ONNX Runtime (pimpl para ocultar dependencias del header publico)
    struct OnnxResources;
    std::unique_ptr<OnnxResources> m_onnx;

    bool m_initialized = false;
};

} // namespace antipop::detector
