// Config.h : Configuracion de la aplicacion.
// Lee y escribe un archivo JSON simple con los parametros ajustables.
// El archivo se almacena junto al ejecutable.

#pragma once

#include "../../framework.h"

namespace antipop::config {

struct AppConfig {
    // Ruta al modelo ONNX
    std::filesystem::path modelPath = L"models/octopus_detector.onnx";

    // Umbral minimo de confianza para considerar una deteccion
    float confidenceThreshold = 0.5f;

    // Intervalo entre capturas de pantalla (milisegundos).
    // Valores mas bajos = mas fluido pero mas uso de CPU/GPU.
    uint32_t captureIntervalMs = 100;  // ~10 FPS

    // Margen extra alrededor de las detecciones (porcentaje, 0.3 = 30%)
    float censorExpansion = 0.30f;

    // Tipo de censura: 0=solido (negro), 1=pixelado (mosaico)
    int censorType = 1;

    // Tamano del bloque de pixelado en pixels (solo si censorType=1)
    // Valores recomendados: 8, 12, 16 (mas alto = menos detalle visible)
    int pixelateBlockSize = 12;

    // Color de la censura (formato RGB) - solo para censorType=0
    uint8_t censorColorR = 0;
    uint8_t censorColorG = 0;
    uint8_t censorColorB = 0;

    // Iniciar con Windows automaticamente
    bool autoStartEnabled = true;

    // Mostrar icono en la bandeja del sistema
    bool showTrayIcon = true;
};

class Config {
public:
    // Carga la configuracion desde el archivo JSON.
    // Si el archivo no existe, crea uno con valores por defecto.
    [[nodiscard]] bool Load(const std::filesystem::path& configPath);

    // Guarda la configuracion actual al archivo.
    [[nodiscard]] bool Save() const;

    // Acceso a la configuracion actual
    [[nodiscard]] const AppConfig& Get() const noexcept { return m_config; }
    [[nodiscard]] AppConfig& GetMutable() noexcept { return m_config; }

    // Devuelve la ruta del directorio de la aplicacion (donde esta el .exe)
    [[nodiscard]] static std::filesystem::path GetAppDirectory();

    // Devuelve el directorio del proyecto (donde esta la carpeta models/).
    // Sube desde el directorio del exe hasta encontrarla. Fallback: GetAppDirectory().
    [[nodiscard]] static std::filesystem::path GetProjectDirectory();

private:
    AppConfig            m_config;
    std::filesystem::path m_configPath;
};

} // namespace antipop::config
