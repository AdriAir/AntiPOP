// Config.cpp : Implementacion de la carga/guardado de configuracion.
// Usa un formato clave=valor simple para evitar dependencias de JSON externas.

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
        if (line.empty() || line[0] == '#') continue;

        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key   = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

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
        } else if (key == "censor_type") {
            m_config.censorType = std::stoi(value);
        } else if (key == "pixelate_block_size") {
            m_config.pixelateBlockSize = std::stoi(value);
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
        // Nuevos parametros v2
        else if (key == "use_gpu_inference") {
            m_config.useGpuInference = (value == "true" || value == "1");
        } else if (key == "use_fp16") {
            m_config.useFP16 = (value == "true" || value == "1");
        } else if (key == "overlay_target_fps") {
            m_config.overlayTargetFps = std::stoi(value);
        } else if (key == "metrics_log_interval") {
            m_config.metricsLogInterval = std::stoi(value);
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

    file << "# Ruta al modelo ONNX (relativa al directorio del proyecto)\n";
    file << "model_path = " << m_config.modelPath.string() << "\n\n";

    file << "# Umbral de confianza para detecciones (0.0 - 1.0)\n";
    file << "confidence_threshold = " << m_config.confidenceThreshold << "\n\n";

    file << "# Intervalo entre capturas en milisegundos (0 = max speed)\n";
    file << "capture_interval_ms = " << m_config.captureIntervalMs << "\n\n";

    file << "# Margen extra alrededor de las detecciones (0.30 = 30%)\n";
    file << "censor_expansion = " << m_config.censorExpansion << "\n\n";

    file << "# Tipo de censura: 0=solido, 1=pixelado\n";
    file << "censor_type = " << m_config.censorType << "\n\n";

    file << "# Tamano del bloque de pixelado en pixels (solo si censor_type=1)\n";
    file << "pixelate_block_size = " << m_config.pixelateBlockSize << "\n\n";

    file << "# Color de censura (RGB, 0-255) - solo para censor_type=0\n";
    file << "censor_color_r = " << static_cast<int>(m_config.censorColorR) << "\n";
    file << "censor_color_g = " << static_cast<int>(m_config.censorColorG) << "\n";
    file << "censor_color_b = " << static_cast<int>(m_config.censorColorB) << "\n\n";

    file << "# Iniciar con Windows (true/false)\n";
    file << "auto_start = " << (m_config.autoStartEnabled ? "true" : "false") << "\n\n";

    file << "# Mostrar icono en la bandeja del sistema (true/false)\n";
    file << "show_tray_icon = " << (m_config.showTrayIcon ? "true" : "false") << "\n\n";

    file << "# === Rendimiento (v2) ===\n\n";

    file << "# Usar GPU para inferencia (true/false). Requiere NVIDIA GPU + CUDA.\n";
    file << "# Si no esta disponible, cae a CPU automaticamente.\n";
    file << "use_gpu_inference = " << (m_config.useGpuInference ? "true" : "false") << "\n\n";

    file << "# Usar precision FP16 en GPU (mas rapido, misma precision practica)\n";
    file << "use_fp16 = " << (m_config.useFP16 ? "true" : "false") << "\n\n";

    file << "# FPS objetivo del overlay (60 recomendado)\n";
    file << "overlay_target_fps = " << m_config.overlayTargetFps << "\n\n";

    file << "# Mostrar metricas de rendimiento en el log cada N frames\n";
    file << "metrics_log_interval = " << m_config.metricsLogInterval << "\n";

    LOG_INFO("Configuracion guardada en: {}", m_configPath.string());
    return true;
}

} // namespace antipop::config
