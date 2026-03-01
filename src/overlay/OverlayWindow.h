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

    // Detecciones actuales que deben censurarse
    std::mutex                       m_detectionsMutex;
    std::vector<detector::Detection> m_currentDetections;

    static constexpr wchar_t kOverlayClassName[] = L"AntiPopOverlay";
};

} // namespace antipop::overlay
