// OverlayWindow.h : Overlay transparente usando ventana Win32 con WS_EX_LAYERED.
// La ventana es siempre-encima (topmost), transparente al raton (click-through),
// y solo dibuja rectangulos de censura sobre las detecciones.

#pragma once

#include "IOverlay.h"

namespace antipop::overlay {

class OverlayWindow final : public IOverlay {
public:
    OverlayWindow() = default;
    ~OverlayWindow() override;

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    [[nodiscard]] bool Initialize(HINSTANCE hInstance) override;
    void UpdateCensorRegions(const std::vector<detector::Detection>& detections) override;
    void ClearCensorRegions() override;
    void SetVisible(bool visible) override;
    void Shutdown() override;

    // Color y estilo del bloqueo de censura
    void SetCensorColor(COLORREF color) { m_censorColor = color; }

    // Tipo y configuracion de censura
    // censorType: 0=solido, 1=pixelado
    // pixelateBlockSize: tamano del bloque de pixelado en pixels
    void SetCensorStyle(int censorType, int pixelateBlockSize = 12) {
        m_censorType = censorType;
        m_pixelateBlockSize = pixelateBlockSize;
    }

private:
    // Window procedure estatico (reenvía a la instancia)
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    // Redibuja el overlay con las regiones de censura actuales.
    void Repaint();

    // Registra la clase de ventana del overlay.
    [[nodiscard]] bool RegisterOverlayClass(HINSTANCE hInstance);

    HWND      m_hwnd        = nullptr;
    HINSTANCE m_hInstance    = nullptr;
    COLORREF  m_censorColor = RGB(0, 0, 0);  // Negro por defecto

    // Tipo de censura: 0=solido (rectangulo negro), 1=pixelado (mosaico)
    int m_censorType = 1;  // Por defecto pixelado
    int m_pixelateBlockSize = 12;  // Tamano del bloque de pixelado

    // Detecciones actuales que deben censurarse
    std::mutex                       m_detectionsMutex;
    std::vector<detector::Detection> m_currentDetections;

    static constexpr wchar_t kOverlayClassName[] = L"AntiPopOverlay";

    // Timer para re-afirmar la posicion topmost periodicamente
    // (necesario para mantenerse encima de juegos borderless fullscreen)
    static constexpr UINT_PTR kTimerReassertTopmost = 100;
};

} // namespace antipop::overlay
