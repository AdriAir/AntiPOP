// IContentDetector.h : Interfaz abstracta para deteccion de contenido.
// Permite cambiar el motor de IA (ONNX, TensorRT, etc.)
// sin afectar al resto de la aplicacion.

#pragma once

#include "Detection.h"

namespace antipop::detector {

class IContentDetector {
public:
    virtual ~IContentDetector() = default;

    // Inicializa el detector cargando el modelo de IA.
    // modelPath: ruta al archivo del modelo (e.g., .onnx).
    [[nodiscard]] virtual bool Initialize(const std::filesystem::path& modelPath) = 0;

    // Detecta objetos en una imagen BGRA.
    // Devuelve una lista de detecciones que superan el umbral de confianza.
    [[nodiscard]] virtual std::vector<Detection> Detect(
        const uint8_t* imageData,
        uint32_t width,
        uint32_t height,
        uint32_t stride
    ) = 0;

    // Ajusta el umbral minimo de confianza para considerar una deteccion valida.
    virtual void SetConfidenceThreshold(float threshold) = 0;

    // Libera los recursos del detector.
    virtual void Shutdown() = 0;
};

} // namespace antipop::detector
