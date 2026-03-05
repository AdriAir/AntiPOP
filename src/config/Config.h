// Config.h : Configuracion de la aplicacion.
// Lee y escribe un archivo key=value con los parametros ajustables.
// El archivo se almacena junto al ejecutable.
//
// v2: Nuevos parametros de rendimiento para pipeline paralelo y GPU inference

#pragma once

#include "../../framework.h"

namespace antipop::config {

struct AppConfig {
    // Ruta al modelo ONNX
    std::filesystem::path modelPath = L"models/octopus_detector.onnx";

    // Umbral minimo de confianza para considerar una deteccion
    float confidenceThreshold = 0.5f;

    // Intervalo entre capturas de pantalla (milisegundos).
    // En modo pipeline paralelo, esto controla el rate del thread de captura.
    // Valor 0 = tan rapido como DXGI permita (~120 Hz maximo)
    uint32_t captureIntervalMs = 0;  // v2: 0 por defecto (max speed)

    // Margen extra alrededor de las detecciones (porcentaje, 0.3 = 30%)
    float censorExpansion = 0.30f;

    // Tipo de censura: 0=solido (negro), 1=pixelado (mosaico)
    int censorType = 1;

    // Tamano del bloque de pixelado en pixels (solo si censorType=1)
    int pixelateBlockSize = 12;

    // Color de la censura (formato RGB) - solo para censorType=0
    uint8_t censorColorR = 0;
    uint8_t censorColorG = 0;
    uint8_t censorColorB = 0;

    // Iniciar con Windows automaticamente
    bool autoStartEnabled = true;

    // Mostrar icono en la bandeja del sistema
    bool showTrayIcon = true;

    // ---- Nuevos parametros de rendimiento (v2) ----

    // Usar GPU para inferencia (CUDA EP). Si false o no disponible, usa CPU.
    bool useGpuInference = true;

    // Usar precision FP16 en GPU (mas rapido, misma precision practica).
    // Solo tiene efecto si useGpuInference=true y el modelo soporta FP16.
    bool useFP16 = false;

    // FPS objetivo del overlay (tipicamente 60)
    int overlayTargetFps = 60;

    // Mostrar metricas de rendimiento en el log cada N frames de inferencia
    int metricsLogInterval = 60;
};

class Config {
public:
    [[nodiscard]] bool Load(const std::filesystem::path& configPath);
    [[nodiscard]] bool Save() const;

    [[nodiscard]] const AppConfig& Get() const noexcept { return m_config; }
    [[nodiscard]] AppConfig& GetMutable() noexcept { return m_config; }

    [[nodiscard]] static std::filesystem::path GetAppDirectory();
    [[nodiscard]] static std::filesystem::path GetProjectDirectory();

private:
    AppConfig            m_config;
    std::filesystem::path m_configPath;
};

} // namespace antipop::config
