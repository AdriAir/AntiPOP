// DxgiCapture.cpp : Implementacion de la captura de pantalla via DXGI Desktop Duplication API.

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

    // Crear textura de staging para leer pixels de la GPU a CPU
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width              = m_width;
    stagingDesc.Height             = m_height;
    stagingDesc.MipLevels          = 1;
    stagingDesc.ArraySize          = 1;
    stagingDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc.Count   = 1;
    stagingDesc.Usage              = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;

    HRESULT hr = m_device->CreateTexture2D(&stagingDesc, nullptr, &m_stagingTexture);
    if (FAILED(hr)) {
        LOG_ERROR("No se pudo crear la textura de staging: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    m_initialized = true;
    LOG_INFO("DXGI Capture inicializado: {}x{}", m_width, m_height);
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

    // Obtener el output principal (monitor primario)
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(0, &output);
    if (FAILED(hr)) {
        LOG_ERROR("No se encontro monitor primario");
        return false;
    }

    // Necesitamos IDXGIOutput1 para Desktop Duplication
    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) return false;

    // Obtener las dimensiones del monitor
    DXGI_OUTPUT_DESC outputDesc;
    output->GetDesc(&outputDesc);
    m_width  = outputDesc.DesktopCoordinates.right  - outputDesc.DesktopCoordinates.left;
    m_height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

    // Crear la duplicacion del escritorio
    hr = output1->DuplicateOutput(m_device.Get(), &m_duplication);
    if (FAILED(hr)) {
        LOG_ERROR("DuplicateOutput fallo: 0x{:08X}. "
                  "Asegurate de no estar en una sesion remota.", static_cast<unsigned>(hr));
        return false;
    }

    return true;
}

std::optional<CapturedFrame> DxgiCapture::CaptureFrame() {
    if (!m_initialized) return std::nullopt;

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;

    // Intentar adquirir el siguiente frame (timeout de 100ms)
    HRESULT hr = m_duplication->AcquireNextFrame(100, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // No hay frame nuevo, no es un error
        return std::nullopt;
    }

    if (FAILED(hr)) {
        // Si la duplicacion se perdio (cambio de resolucion, etc.), reinicializar
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            LOG_WARN("Desktop Duplication perdido, reinicializando...");
            m_duplication.Reset();
            m_stagingTexture.Reset();
            m_initialized = false;
            Initialize();
        }
        return std::nullopt;
    }

    // Obtener la textura del frame capturado
    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (FAILED(hr)) {
        m_duplication->ReleaseFrame();
        return std::nullopt;
    }

    // Copiar la textura del escritorio a nuestra textura de staging
    m_context->CopyResource(m_stagingTexture.Get(), desktopTexture.Get());

    // Mapear la textura de staging para leer los pixels en CPU
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        m_duplication->ReleaseFrame();
        return std::nullopt;
    }

    // Construir el frame capturado
    CapturedFrame frame;
    frame.width  = m_width;
    frame.height = m_height;
    frame.stride = m_width * 4;
    frame.data.resize(frame.stride * frame.height);

    // Copiar fila por fila (el stride de la GPU puede diferir)
    const auto* src = static_cast<const uint8_t*>(mapped.pData);
    for (uint32_t y = 0; y < m_height; ++y) {
        std::memcpy(
            frame.data.data() + y * frame.stride,
            src + y * mapped.RowPitch,
            frame.stride
        );
    }

    m_context->Unmap(m_stagingTexture.Get(), 0);
    m_duplication->ReleaseFrame();

    return frame;
}

void DxgiCapture::Shutdown() {
    if (m_duplication) {
        m_duplication.Reset();
    }
    m_stagingTexture.Reset();
    m_context.Reset();
    m_device.Reset();
    m_initialized = false;

    LOG_INFO("DXGI Capture liberado");
}

} // namespace antipop::capture
