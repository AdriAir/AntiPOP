// DxgiCapture.h : Captura de pantalla usando la Desktop Duplication API de DXGI.
// Esta API es la forma mas eficiente de capturar la pantalla en Windows 10/11,
// ya que opera a nivel de GPU y evita copias innecesarias a RAM.
// Soporta multiples monitores: crea una duplicacion por cada output.
//
// MODOS DE OPERACION (v2):
//   - Modo CPU (default): GPU -> Staging -> memcpy -> CPU vector
//     Usado cuando la inferencia corre en CPU (sin CUDA)
//
//   - Modo GPU (ANTIPOP_USE_CUDA): GPU -> GPU shared texture (zero-copy)
//     Usado cuando la inferencia corre en GPU via CUDA EP
//     Elimina staging texture + Map + memcpy (~3-5ms ahorro por frame)

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

    // ---- Modo GPU: acceso directo a textura compartida ----

    // Captura el frame mas reciente directamente a una textura GPU compartida.
    // No copia datos a CPU. Retorna true si hay frame nuevo.
    // La textura se puede registrar con CUDA para interop.
    [[nodiscard]] bool CaptureToGpuTexture();

    // Devuelve la textura GPU compartida (null si no inicializado)
    [[nodiscard]] ID3D11Texture2D* GetSharedTexture() const { return m_sharedTexture.Get(); }

    // Devuelve el D3D11Device (necesario para CUDA interop)
    [[nodiscard]] ID3D11Device* GetDevice() const { return m_device.Get(); }

    // Dimensiones del primer monitor
    [[nodiscard]] uint32_t GetPrimaryWidth()  const { return m_monitors.empty() ? 0 : m_monitors[0].width; }
    [[nodiscard]] uint32_t GetPrimaryHeight() const { return m_monitors.empty() ? 0 : m_monitors[0].height; }

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
    void InitializeSharedTexture();

    [[nodiscard]] CapturedFrame CaptureMonitor(MonitorCapture& monitor, std::vector<uint8_t>& buffer);

    // D3D11 (compartidos entre todos los monitores)
    Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>  m_context;

    // Textura compartida GPU-only para modo zero-copy
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_sharedTexture;

    std::vector<MonitorCapture> m_monitors;
    bool m_initialized = false;

    // Buffers pre-alocados para evitar allocation por frame (~8MB/frame eliminado)
    std::vector<std::vector<uint8_t>> m_frameBuffers;
};

} // namespace antipop::capture
