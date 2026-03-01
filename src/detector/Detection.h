// Detection.h : Estructuras de datos para representar detecciones de objetos.
// Separadas de la interfaz del detector para que puedan usarse
// en cualquier modulo sin depender de la implementacion de IA.

#pragma once

#include "../../framework.h"

namespace antipop::detector {

// Rectangulo de deteccion en coordenadas de pantalla.
struct BoundingBox {
    float x      = 0.0f;   // Esquina superior izquierda X
    float y      = 0.0f;   // Esquina superior izquierda Y
    float width  = 0.0f;
    float height = 0.0f;

    // Expande el bounding box un porcentaje en cada direccion (para margen de censura).
    [[nodiscard]] BoundingBox Expanded(float factor) const noexcept {
        const float dw = width  * factor * 0.5f;
        const float dh = height * factor * 0.5f;
        return { x - dw, y - dh, width + dw * 2, height + dh * 2 };
    }

    // Convierte a RECT de Win32.
    [[nodiscard]] RECT ToRECT() const noexcept {
        return {
            static_cast<LONG>(x),
            static_cast<LONG>(y),
            static_cast<LONG>(x + width),
            static_cast<LONG>(y + height)
        };
    }
};

// Resultado de una deteccion individual.
struct Detection {
    BoundingBox box;
    float       confidence = 0.0f;  // Probabilidad [0.0, 1.0]
    int         classId    = -1;    // ID de clase en el modelo
    std::string className;          // Nombre legible (e.g., "octopus")
};

// Categorias de contenido a censurar.
// Extensible para futuras ampliaciones.
enum class CensorCategory {
    Octopus,        // Pulpos
    Tentacles,      // Tentaculos (si se detectan por separado)
    // Futuras categorias:
    // Spider,
    // Snake,
};

} // namespace antipop::detector
