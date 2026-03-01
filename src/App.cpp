// App.cpp : Implementacion del orquestador principal.
// Pipeline: Captura -> Deteccion IA -> Actualizacion Overlay.
// Se ejecuta en un hilo dedicado con intervalo configurable.

#include "App.h"
#include "capture/DxgiCapture.h"
#include "detector/OnnxDetector.h"
#include "overlay/OverlayWindow.h"
#include "config/AutoStart.h"
#include "utils/Logger.h"
#include "../Resource.h"

namespace antipop {

App::~App() {
    Stop();
    RemoveTrayIcon();
}

bool App::Initialize(HINSTANCE hInstance, bool silentMode) {
    m_hInstance = hInstance;

    // 1. Cargar configuracion
    auto configPath = config::Config::GetAppDirectory() / "antipop.cfg";
    if (!m_config.Load(configPath)) {
        LOG_WARN("Usando configuracion por defecto");
    }

    const auto& cfg = m_config.Get();

    // 2. Configurar inicio automatico segun configuracion
    if (cfg.autoStartEnabled && !config::AutoStart::IsEnabled()) {
        config::AutoStart::Enable();
    } else if (!cfg.autoStartEnabled && config::AutoStart::IsEnabled()) {
        config::AutoStart::Disable();
    }

    // 3. Inicializar captura de pantalla (DXGI Desktop Duplication)
    m_capture = std::make_unique<capture::DxgiCapture>();
    if (!m_capture->Initialize()) {
        LOG_ERROR("Fallo al inicializar la captura de pantalla");
        return false;
    }

    // 4. Inicializar detector de IA (ONNX Runtime)
    m_detector = std::make_unique<detector::OnnxDetector>();
    auto modelPath = config::Config::GetAppDirectory() / cfg.modelPath;
    if (!m_detector->Initialize(modelPath)) {
        // No es un error fatal: la app puede funcionar sin modelo
        // (util durante desarrollo antes de tener el modelo entrenado)
        LOG_WARN("Detector de IA no disponible. "
                 "Coloca el modelo ONNX en: {}", modelPath.string());
    }
    m_detector->SetConfidenceThreshold(cfg.confidenceThreshold);

    // 5. Inicializar overlay
    m_overlay = std::make_unique<overlay::OverlayWindow>();
    if (!m_overlay->Initialize(hInstance)) {
        LOG_ERROR("Fallo al inicializar el overlay");
        return false;
    }

    // Configurar color de censura
    auto* overlayWin = dynamic_cast<overlay::OverlayWindow*>(m_overlay.get());
    if (overlayWin) {
        overlayWin->SetCensorColor(RGB(cfg.censorColorR, cfg.censorColorG, cfg.censorColorB));
    }

    LOG_INFO("AntiPop inicializado correctamente (silentMode={})", silentMode);
    return true;
}

void App::Start() {
    if (m_running.load()) return;

    m_running.store(true);
    m_overlay->SetVisible(true);

    // Lanzar el pipeline en un hilo separado
    m_pipelineThread = std::thread(&App::PipelineLoop, this);

    LOG_INFO("Pipeline de censura iniciado");
}

void App::Stop() {
    if (!m_running.load()) return;

    m_running.store(false);

    if (m_pipelineThread.joinable()) {
        m_pipelineThread.join();
    }

    if (m_overlay) {
        m_overlay->ClearCensorRegions();
        m_overlay->SetVisible(false);
    }

    LOG_INFO("Pipeline de censura detenido");
}

void App::PipelineLoop() {
    const auto& cfg = m_config.Get();
    const auto interval = std::chrono::milliseconds(cfg.captureIntervalMs);

    LOG_INFO("Pipeline loop iniciado con intervalo de {}ms", cfg.captureIntervalMs);

    while (m_running.load()) {
        auto frameStart = std::chrono::steady_clock::now();

        // Paso 1: Capturar frame de la pantalla
        auto frame = m_capture->CaptureFrame();

        if (frame && frame->IsValid()) {
            // Paso 2: Detectar pulpos en el frame
            auto detections = m_detector->Detect(
                frame->data.data(),
                frame->width,
                frame->height,
                frame->stride
            );

            // Paso 3: Actualizar el overlay con las detecciones
            if (!detections.empty()) {
                m_overlay->UpdateCensorRegions(detections);
                LOG_DEBUG("Detectados {} objetos para censurar", detections.size());
            } else {
                m_overlay->ClearCensorRegions();
            }
        }

        // Dormir el tiempo restante del intervalo
        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (elapsed < interval) {
            std::this_thread::sleep_for(interval - elapsed);
        }
    }
}

bool App::SetupTrayIcon(HWND hwnd) {
    m_trayIconData = {};
    m_trayIconData.cbSize           = sizeof(NOTIFYICONDATAW);
    m_trayIconData.hWnd             = hwnd;
    m_trayIconData.uID              = 1;
    m_trayIconData.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_trayIconData.uCallbackMessage = WM_TRAYICON;
    m_trayIconData.hIcon            = LoadIconW(m_hInstance, MAKEINTRESOURCEW(IDI_ANTIPOP));
    wcscpy_s(m_trayIconData.szTip, L"AntiPop - Proteccion anti-pulpos activa");

    if (!Shell_NotifyIconW(NIM_ADD, &m_trayIconData)) {
        LOG_ERROR("No se pudo crear el icono de la bandeja del sistema");
        return false;
    }

    m_trayIconActive = true;
    LOG_INFO("Icono de bandeja del sistema creado");
    return true;
}

void App::RemoveTrayIcon() {
    if (m_trayIconActive) {
        Shell_NotifyIconW(NIM_DELETE, &m_trayIconData);
        m_trayIconActive = false;
    }
}

void App::HandleTrayMessage(HWND hwnd, [[maybe_unused]] WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(lParam)) {
    case WM_RBUTTONUP: {
        // Mostrar menu contextual
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;

        AppendMenuW(hMenu, MF_STRING,
                    ID_TRAY_TOGGLE,
                    m_running.load() ? L"Pausar censura" : L"Reanudar censura");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_ABOUT, L"Acerca de AntiPop...");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Salir");

        // Necesario para que el menu se cierre al hacer click fuera
        SetForegroundWindow(hwnd);

        POINT pt;
        GetCursorPos(&pt);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                       pt.x, pt.y, 0, hwnd, nullptr);

        DestroyMenu(hMenu);
        break;
    }
    case WM_LBUTTONDBLCLK:
        // Doble click: toggle censura
        if (m_running.load()) {
            Stop();
            wcscpy_s(m_trayIconData.szTip, L"AntiPop - Censura PAUSADA");
        } else {
            Start();
            wcscpy_s(m_trayIconData.szTip, L"AntiPop - Proteccion anti-pulpos activa");
        }
        Shell_NotifyIconW(NIM_MODIFY, &m_trayIconData);
        break;
    }
}

} // namespace antipop
