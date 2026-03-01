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

    // Posicion del monitor en el escritorio virtual (multi-monitor)
    int32_t originX = 0;
    int32_t originY = 0;

    [[nodiscard]] bool IsValid() const noexcept {
        return !data.empty() && width > 0 && height > 0;
    }
};

// Interfaz de captura de pantalla.
// Implementaciones concretas encapsulan la API de captura especifica.
class IScreenCapture {
public:
    virtual ~IScreenCapture() = default;

    // Inicializa el sistema de captura (todos los monitores).
    [[nodiscard]] virtual bool Initialize() = 0;

    // Captura un frame de cada monitor conectado.
    // Devuelve un vector con los frames disponibles (puede estar vacio si no hay frames nuevos).
    [[nodiscard]] virtual std::vector<CapturedFrame> CaptureAllFrames() = 0;

    // Libera los recursos de la captura.
    virtual void Shutdown() = 0;
};

} // namespace antipop::capture
