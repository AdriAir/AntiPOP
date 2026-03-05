// DxgiCapture.cpp : Implementacion de la captura de pantalla via DXGI Desktop Duplication API.
// Soporta multiples monitores: enumera todos los outputs y crea una duplicacion por cada uno.
//
// OPTIMIZACIONES v2:
// - Modo GPU: textura compartida GPU-only para zero-copy (sin staging ni memcpy)
// - AcquireNextFrame con timeout 0 para non-blocking capture
// - Staging texture solo se crea/usa en modo CPU (fallback)

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

    // Crear textura compartida para modo GPU zero-copy
    InitializeSharedTexture();

    m_initialized = true;
    LOG_INFO("DXGI Capture inicializado con {} monitor(es)", m_monitors.size());
    return true;
}

bool DxgiCapture::InitializeD3D() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;  // Necesario para interop DXGI
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // Especificar feature level para mejor rendimiento
    D3D_FEATURE_LEVEL requestedLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    D3D_FEATURE_LEVEL obtainedLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        requestedLevels, _countof(requestedLevels),
        D3D11_SDK_VERSION,
        &m_device,
        &obtainedLevel,
        &m_context
    );

    if (FAILED(hr)) {
        LOG_ERROR("D3D11CreateDevice fallo: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    LOG_INFO("D3D11 inicializado (feature level: {:#x})",
             static_cast<unsigned>(obtainedLevel));
    return true;
}

bool DxgiCapture::InitializeDuplication() {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_device.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    for (UINT i = 0; adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND; ++i) {
        Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
        hr = output.As(&output1);
        if (FAILED(hr)) {
            output.Reset();
            continue;
        }

        DXGI_OUTPUT_DESC desc;
        output->GetDesc(&desc);

        MonitorCapture mon;
        mon.width   = desc.DesktopCoordinates.right  - desc.DesktopCoordinates.left;
        mon.height  = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
        mon.originX = desc.DesktopCoordinates.left;
        mon.originY = desc.DesktopCoordinates.top;

        hr = output1->DuplicateOutput(m_device.Get(), &mon.duplication);
        if (FAILED(hr)) {
            LOG_WARN("No se pudo duplicar monitor {}: 0x{:08X}", i, static_cast<unsigned>(hr));
            output.Reset();
            continue;
        }

        // Staging texture para modo CPU (fallback cuando no hay CUDA)
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

void DxgiCapture::InitializeSharedTexture() {
    if (m_monitors.empty()) return;

    // Crear textura GPU-only compartida para modo zero-copy.
    // Dimensiones del primer monitor (el pipeline GPU procesa un monitor a la vez).
    const auto& mon = m_monitors[0];

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = mon.width;
    desc.Height           = mon.height;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;  // GPU-only (sin CPU access)
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags        = D3D11_RESOURCE_MISC_SHARED;  // Para CUDA interop

    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_sharedTexture);
    if (FAILED(hr)) {
        LOG_WARN("No se pudo crear shared texture para GPU mode: 0x{:08X}",
                 static_cast<unsigned>(hr));
        // No es fatal: el modo CPU sigue funcionando
    } else {
        LOG_INFO("Shared texture GPU creada: {}x{} (zero-copy mode disponible)",
                 mon.width, mon.height);
    }
}

bool DxgiCapture::CaptureToGpuTexture() {
    if (!m_initialized || m_monitors.empty() || !m_sharedTexture) return false;

    auto& mon = m_monitors[0];  // Monitor primario

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;

    // Timeout 0: non-blocking, devuelve inmediatamente si no hay frame nuevo
    HRESULT hr = mon.duplication->AcquireNextFrame(0, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;  // No hay frame nuevo
    }

    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            LOG_WARN("Desktop Duplication perdido, reinicializando...");
            Shutdown();
            (void)Initialize();
        }
        return false;
    }

    // Copiar GPU->GPU (rapidisimo, ~0.1ms)
    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (SUCCEEDED(hr)) {
        m_context->CopyResource(m_sharedTexture.Get(), desktopTexture.Get());
    }

    mon.duplication->ReleaseFrame();
    return SUCCEEDED(hr);
}

CapturedFrame DxgiCapture::CaptureMonitor(MonitorCapture& mon) {
    CapturedFrame frame;

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;

    // Timeout reducido a 16ms (un frame a 60 FPS) en vez de 50ms
    HRESULT hr = mon.duplication->AcquireNextFrame(16, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return frame;
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

    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr)) {
        mon.duplication->ReleaseFrame();
        return frame;
    }

    // Copiar a staging texture para lectura CPU
    m_context->CopyResource(mon.stagingTexture.Get(), desktopTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = m_context->Map(mon.stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        mon.duplication->ReleaseFrame();
        return frame;
    }

    frame.width   = mon.width;
    frame.height  = mon.height;
    frame.stride  = mon.width * 4;
    frame.originX = mon.originX;
    frame.originY = mon.originY;
    frame.data.resize(static_cast<size_t>(frame.stride) * frame.height);

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
    m_sharedTexture.Reset();
    m_monitors.clear();
    m_context.Reset();
    m_device.Reset();
    m_initialized = false;

    LOG_INFO("DXGI Capture liberado");
}

} // namespace antipop::capture
