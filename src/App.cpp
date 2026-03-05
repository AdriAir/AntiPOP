// App.cpp : Implementacion del orquestador principal.
//
// ARQUITECTURA v2 - Pipeline paralelo de 3 threads:
//
//   CaptureThread():    Captura DXGI continua, publica frames
//   InferenceThread():  Lee frames, ejecuta ONNX + tracking, publica detecciones
//   OverlayThread():    Lee detecciones, repinta a 60 FPS fijo
//
// Frame skipping implicito: si la inferencia tarda mas que un ciclo de captura,
// el thread de inferencia salta al frame mas reciente automaticamente.
// El overlay repinta las ultimas detecciones conocidas a 60 FPS constante.

#include "App.h"
#include "capture/DxgiCapture.h"
#include "detector/OnnxDetector.h"
#include "config/AutoStart.h"
#include "utils/Logger.h"
#include "../Resource.h"

#include <intrin.h>  // Para _mm_pause()

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
        if (!config::AutoStart::Enable()) {
            LOG_WARN("No se pudo habilitar el inicio automatico con Windows");
        }
    } else if (!cfg.autoStartEnabled && config::AutoStart::IsEnabled()) {
        if (!config::AutoStart::Disable()) {
            LOG_WARN("No se pudo deshabilitar el inicio automatico con Windows");
        }
    }

    // 3. Inicializar captura de pantalla (DXGI Desktop Duplication)
    m_capture = std::make_unique<capture::DxgiCapture>();
    if (!m_capture->Initialize()) {
        LOG_ERROR("Fallo al inicializar la captura de pantalla");
        return false;
    }

    // 4. Inicializar detector de IA (ONNX Runtime con CUDA EP si disponible)
    auto onnxDetector = std::make_unique<detector::OnnxDetector>();
    onnxDetector->SetUseGpu(cfg.useGpuInference);
    onnxDetector->SetUseFP16(cfg.useFP16);
    m_detector = std::move(onnxDetector);
    auto modelPath = config::Config::GetProjectDirectory() / cfg.modelPath;
    if (!m_detector->Initialize(modelPath)) {
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

    // Guardar puntero tipado para acceso a metodos especificos
    m_overlayWindow = dynamic_cast<overlay::OverlayWindow*>(m_overlay.get());
    if (m_overlayWindow) {
        m_overlayWindow->SetCensorColor(RGB(cfg.censorColorR, cfg.censorColorG, cfg.censorColorB));
        m_overlayWindow->SetCensorStyle(cfg.censorType, cfg.pixelateBlockSize, cfg.censorExpansion);

        const char* censorTypeStr = (cfg.censorType == 0) ? "solido" : "pixelado";
        LOG_INFO("Censura configurada: tipo={}, bloque={}px, expansion={:.0f}%",
                 censorTypeStr, cfg.pixelateBlockSize, cfg.censorExpansion * 100.0f);
    }

    LOG_INFO("AntiPop inicializado correctamente (silentMode={})", silentMode);
    return true;
}

void App::Start() {
    if (m_state.running.load()) return;

    m_state.running.store(true);
    m_overlay->SetVisible(true);

    // Lanzar los 3 threads del pipeline
    m_captureThread   = std::thread(&App::CaptureThread, this);
    m_inferenceThread = std::thread(&App::InferenceThread, this);
    m_overlayThread   = std::thread(&App::OverlayThread, this);

    LOG_INFO("Pipeline de censura iniciado (3 threads: captura + inferencia + overlay)");
}

void App::Stop() {
    if (!m_state.running.load()) return;

    m_state.running.store(false);

    // Esperar a que los 3 threads terminen
    if (m_captureThread.joinable())   m_captureThread.join();
    if (m_inferenceThread.joinable()) m_inferenceThread.join();
    if (m_overlayThread.joinable())   m_overlayThread.join();

    if (m_overlay) {
        m_overlay->ClearCensorRegions();
        m_overlay->SetVisible(false);
    }

    m_tracker.Reset();
    m_framesWithoutDetection = 0;

    LOG_INFO("Pipeline de censura detenido");
}

// ============================================================================
// Thread 1: CAPTURA
// Captura frames DXGI continuamente y los publica para el thread de inferencia.
// No tiene framerate fijo - corre tan rapido como DXGI permite.
// ============================================================================
void App::CaptureThread() {
    LOG_INFO("[Captura] Thread iniciado");
    pipeline::PerfTimer timer;

    while (m_state.running.load(std::memory_order_relaxed)) {
        timer.Reset();

        auto frames = m_capture->CaptureAllFrames();

        if (!frames.empty()) {
            // Publicar frames para el thread de inferencia
            {
                std::lock_guard lock(m_state.frameMutex);
                m_state.latestFrames = std::move(frames);
            }
            m_state.captureFrameId.fetch_add(1, std::memory_order_release);
        }

        double elapsed = timer.ElapsedMs();

        // Si no hubo frame nuevo (AcquireNextFrame devolvio timeout),
        // dormir brevemente para no quemar CPU
        if (frames.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // Si la captura fue rapida, limitar a ~120 Hz para no saturar
        else if (elapsed < 8.0) {
            auto remaining = std::chrono::microseconds(8000) -
                             std::chrono::microseconds(static_cast<int64_t>(elapsed * 1000));
            if (remaining > std::chrono::microseconds(0)) {
                std::this_thread::sleep_for(remaining);
            }
        }
    }

    LOG_INFO("[Captura] Thread finalizado");
}

// ============================================================================
// Thread 2: INFERENCIA
// Lee el frame mas reciente, ejecuta preprocesado + ONNX + tracking.
// Frame skipping implicito: si hay frames acumulados, procesa solo el ultimo.
// ============================================================================
void App::InferenceThread() {
    LOG_INFO("[Inferencia] Thread iniciado");

    const auto& cfg = m_config.Get();
    const int metricsInterval = std::max(1, cfg.metricsLogInterval);

    uint64_t lastProcessedFrame = 0;
    pipeline::PerfTimer timer;
    pipeline::PerfTimer totalTimer;

    // Metricas rolling average
    double avgInferenceMs = 0.0;
    int metricsCounter = 0;

    while (m_state.running.load(std::memory_order_relaxed)) {
        uint64_t currentFrame = m_state.captureFrameId.load(std::memory_order_acquire);

        // No hay frame nuevo - esperar brevemente
        if (currentFrame <= lastProcessedFrame) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            continue;
        }

        totalTimer.Reset();

        // Log de frames saltados
        uint64_t skipped = currentFrame - lastProcessedFrame - 1;
        if (skipped > 0) {
            m_state.metrics.framesSkipped += skipped;
            LOG_DEBUG("[Inferencia] Saltados {} frames (inferencia mas lenta que captura)", skipped);
        }
        lastProcessedFrame = currentFrame;

        // Copiar frames bajo lock (rapido, solo mueve el vector)
        std::vector<capture::CapturedFrame> frames;
        {
            std::lock_guard lock(m_state.frameMutex);
            frames = std::move(m_state.latestFrames);
        }

        // Ejecutar deteccion en cada frame
        std::vector<detector::Detection> allDetections;

        for (auto& frame : frames) {
            timer.Reset();
            auto detections = m_detector->Detect(
                frame.data.data(),
                frame.width,
                frame.height,
                frame.stride
            );
            double inferMs = timer.ElapsedMs();

            // Actualizar metricas
            avgInferenceMs = avgInferenceMs * 0.9 + inferMs * 0.1;  // EMA

            // Offset de coordenadas al escritorio virtual
            for (auto& det : detections) {
                det.box.x += static_cast<float>(frame.originX);
                det.box.y += static_cast<float>(frame.originY);
            }

            allDetections.insert(allDetections.end(),
                std::make_move_iterator(detections.begin()),
                std::make_move_iterator(detections.end()));
        }

        // Tracking: suavizar detecciones entre frames
        auto trackedDetections = m_tracker.UpdateAndGetTrackedDetections(
            allDetections, kFramesToClearCensor);

        // Actualizar lógica de limpieza
        if (!trackedDetections.empty()) {
            m_framesWithoutDetection = 0;
        } else {
            m_framesWithoutDetection++;
            if (m_framesWithoutDetection >= kFramesToClearCensor) {
                m_tracker.Reset();
            }
        }

        // Publicar detecciones en el double buffer (lock-free)
        int writeSlot = 1 - m_state.activeSlot.load(std::memory_order_relaxed);
        m_state.slots[writeSlot].detections = std::move(trackedDetections);
        m_state.slots[writeSlot].frameId = currentFrame;
        m_state.slots[writeSlot].timestamp = std::chrono::steady_clock::now();

        // Swap atomico: el overlay ahora lee el nuevo slot
        m_state.activeSlot.store(writeSlot, std::memory_order_release);

        // Actualizar metricas periodicamente
        double totalMs = totalTimer.ElapsedMs();
        metricsCounter++;
        if (metricsCounter % metricsInterval == 0) {
            m_state.metrics.inferenceMs = avgInferenceMs;
            m_state.metrics.totalMs = totalMs;
            m_state.metrics.fps = (totalMs > 0) ? 1000.0 / totalMs : 0.0;
            m_state.metricsUpdated.store(true, std::memory_order_relaxed);

            LOG_INFO("[Inferencia] Avg: {:.1f}ms ({:.0f} FPS), Skipped: {}",
                     avgInferenceMs, m_state.metrics.fps,
                     m_state.metrics.framesSkipped);
        }
    }

    LOG_INFO("[Inferencia] Thread finalizado");
}

// ============================================================================
// Thread 3: OVERLAY
// Repinta a 60 FPS fijo usando las ultimas detecciones disponibles.
// Completamente independiente del ritmo de inferencia.
// Usa spin-wait preciso para los ultimos ~1ms (garantiza 60 FPS estable).
// ============================================================================
void App::OverlayThread() {
    const auto& cfg = m_config.Get();
    const int targetFps = std::clamp(cfg.overlayTargetFps, 1, 240);
    const auto frameTime = std::chrono::microseconds(1000000 / targetFps);

    LOG_INFO("[Overlay] Thread iniciado ({} FPS, {:.1f}ms/frame)",
             targetFps, 1000.0 / targetFps);

    using clock = std::chrono::steady_clock;

    uint64_t lastSeenFrameId = 0;
    int framesWithoutNewDetections = 0;

    while (m_state.running.load(std::memory_order_relaxed)) {
        auto frameStart = clock::now();

        // Leer slot activo (lock-free, sin contention)
        int readSlot = m_state.activeSlot.load(std::memory_order_acquire);
        auto& slot = m_state.slots[readSlot];

        bool hasNewData = (slot.frameId > lastSeenFrameId);
        if (hasNewData) {
            lastSeenFrameId = slot.frameId;
            framesWithoutNewDetections = 0;
        } else {
            framesWithoutNewDetections++;
        }

        // Actualizar overlay
        if (!slot.detections.empty()) {
            m_overlay->UpdateCensorRegions(slot.detections);
        } else {
            // Verificar si debemos limpiar (usando edad del ultimo resultado)
            auto age = clock::now() - slot.timestamp;
            if (age > std::chrono::milliseconds(kFramesToClearCensor * 100)) {
                m_overlay->ClearCensorRegions();
            } else if (m_overlayWindow) {
                // Solo repintar (sin cambiar detecciones)
                m_overlayWindow->RequestRepaint();
            }
        }

        // Sleep preciso para 60 FPS
        auto elapsed = clock::now() - frameStart;
        auto remaining = frameTime - elapsed;

        if (remaining > std::chrono::milliseconds(2)) {
            // Sleep grueso (cede CPU al OS)
            std::this_thread::sleep_for(remaining - std::chrono::milliseconds(1));
        }

        // Spin-wait preciso para los ultimos ~1ms
        while (clock::now() - frameStart < frameTime) {
            _mm_pause();  // Hint al CPU: estamos en spin-wait
        }
    }

    LOG_INFO("[Overlay] Thread finalizado");
}

// ============================================================================
// Bandeja del sistema (sin cambios respecto a v1)
// ============================================================================
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
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;

        AppendMenuW(hMenu, MF_STRING,
                    ID_TRAY_TOGGLE,
                    m_state.running.load() ? L"Pausar censura" : L"Reanudar censura");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_ABOUT, L"Acerca de AntiPop...");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Salir");

        SetForegroundWindow(hwnd);

        POINT pt;
        GetCursorPos(&pt);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                       pt.x, pt.y, 0, hwnd, nullptr);

        DestroyMenu(hMenu);
        break;
    }
    case WM_LBUTTONDBLCLK:
        if (m_state.running.load()) {
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
