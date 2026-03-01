// IOverlay.h : Interfaz abstracta para el overlay de censura.
// El overlay dibuja rectangulos opacos sobre las areas detectadas
// como contenido de pulpos, cubriendo la pantalla real del usuario.

#pragma once

#include "../../framework.h"
#include "../detector/Detection.h"

namespace antipop::overlay {

class IOverlay {
public:
    virtual ~IOverlay() = default;

    // Crea la ventana del overlay (transparente, topmost, click-through).
    [[nodiscard]] virtual bool Initialize(HINSTANCE hInstance) = 0;

    // Actualiza las areas de censura que se deben dibujar.
    virtual void UpdateCensorRegions(const std::vector<detector::Detection>& detections) = 0;

    // Limpia todas las areas de censura (pantalla libre).
    virtual void ClearCensorRegions() = 0;

    // Muestra u oculta el overlay.
    virtual void SetVisible(bool visible) = 0;

    // Libera los recursos del overlay.
    virtual void Shutdown() = 0;
};

} // namespace antipop::overlay
