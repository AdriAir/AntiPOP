// DxgiCapture.h : Captura de pantalla usando la Desktop Duplication API de DXGI.
// Esta API es la forma mas eficiente de capturar la pantalla en Windows 10/11,
// ya que opera a nivel de GPU y evita copias innecesarias a RAM.

#pragma once

#include "IScreenCapture.h"

namespace antipop::capture {

class DxgiCapture final : public IScreenCapture {
public:
    DxgiCapture() = default;
    ~DxgiCapture() override;

    // No copiable, movible
    DxgiCapture(const DxgiCapture&) = delete;
    DxgiCapture& operator=(const DxgiCapture&) = delete;
    DxgiCapture(DxgiCapture&&) noexcept = default;
    DxgiCapture& operator=(DxgiCapture&&) noexcept = default;

    [[nodiscard]] bool Initialize() override;
    [[nodiscard]] std::optional<CapturedFrame> CaptureFrame() override;
    void Shutdown() override;

    [[nodiscard]] uint32_t GetWidth() const noexcept override { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const noexcept override { return m_height; }

private:
    // Inicializa el dispositivo D3D11 y obtiene el output duplicado.
    [[nodiscard]] bool InitializeD3D();
    [[nodiscard]] bool InitializeDuplication();

    // Dispositivo D3D11 y contexto
    Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>  m_context;

    // DXGI Output Duplication
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> m_duplication;

    // Textura de staging para leer los pixels desde la GPU
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_stagingTexture;

    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    bool     m_initialized = false;
};

} // namespace antipop::capture
