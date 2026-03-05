// App.h : Orquestador principal de la aplicacion AntiPop.
//
// ARQUITECTURA v2 - Pipeline paralelo de 3 threads:
//
//   Thread 1 (Captura):    DXGI Desktop Duplication, captura continua
//   Thread 2 (Inferencia): ONNX Runtime (CPU/GPU), tracking, publica detecciones
//   Thread 3 (Overlay):    Repinta a 60 FPS fijo usando ultimas detecciones
//
// El hilo principal maneja el message loop de Windows y la bandeja del sistema.
// Los 3 threads del pipeline son independientes y se comunican via
// PipelineState (lock-free donde es posible).

#pragma once

#include "../framework.h"
#include "capture/IScreenCapture.h"
#include "detector/IContentDetector.h"
#include "detector/DetectionTracker.h"
#include "overlay/IOverlay.h"
#include "overlay/OverlayWindow.h"
#include "config/Config.h"
#include "pipeline/PipelineState.h"

namespace antipop {

class App {
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Inicializa todos los subsistemas.
    [[nodiscard]] bool Initialize(HINSTANCE hInstance, bool silentMode);

    // Inicia el pipeline de 3 threads.
    void Start();

    // Detiene todos los threads y libera recursos.
    void Stop();

    [[nodiscard]] bool IsRunning() const noexcept { return m_state.running.load(); }

    const config::AppConfig& GetConfig() const noexcept { return m_config.Get(); }

    // Bandeja del sistema
    bool SetupTrayIcon(HWND hwnd);
    void RemoveTrayIcon();
    void HandleTrayMessage(HWND hwnd, WPARAM wParam, LPARAM lParam);

private:
    // ---- Threads del pipeline ----
    void CaptureThread();
    void InferenceThread();
    void OverlayThread();

    // ---- Subsistemas ----
    std::unique_ptr<capture::IScreenCapture>   m_capture;
    std::unique_ptr<detector::IContentDetector> m_detector;
    std::unique_ptr<overlay::IOverlay>          m_overlay;

    // Puntero tipado al overlay concreto (para acceso a metodos especificos)
    overlay::OverlayWindow* m_overlayWindow = nullptr;

    config::Config m_config;

    // ---- Pipeline state (compartido entre threads) ----
    pipeline::PipelineState m_state;

    // ---- Threads ----
    std::thread m_captureThread;
    std::thread m_inferenceThread;
    std::thread m_overlayThread;

    // Rastreador de detecciones (usado solo por InferenceThread)
    detector::DetectionTracker m_tracker;

    // Temporal smoothing para evitar parpadeos de censura
    int m_framesWithoutDetection = 0;
    static constexpr int kFramesToClearCensor = 12;

    // ---- Bandeja del sistema ----
    NOTIFYICONDATAW m_trayIconData = {};
    bool            m_trayIconActive = false;
    HINSTANCE m_hInstance = nullptr;

    static constexpr UINT WM_TRAYICON = WM_USER + 1;
    static constexpr UINT ID_TRAY_EXIT    = 1001;
    static constexpr UINT ID_TRAY_TOGGLE  = 1002;
    static constexpr UINT ID_TRAY_ABOUT   = 1003;
};

} // namespace antipop
