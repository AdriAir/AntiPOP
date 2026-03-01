// DetectionTracker.h : Sistema de rastreo de detecciones entre frames
// con interpolacion suave para evitar saltos visuales y parpadeos.
//
// Problema resuelto:
// - Cuando un objeto se mueve rapidamente, la detección salta entre posiciones
// - Sin tracking, se ve desaparecer y reaparecer en nueva posición
//
// Solucion:
// - Rastrea objetos entre frames usando IoU (Intersection over Union)
// - Interpola suavemente la posición a lo largo de varios frames
// - Mantiene censura aunque objeto desaparezca brevemente

#pragma once

#include "Detection.h"
#include <deque>
#include <unordered_map>

namespace antipop::detector {

// Estado de un objeto rastreado
struct TrackedObject {
    BoundingBox box;           // Posición actual (interpolada)
    float confidence = 0.0f;   // Confianza de detección
    std::string className;

    int trackId = -1;          // ID único de seguimiento
    int framesWithoutDetection = 0;  // Frames consecutivos sin detectar este objeto

    // Historial de posiciones para interpolacion suave
    std::deque<BoundingBox> positionHistory;

    // Velocidad estimada para predicción
    float velocityX = 0.0f;
    float velocityY = 0.0f;
};

// Rastreador de detecciones entre frames
class DetectionTracker {
public:
    DetectionTracker() = default;
    ~DetectionTracker() = default;

    DetectionTracker(const DetectionTracker&) = delete;
    DetectionTracker& operator=(const DetectionTracker&) = delete;

    // Actualiza el seguimiento con nuevas detecciones del frame actual
    // Devuelve detecciones suavizadas y con seguimiento
    [[nodiscard]] std::vector<Detection> UpdateAndGetTrackedDetections(
        const std::vector<Detection>& currentDetections,
        int maxFramesWithoutDetection = 6  // Frames para mantener objeto sin detectar
    );

    // Limpia todo el tracking (ej: cuando se pausa la app)
    void Reset();

private:
    // Asocia detecciones actuales con objetos rastreados previos
    // Devuelve pares (indice_deteccion_actual, id_objeto_rastreado)
    [[nodiscard]] std::vector<std::pair<size_t, int>> AssociateDetections(
        const std::vector<Detection>& currentDetections
    );

    // Calcula IoU (Intersection over Union) entre dos bounding boxes
    [[nodiscard]] static float CalculateIoU(
        const BoundingBox& box1,
        const BoundingBox& box2
    );

    // Interpola suavemente la posicion entre frames
    [[nodiscard]] static BoundingBox InterpolateBox(
        const BoundingBox& old_box,
        const BoundingBox& new_box,
        float alpha = 0.3f  // 0.0 = completamente viejo, 1.0 = completamente nuevo
    );

    // Estima la velocidad de movimiento
    static void UpdateVelocity(TrackedObject& obj);

    // Predice la siguiente posicion basada en velocidad estimada
    // Usado cuando no hay nuevas detecciones pero queremos seguimiento continuo
    [[nodiscard]] static BoundingBox PredictNextPosition(const TrackedObject& obj);

    std::unordered_map<int, TrackedObject> m_trackedObjects;
    int m_nextTrackId = 0;

    static constexpr float kIoUThreshold = 0.3f;  // Umbral minimo de IoU para asociar
    static constexpr float kInterpolationAlpha = 0.25f;  // Suavidad de interpolacion
    static constexpr size_t kMaxPositionHistory = 5;  // Frames para historial de posiciones
    static constexpr float kVelocityPredictionFactor = 0.8f;  // Factor para prediccion (0.8 = 80% de la velocidad)
};

} // namespace antipop::detector
