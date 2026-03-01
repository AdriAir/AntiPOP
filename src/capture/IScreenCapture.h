// IScreenCapture.h : Interfaz abstracta para captura de pantalla.
// Permite desacoplar la estrategia de captura (DXGI, GDI, etc.)
// del resto de la aplicacion.

#pragma once

#include "../../framework.h"

namespace antipop::capture {

// Frame capturado de la pantalla.
// Contiene los datos en bruto de la imagen BGRA y sus dimensiones.
struct CapturedFrame {
    std::vector<uint8_t> data;  // Pixels en formato BGRA
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t stride = 0;        // Bytes por fila (width * 4 normalmente)

    [[nodiscard]] bool IsValid() const noexcept {
        return !data.empty() && width > 0 && height > 0;
    }
};

// Interfaz de captura de pantalla.
// Implementaciones concretas encapsulan la API de captura especifica.
class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;

    // Inicializa el sistema de captura.
    // Devuelve true si la inicializacion fue exitosa.
    [[nodiscard]] virtual bool Initialize() = 0;

    // Captura un frame de la pantalla.
    // Devuelve nullopt si no hay frame nuevo disponible o si falla la captura.
    [[nodiscard]] virtual std::optional<CapturedFrame> CaptureFrame() = 0;

    // Libera los recursos de la captura.
    virtual void Shutdown() = 0;

    // Devuelve las dimensiones de la pantalla capturada.
    [[nodiscard]] virtual uint32_t GetWidth() const noexcept = 0;
    [[nodiscard]] virtual uint32_t GetHeight() const noexcept = 0;
};

} // namespace antipop::capture
