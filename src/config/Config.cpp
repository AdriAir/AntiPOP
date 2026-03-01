// Config.cpp : Implementacion de la carga/guardado de configuracion.
// Usa un formato clave=valor simple para evitar dependencias de JSON externas.
// Se puede reemplazar con nlohmann/json u otro parser en el futuro.

#include "Config.h"
#include "../utils/Logger.h"

#include <sstream>

namespace antipop::config {

std::filesystem::path Config::GetAppDirectory() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

std::filesystem::path Config::GetProjectDirectory() {
    auto dir = GetAppDirectory();
    for (int i = 0; i < 5; ++i) {
        if (std::filesystem::exists(dir / "models")) {
            return dir;
        }
        auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return GetAppDirectory();
}

bool Config::Load(const std::filesystem::path& configPath) {
    m_configPath = configPath;

    if (!std::filesystem::exists(configPath)) {
        LOG_INFO("Archivo de configuracion no encontrado, creando con valores por defecto: {}",
                 configPath.string());
        return Save();
    }

    std::ifstream file(configPath);
    if (!file.is_open()) {
        LOG_ERROR("No se pudo abrir el archivo de configuracion: {}", configPath.string());
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Ignorar comentarios y lineas vacias
        if (line.empty() || line[0] == '#') continue;

        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key   = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

        // Trim espacios
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(key);
        trim(value);

        // Parsear cada clave conocida
        if (key == "model_path") {
            m_config.modelPath = value;
        } else if (key == "confidence_threshold") {
            m_config.confidenceThreshold = std::stof(value);
        } else if (key == "capture_interval_ms") {
            m_config.captureIntervalMs = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "censor_expansion") {
            m_config.censorExpansion = std::stof(value);
        } else if (key == "censor_color_r") {
            m_config.censorColorR = static_cast<uint8_t>(std::stoul(value));
        } else if (key == "censor_color_g") {
            m_config.censorColorG = static_cast<uint8_t>(std::stoul(value));
        } else if (key == "censor_color_b") {
            m_config.censorColorB = static_cast<uint8_t>(std::stoul(value));
        } else if (key == "auto_start") {
            m_config.autoStartEnabled = (value == "true" || value == "1");
        } else if (key == "show_tray_icon") {
            m_config.showTrayIcon = (value == "true" || value == "1");
        }
    }

    LOG_INFO("Configuracion cargada desde: {}", configPath.string());
    return true;
}

bool Config::Save() const {
    std::ofstream file(m_configPath);
    if (!file.is_open()) {
        LOG_ERROR("No se pudo guardar la configuracion en: {}", m_configPath.string());
        return false;
    }

    file << "# AntiPop - Configuracion\n";
    file << "# Modifica estos valores y reinicia la aplicacion para aplicar cambios.\n\n";

    file << "# Ruta al modelo ONNX (relativa al directorio del ejecutable)\n";
    file << "model_path = " << m_config.modelPath.string() << "\n\n";

    file << "# Umbral de confianza para detecciones (0.0 - 1.0)\n";
    file << "confidence_threshold = " << m_config.confidenceThreshold << "\n\n";

    file << "# Intervalo entre capturas en milisegundos (menor = mas fluido, mas CPU)\n";
    file << "capture_interval_ms = " << m_config.captureIntervalMs << "\n\n";

    file << "# Margen extra alrededor de las detecciones (0.15 = 15%)\n";
    file << "censor_expansion = " << m_config.censorExpansion << "\n\n";

    file << "# Color de censura (RGB, 0-255)\n";
    file << "censor_color_r = " << static_cast<int>(m_config.censorColorR) << "\n";
    file << "censor_color_g = " << static_cast<int>(m_config.censorColorG) << "\n";
    file << "censor_color_b = " << static_cast<int>(m_config.censorColorB) << "\n\n";

    file << "# Iniciar con Windows (true/false)\n";
    file << "auto_start = " << (m_config.autoStartEnabled ? "true" : "false") << "\n\n";

    file << "# Mostrar icono en la bandeja del sistema (true/false)\n";
    file << "show_tray_icon = " << (m_config.showTrayIcon ? "true" : "false") << "\n";

    LOG_INFO("Configuracion guardada en: {}", m_configPath.string());
    return true;
}

} // namespace antipop::config
