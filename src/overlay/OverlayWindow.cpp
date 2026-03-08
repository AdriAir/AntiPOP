// OverlayWindow.cpp : Implementacion del overlay transparente de censura.
// Usa una ventana WS_EX_LAYERED + WS_EX_TRANSPARENT para ser invisible
// a la interaccion del usuario, mientras dibuja bloques opacos sobre
// las areas donde se detectan pulpos.
//
// OPTIMIZACIONES v2:
// - GDI objects cacheados: HDC, HBITMAP y brushes se crean una vez
// - Repaint() solo limpia y dibuja, sin create/destroy de objects
// - Ahorro estimado: ~0.5-1ms por frame

#include "OverlayWindow.h"
#include "../utils/Logger.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "msimg32.lib")

namespace antipop::overlay {

OverlayWindow::~OverlayWindow() {
    Shutdown();
}

void OverlayWindow::InitializeGDICache() {
    if (!m_hwnd) return;

    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);
    m_cachedWidth  = clientRect.right  - clientRect.left;
    m_cachedHeight = clientRect.bottom - clientRect.top;

    if (m_cachedWidth <= 0 || m_cachedHeight <= 0) return;

    // Crear memory DC y bitmap persistentes
    HDC hdcScreen = GetDC(m_hwnd);
    m_hdcMem = CreateCompatibleDC(hdcScreen);
    m_hBitmap = CreateCompatibleBitmap(hdcScreen, m_cachedWidth, m_cachedHeight);
    m_hOldBitmap = static_cast<HBITMAP>(SelectObject(m_hdcMem, m_hBitmap));
    ReleaseDC(m_hwnd, hdcScreen);

    // Crear brushes persistentes
    m_transparentBrush = CreateSolidBrush(RGB(0, 0, 0));  // Color key (transparente)
    m_solidBrush = CreateSolidBrush(RGB(1, 1, 1));        // Casi negro (no es color key)

    // Brushes para los 3 tonos de pixelado
    m_pixelateBrushes[0] = CreateSolidBrush(RGB(40, 40, 40));
    m_pixelateBrushes[1] = CreateSolidBrush(RGB(50, 50, 50));
    m_pixelateBrushes[2] = CreateSolidBrush(RGB(60, 60, 60));

    LOG_INFO("GDI cache inicializado: {}x{}", m_cachedWidth, m_cachedHeight);

#ifdef _DEBUG
    // Fuente monoespaciada para el panel de debug
    m_debugFont = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
        L"Consolas"
    );
#endif
}

#ifdef _DEBUG
void OverlayWindow::SetDebugStats(const DebugStats& stats) {
    std::lock_guard lock(m_debugStatsMutex);
    m_debugStats = stats;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void OverlayWindow::DrawDebugPanel() {
    if (!m_hdcMem || !m_debugFont) return;

    DebugStats stats;
    {
        std::lock_guard lock(m_debugStatsMutex);
        stats = m_debugStats;
    }

    constexpr int panelX = 10, panelY = 10;
    constexpr int panelW = 280, panelH = 120;
    RECT panelRect = { panelX, panelY, panelX + panelW, panelY + panelH };

    HBRUSH bgBrush = CreateSolidBrush(RGB(20, 20, 20));
    FillRect(m_hdcMem, &panelRect, bgBrush);
    DeleteObject(bgBrush);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
    HPEN oldPen = static_cast<HPEN>(SelectObject(m_hdcMem, pen));
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(m_hdcMem, GetStockObject(NULL_BRUSH)));
    Rectangle(m_hdcMem, panelX, panelY, panelX + panelW, panelY + panelH);
    SelectObject(m_hdcMem, oldPen);
    SelectObject(m_hdcMem, oldBrush);
    DeleteObject(pen);

    HFONT oldFont = static_cast<HFONT>(SelectObject(m_hdcMem, m_debugFont));
    SetBkMode(m_hdcMem, TRANSPARENT);

    wchar_t buf[80];

    SetTextColor(m_hdcMem, RGB(200, 200, 200));
    TextOutW(m_hdcMem, panelX + 8, panelY + 8, L"[AntiPop DEBUG]", 15);

    swprintf_s(buf, L"Modo:    %s", stats.usingGpu ? L"GPU (CUDA)" : L"CPU");
    SetTextColor(m_hdcMem, stats.usingGpu ? RGB(80, 220, 80) : RGB(220, 180, 80));
    TextOutW(m_hdcMem, panelX + 8, panelY + 26, buf, static_cast<int>(wcslen(buf)));

    swprintf_s(buf, L"Det:     %5.1f FPS  (%4.1f ms)", stats.inferenceFps, stats.inferenceMs);
    SetTextColor(m_hdcMem, RGB(100, 180, 255));
    TextOutW(m_hdcMem, panelX + 8, panelY + 44, buf, static_cast<int>(wcslen(buf)));

    swprintf_s(buf, L"Overlay: %5.1f FPS", stats.overlayFps);
    TextOutW(m_hdcMem, panelX + 8, panelY + 62, buf, static_cast<int>(wcslen(buf)));

    swprintf_s(buf, L"Detecciones: %d", stats.detectionCount);
    SetTextColor(m_hdcMem, stats.detectionCount > 0 ? RGB(255, 100, 100) : RGB(150, 150, 150));
    TextOutW(m_hdcMem, panelX + 8, panelY + 80, buf, static_cast<int>(wcslen(buf)));

    swprintf_s(buf, L"Saltados: %llu", stats.framesSkipped);
    SetTextColor(m_hdcMem, RGB(150, 150, 150));
    TextOutW(m_hdcMem, panelX + 8, panelY + 98, buf, static_cast<int>(wcslen(buf)));

    SelectObject(m_hdcMem, oldFont);
}
#endif

void OverlayWindow::ReleaseGDICache() {
    if (m_hdcMem) {
        if (m_hOldBitmap) SelectObject(m_hdcMem, m_hOldBitmap);
        DeleteDC(m_hdcMem);
        m_hdcMem = nullptr;
        m_hOldBitmap = nullptr;
    }
    if (m_hBitmap) { DeleteObject(m_hBitmap); m_hBitmap = nullptr; }
    if (m_transparentBrush) { DeleteObject(m_transparentBrush); m_transparentBrush = nullptr; }
    if (m_solidBrush) { DeleteObject(m_solidBrush); m_solidBrush = nullptr; }
    for (auto& brush : m_pixelateBrushes) {
        if (brush) { DeleteObject(brush); brush = nullptr; }
    }
    m_cachedWidth = m_cachedHeight = 0;

#ifdef _DEBUG
    if (m_debugFont) { DeleteObject(m_debugFont); m_debugFont = nullptr; }
#endif
}

bool OverlayWindow::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    if (!RegisterOverlayClass(hInstance)) {
        LOG_ERROR("No se pudo registrar la clase de ventana del overlay");
        return false;
    }

    // Obtener dimensiones de la pantalla completa (multi-monitor: virtual screen)
    const int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClassName,
        L"",
        WS_POPUP,
        screenX, screenY,
        screenW, screenH,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!m_hwnd) {
        LOG_ERROR("No se pudo crear la ventana overlay: {}", GetLastError());
        return false;
    }

    // Configurar transparencia por color key
    SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    // Timer para re-afirmar topmost periodicamente
    SetTimer(m_hwnd, kTimerReassertTopmost, 2000, nullptr);

    // Inicializar GDI cache DESPUES de crear la ventana
    InitializeGDICache();

    LOG_INFO("Overlay inicializado: {}x{} en ({}, {})", screenW, screenH, screenX, screenY);
    return true;
}

bool OverlayWindow::RegisterOverlayClass(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = StaticWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kOverlayClassName;

    return RegisterClassExW(&wc) != 0;
}

LRESULT CALLBACK OverlayWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(hwnd, msg, wp, lp);
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        Repaint();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (wp == kTimerReassertTopmost) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    case WM_DISPLAYCHANGE:
        {
            const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
            const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
            const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);

            // Recrear GDI cache con nuevas dimensiones
            ReleaseGDICache();
            InitializeGDICache();
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void OverlayWindow::Repaint() {
    if (!m_hwnd || !m_hdcMem) return;

    // Limpiar con color de transparencia (color key = negro puro)
    RECT fullRect = { 0, 0, m_cachedWidth, m_cachedHeight };
    FillRect(m_hdcMem, &fullRect, m_transparentBrush);

    // Dibujar censura sobre las detecciones
    {
        std::lock_guard lock(m_detectionsMutex);
        if (!m_currentDetections.empty()) {
            if (m_censorType == 0) {
                // Modo: Rectangulo solido
                HBRUSH brush = (m_censorColor == RGB(0, 0, 0))
                               ? m_solidBrush : nullptr;
                HBRUSH customBrush = nullptr;

                if (!brush) {
                    customBrush = CreateSolidBrush(m_censorColor);
                    brush = customBrush;
                }

                for (const auto& det : m_currentDetections) {
                    auto expanded = det.box.Expanded(m_censorExpansion);
                    RECT r = expanded.ToRECT();

                    r.left   = std::max(r.left, 0L);
                    r.top    = std::max(r.top, 0L);
                    r.right  = std::min(r.right, static_cast<LONG>(m_cachedWidth));
                    r.bottom = std::min(r.bottom, static_cast<LONG>(m_cachedHeight));

                    FillRect(m_hdcMem, &r, brush);
                }

                if (customBrush) DeleteObject(customBrush);

            } else if (m_censorType == 1) {
                // Modo: Pixelado (mosaico) - usa brushes cacheados
                for (const auto& det : m_currentDetections) {
                    auto expanded = det.box.Expanded(m_censorExpansion);
                    RECT r = expanded.ToRECT();

                    r.left   = std::max(r.left, 0L);
                    r.top    = std::max(r.top, 0L);
                    r.right  = std::min(r.right, static_cast<LONG>(m_cachedWidth));
                    r.bottom = std::min(r.bottom, static_cast<LONG>(m_cachedHeight));

                    const int blockSize = m_pixelateBlockSize;
                    for (LONG by = r.top; by < r.bottom; by += blockSize) {
                        for (LONG bx = r.left; bx < r.right; bx += blockSize) {
                            RECT blockRect = {
                                bx,
                                by,
                                std::min(bx + blockSize, r.right),
                                std::min(by + blockSize, r.bottom)
                            };

                            int colorVar = ((bx / blockSize) + (by / blockSize)) % 3;
                            FillRect(m_hdcMem, &blockRect, m_pixelateBrushes[colorVar]);
                        }
                    }
                }
            }
        }
    }

#ifdef _DEBUG
    DrawDebugPanel();
#endif

    // Copiar el buffer al DC de la ventana (ya tiene color key configurado)
    HDC hdcWindow = GetDC(m_hwnd);
    BitBlt(hdcWindow, 0, 0, m_cachedWidth, m_cachedHeight, m_hdcMem, 0, 0, SRCCOPY);
    ReleaseDC(m_hwnd, hdcWindow);
}

void OverlayWindow::RequestRepaint() {
    Repaint();
}

void OverlayWindow::UpdateCensorRegions(const std::vector<detector::Detection>& detections) {
    {
        std::lock_guard lock(m_detectionsMutex);
        m_currentDetections = detections;
    }

    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
        Repaint();
    }
}

void OverlayWindow::ClearCensorRegions() {
    {
        std::lock_guard lock(m_detectionsMutex);
        m_currentDetections.clear();
    }

    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
        Repaint();
    }
}

void OverlayWindow::SetVisible(bool visible) {
    if (m_hwnd) {
        ShowWindow(m_hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
}

void OverlayWindow::Shutdown() {
    ReleaseGDICache();

    if (m_hwnd) {
        KillTimer(m_hwnd, kTimerReassertTopmost);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_hInstance) {
        UnregisterClassW(kOverlayClassName, m_hInstance);
    }
    LOG_INFO("Overlay liberado");
}

} // namespace antipop::overlay
