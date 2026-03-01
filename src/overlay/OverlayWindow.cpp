// OverlayWindow.cpp : Implementacion del overlay transparente de censura.
// Usa una ventana WS_EX_LAYERED + WS_EX_TRANSPARENT para ser invisible
// a la interaccion del usuario, mientras dibuja bloques opacos sobre
// las areas donde se detectan pulpos.

#include "OverlayWindow.h"
#include "../utils/Logger.h"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "msimg32.lib")

namespace antipop::overlay {

OverlayWindow::~OverlayWindow() {
    Shutdown();
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

    // Crear ventana overlay:
    // - WS_EX_LAYERED: permite transparencia per-pixel
    // - WS_EX_TRANSPARENT: click-through (los clicks pasan a la ventana de abajo)
    // - WS_EX_TOPMOST: siempre encima de las demas ventanas
    // - WS_EX_TOOLWINDOW: no aparece en la barra de tareas ni en Alt+Tab
    // - WS_EX_NOACTIVATE: no roba el foco al usuario
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClassName,
        L"",                        // Sin titulo
        WS_POPUP,                   // Sin bordes ni decoracion
        screenX, screenY,
        screenW, screenH,
        nullptr,                    // Sin ventana padre
        nullptr,                    // Sin menu
        hInstance,
        this                        // Pasar puntero a la instancia
    );

    if (!m_hwnd) {
        LOG_ERROR("No se pudo crear la ventana overlay: {}", GetLastError());
        return false;
    }

    // Configurar la ventana como completamente transparente inicialmente.
    // Usamos UpdateLayeredWindow con un bitmap para control per-pixel.
    SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    // Timer para re-afirmar topmost periodicamente (compatibilidad con juegos)
    SetTimer(m_hwnd, kTimerReassertTopmost, 2000, nullptr);

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
    wc.hbrBackground = nullptr;  // Sin fondo (transparente)
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
        HDC hdc = BeginPaint(hwnd, &ps);
        Repaint();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;  // No borrar el fondo (evita parpadeo)
    case WM_TIMER:
        if (wp == kTimerReassertTopmost) {
            // Re-afirmar posicion topmost (algunas apps/juegos luchan por ser topmost)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    case WM_DISPLAYCHANGE:
        // Cambio de resolucion: reposicionar la ventana
        {
            const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
            const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
            const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void OverlayWindow::Repaint() {
    if (!m_hwnd) return;

    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);
    const int width  = clientRect.right  - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;

    // Crear un buffer de doble buffering para evitar parpadeo
    HDC hdcScreen = GetDC(m_hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    HBITMAP hOldBitmap = static_cast<HBITMAP>(SelectObject(hdcMem, hBitmap));

    // Limpiar con color de transparencia (color key)
    HBRUSH hTransBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdcMem, &clientRect, hTransBrush);
    DeleteObject(hTransBrush);

    // Dibujar rectangulos de censura sobre las detecciones
    {
        std::lock_guard lock(m_detectionsMutex);
        if (!m_currentDetections.empty()) {
            // Usar un color ligeramente distinto al color key para las censuras
            COLORREF censorColor = (m_censorColor == RGB(0, 0, 0))
                                   ? RGB(1, 1, 1)   // Casi negro pero no el color key
                                   : m_censorColor;
            HBRUSH hCensorBrush = CreateSolidBrush(censorColor);

            for (const auto& det : m_currentDetections) {
                // Expandir un 10% para asegurar cobertura completa
                auto expanded = det.box.Expanded(0.1f);
                RECT r = expanded.ToRECT();

                // Clamp a los limites de la ventana
                r.left   = std::max(r.left, 0L);
                r.top    = std::max(r.top, 0L);
                r.right  = std::min(r.right, static_cast<LONG>(width));
                r.bottom = std::min(r.bottom, static_cast<LONG>(height));

                FillRect(hdcMem, &r, hCensorBrush);
            }

            DeleteObject(hCensorBrush);
        }
    }

    // Actualizar la ventana layered con el bitmap renderizado
    POINT ptPos  = { clientRect.left, clientRect.top };
    SIZE  szSize = { width, height };
    POINT ptSrc  = { 0, 0 };

    BLENDFUNCTION blend = {};
    blend.BlendOp             = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat         = 0;

    // Actualizar con color key: el negro puro (0,0,0) sera transparente
    SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    // Copiar el buffer al DC de la ventana
    HDC hdcWindow = GetDC(m_hwnd);
    BitBlt(hdcWindow, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);
    ReleaseDC(m_hwnd, hdcWindow);

    // Limpiar
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(m_hwnd, hdcScreen);
}

void OverlayWindow::UpdateCensorRegions(const std::vector<detector::Detection>& detections) {
    {
        std::lock_guard lock(m_detectionsMutex);
        m_currentDetections = detections;
    }

    // Forzar un repintado
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
