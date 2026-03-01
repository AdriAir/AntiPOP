// DxgiCapture.cpp : Implementacion de la captura de pantalla via DXGI Desktop Duplication API.
// Soporta multiples monitores: enumera todos los outputs y crea una duplicacion por cada uno.

#include "DxgiCapture.h"
#include "../utils/Logger.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace antipop::capture {

DxgiCapture::~DxgiCapture() {
    Shutdown();
}

bool DxgiCapture::Initialize() {
    if (m_initialized) return true;

    if (!InitializeD3D()) {
        LOG_ERROR("No se pudo inicializar D3D11");
        return false;
    }

    if (!InitializeDuplication()) {
        LOG_ERROR("No se pudo inicializar Desktop Duplication");
        return false;
    }

    m_initialized = true;
    LOG_INFO("DXGI Capture inicializado con {} monitor(es)", m_monitors.size());
    return true;
}

bool DxgiCapture::InitializeD3D() {
    D3D_FEATURE_LEVEL featureLevel;
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // Adaptador por defecto
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        nullptr, 0,                 // Feature levels por defecto
        D3D11_SDK_VERSION,
        &m_device,
        &featureLevel,
        &m_context
    );

    if (FAILED(hr)) {
        LOG_ERROR("D3D11CreateDevice fallo: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    return true;
}

bool DxgiCapture::InitializeDuplication() {
    // Obtener el adaptador DXGI desde el dispositivo D3D11
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_device.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return false;

    // Enumerar TODOS los monitores conectados
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    for (UINT i = 0; adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND; ++i) {
        // Necesitamos IDXGIOutput1 para Desktop Duplication
        Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
        hr = output.As(&output1);
        if (FAILED(hr)) {
            output.Reset();
            continue;
        }

        // Obtener las dimensiones y posicion del monitor
        DXGI_OUTPUT_DESC desc;
        output->GetDesc(&desc);

        MonitorCapture mon;
        mon.width   = desc.DesktopCoordinates.right  - desc.DesktopCoordinates.left;
        mon.height  = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
        mon.originX = desc.DesktopCoordinates.left;
        mon.originY = desc.DesktopCoordinates.top;

        // Crear la duplicacion del escritorio para este monitor
        hr = output1->DuplicateOutput(m_device.Get(), &mon.duplication);
        if (FAILED(hr)) {
            LOG_WARN("No se pudo duplicar monitor {}: 0x{:08X}", i, static_cast<unsigned>(hr));
            output.Reset();
            continue;
        }

        // Crear textura de staging para leer pixels de la GPU a CPU
        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width            = mon.width;
        stagingDesc.Height           = mon.height;
        stagingDesc.MipLevels        = 1;
        stagingDesc.ArraySize        = 1;
        stagingDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage            = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;

        hr = m_device->CreateTexture2D(&stagingDesc, nullptr, &mon.stagingTexture);
        if (FAILED(hr)) {
            LOG_WARN("No se pudo crear staging texture para monitor {}: 0x{:08X}",
                     i, static_cast<unsigned>(hr));
            output.Reset();
            continue;
        }

        LOG_INFO("Monitor {} registrado: {}x{} en ({}, {})",
                 i, mon.width, mon.height, mon.originX, mon.originY);
        m_monitors.push_back(std::move(mon));
        output.Reset();
    }

    if (m_monitors.empty()) {
        LOG_ERROR("No se encontro ningun monitor para capturar");
        return false;
    }

    return true;
}

CapturedFrame DxgiCapture::CaptureMonitor(MonitorCapture& mon) {
    CapturedFrame frame;

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;

    // Intentar adquirir el siguiente frame (timeout de 50ms por monitor)
    HRESULT hr = mon.duplication->AcquireNextFrame(50, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return frame;  // No hay frame nuevo, devolver frame vacio (IsValid() = false)
    }

    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            LOG_WARN("Desktop Duplication perdido en monitor ({}, {}), reinicializando...",
                     mon.originX, mon.originY);
            Shutdown();
            if (!Initialize()) {
                LOG_ERROR("Fallo al reinicializar Desktop Duplication");
            }
        }
        return frame;
    }

    // Obtener la textura del frame capturado
    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr)) {
        mon.duplication->ReleaseFrame();
        return frame;
    }

    // Copiar la textura del escritorio a nuestra textura de staging
    m_context->CopyResource(mon.stagingTexture.Get(), desktopTexture.Get());

    // Mapear la textura de staging para leer los pixels en CPU
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = m_context->Map(mon.stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        mon.duplication->ReleaseFrame();
        return frame;
    }

    // Construir el frame capturado
    frame.width   = mon.width;
    frame.height  = mon.height;
    frame.stride  = mon.width * 4;
    frame.originX = mon.originX;
    frame.originY = mon.originY;
    frame.data.resize(static_cast<size_t>(frame.stride) * frame.height);

    // Copiar fila por fila (el stride de la GPU puede diferir)
    const auto* src = static_cast<const uint8_t*>(mapped.pData);
    for (uint32_t y = 0; y < mon.height; ++y) {
        std::memcpy(
            frame.data.data() + y * frame.stride,
            src + y * mapped.RowPitch,
            frame.stride
        );
    }

    m_context->Unmap(mon.stagingTexture.Get(), 0);
    mon.duplication->ReleaseFrame();

    return frame;
}

std::vector<CapturedFrame> DxgiCapture::CaptureAllFrames() {
    if (!m_initialized) return {};

    std::vector<CapturedFrame> frames;
    frames.reserve(m_monitors.size());

    for (auto& mon : m_monitors) {
        auto frame = CaptureMonitor(mon);
        if (frame.IsValid()) {
            frames.push_back(std::move(frame));
        }
    }

    return frames;
}

void DxgiCapture::Shutdown() {
    m_monitors.clear();
    m_context.Reset();
    m_device.Reset();
    m_initialized = false;

    LOG_INFO("DXGI Capture liberado");
}

} // namespace antipop::capture
