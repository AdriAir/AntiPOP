// Logger.h : Sistema de logging minimalista para AntiPop.
// Escribe al archivo de log y opcionalmente a OutputDebugString.
// Thread-safe mediante mutex.

#pragma once

#include "../../framework.h"

namespace antipop::utils {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

class Logger {
public:
    // Singleton: una unica instancia de logger para toda la aplicacion.
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    // Inicializa el logger abriendo el archivo de log.
    void Initialize(const std::filesystem::path& logPath, LogLevel minLevel = LogLevel::Info) {
        std::lock_guard lock(m_mutex);
        m_logFile.open(logPath, std::ios::app);
        m_minLevel = minLevel;
        m_initialized = true;
    }

    // Escribe un mensaje de log.
    template<typename... Args>
    void Log(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
        if (level < m_minLevel) return;

        std::string message;
        try {
            message = std::format(fmt, std::forward<Args>(args)...);
        } catch (...) {
            message = "(error formateando mensaje de log)";
        }

        std::string fullMessage = std::format("[{}] [{}] {}\n",
            GetTimestamp(), LevelToString(level), message);

        std::lock_guard lock(m_mutex);

        // Escribir al archivo de log
        if (m_logFile.is_open()) {
            m_logFile << fullMessage;
            m_logFile.flush();
        }

        // En modo debug, tambien escribir a OutputDebugString
#ifdef _DEBUG
        OutputDebugStringA(fullMessage.c_str());
#endif
    }

    void Shutdown() {
        std::lock_guard lock(m_mutex);
        if (m_logFile.is_open()) {
            m_logFile.close();
        }
        m_initialized = false;
    }

private:
    Logger() = default;
    ~Logger() { Shutdown(); }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    [[nodiscard]] static const char* LevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
            default:              return "?????";
        }
    }

    [[nodiscard]] static std::string GetTimestamp() {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm = {};
        localtime_s(&tm, &time);

        return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec, ms.count());
    }

    std::ofstream m_logFile;
    std::mutex    m_mutex;
    LogLevel      m_minLevel    = LogLevel::Info;
    bool          m_initialized = false;
};

} // namespace antipop::utils

// Macros de conveniencia para logging.
// Uso: LOG_INFO("Mensaje con {} parametros: {}", 2, "ejemplo");
#define LOG_DEBUG(...) ::antipop::utils::Logger::Instance().Log(::antipop::utils::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  ::antipop::utils::Logger::Instance().Log(::antipop::utils::LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::antipop::utils::Logger::Instance().Log(::antipop::utils::LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::antipop::utils::Logger::Instance().Log(::antipop::utils::LogLevel::Error, __VA_ARGS__)
