// App.h : Orquestador principal de la aplicacion AntiPop.
// Coordina los modulos de captura, deteccion y overlay en un pipeline
// que se ejecuta en un hilo dedicado, mientras el hilo principal
// maneja el message loop de Windows y la bandeja del sistema.

#pragma once

#include "../framework.h"
#include "capture/IScreenCapture.h"
#include "detector/IContentDetector.h"
#include "detector/DetectionTracker.h"
#include "overlay/IOverlay.h"
#include "config/Config.h"

namespace antipop {

class App {
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Inicializa todos los subsistemas.
    // hInstance: handle de la aplicacion Win32.
    // silentMode: si es true, no muestra ventanas al inicio (modo auto-start).
    [[nodiscard]] bool Initialize(HINSTANCE hInstance, bool silentMode);

    // Inicia el pipeline de captura+deteccion+censura en un hilo separado.
    void Start();

    // Detiene el pipeline y libera recursos.
    void Stop();

    // Devuelve true si el pipeline esta activo.
    [[nodiscard]] bool IsRunning() const noexcept { return m_running.load(); }

    // Acceso a la configuracion
    [[nodiscard]] const config::AppConfig& GetConfig() const noexcept { return m_config.Get(); }

    // Configura el icono de la bandeja del sistema.
    bool SetupTrayIcon(HWND hwnd);
    void RemoveTrayIcon();

    // Maneja los mensajes del icono de la bandeja.
    void HandleTrayMessage(HWND hwnd, WPARAM wParam, LPARAM lParam);

private:
    // Bucle principal del pipeline ejecutado en hilo separado.
    void PipelineLoop();

    // Subsistemas
    std::unique_ptr<capture::IScreenCapture>   m_capture;
    std::unique_ptr<detector::IContentDetector> m_detector;
    std::unique_ptr<overlay::IOverlay>          m_overlay;

    // Configuracion
    config::Config m_config;

    // Control del hilo del pipeline
    std::thread   m_pipelineThread;
    std::atomic<bool> m_running{ false };

    // Rastreador de detecciones para suavizacion entre frames
    // Permite interpolacion suave y elimina salteos cuando los objetos se mueven
    detector::DetectionTracker m_tracker;

    // Temporal smoothing para evitar parpadeos de censura
    // Mantiene las detecciones previas durante N frames si no hay nuevas detecciones
    int m_framesWithoutDetection = 0;
    static constexpr int kFramesToClearCensor = 12;  // Buffer temporal: 12 frames (~1200ms)

    // Bandeja del sistema
    NOTIFYICONDATAW m_trayIconData = {};
    bool            m_trayIconActive = false;

    HINSTANCE m_hInstance = nullptr;

    // Mensaje personalizado para la bandeja del sistema
    static constexpr UINT WM_TRAYICON = WM_USER + 1;

    // IDs del menu contextual de la bandeja
    static constexpr UINT ID_TRAY_EXIT    = 1001;
    static constexpr UINT ID_TRAY_TOGGLE  = 1002;
    static constexpr UINT ID_TRAY_ABOUT   = 1003;
};

} // namespace antipop
