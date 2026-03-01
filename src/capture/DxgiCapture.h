// DxgiCapture.h : Captura de pantalla usando la Desktop Duplication API de DXGI.
// Esta API es la forma mas eficiente de capturar la pantalla en Windows 10/11,
// ya que opera a nivel de GPU y evita copias innecesarias a RAM.
// Soporta multiples monitores: crea una duplicacion por cada output.

#pragma once

#include "IScreenCapture.h"

namespace antipop::capture {

class DxgiCapture final : public IScreenCapture {
public:
    DxgiCapture() = default;
    ~DxgiCapture() override;

    // No copiable
    DxgiCapture(const DxgiCapture&) = delete;
    DxgiCapture& operator=(const DxgiCapture&) = delete;

    [[nodiscard]] bool Initialize() override;
    [[nodiscard]] std::vector<CapturedFrame> CaptureAllFrames() override;
    void Shutdown() override;

private:
    // Recursos por monitor
    struct MonitorCapture {
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
        Microsoft::WRL::ComPtr<ID3D11Texture2D>        stagingTexture;
        uint32_t width   = 0;
        uint32_t height  = 0;
        int32_t  originX = 0;
        int32_t  originY = 0;
    };

    [[nodiscard]] bool InitializeD3D();
    [[nodiscard]] bool InitializeDuplication();

    // Captura un frame de un monitor individual
    [[nodiscard]] CapturedFrame CaptureMonitor(MonitorCapture& monitor);

    // Dispositivo D3D11 y contexto (compartidos entre todos los monitores)
    Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>  m_context;

    std::vector<MonitorCapture> m_monitors;
    bool m_initialized = false;
};

} // namespace antipop::capture
