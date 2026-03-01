// DetectionTracker.cpp : Implementacion del rastreador de detecciones

#include "DetectionTracker.h"
#include "../utils/Logger.h"
#include <algorithm>
#include <cmath>

namespace antipop::detector {

std::vector<Detection> DetectionTracker::UpdateAndGetTrackedDetections(
    const std::vector<Detection>& currentDetections,
    int maxFramesWithoutDetection
) {
    // Paso 1: Asociar detecciones actuales con objetos rastreados previos
    auto associations = AssociateDetections(currentDetections);

    // Paso 2: Actualizar objetos rastreados existentes
    std::vector<int> updatedTrackIds;
    for (const auto& [detIdx, trackId] : associations) {
        const auto& det = currentDetections[detIdx];
        auto& trackedObj = m_trackedObjects[trackId];

        // Actualizar con nueva deteccion
        const auto smoothedBox = InterpolateBox(trackedObj.box, det.box, kInterpolationAlpha);
        trackedObj.box = smoothedBox;
        trackedObj.confidence = det.confidence;
        trackedObj.className = det.className;
        trackedObj.framesWithoutDetection = 0;

        // Mantener historial de posiciones para suavizado futuro
        trackedObj.positionHistory.push_back(det.box);
        if (trackedObj.positionHistory.size() > kMaxPositionHistory) {
            trackedObj.positionHistory.pop_front();
        }

        UpdateVelocity(trackedObj);
        updatedTrackIds.push_back(trackId);
    }

    // Paso 3: Manejar detecciones no asociadas (nuevos objetos)
    for (size_t i = 0; i < currentDetections.size(); ++i) {
        if (std::find_if(associations.begin(), associations.end(),
                        [i](const auto& p) { return p.first == i; }) == associations.end()) {
            // Nueva deteccion sin asociacion previa
            int newTrackId = m_nextTrackId++;
            TrackedObject newObj;
            newObj.trackId = newTrackId;
            newObj.box = currentDetections[i].box;
            newObj.confidence = currentDetections[i].confidence;
            newObj.className = currentDetections[i].className;
            newObj.framesWithoutDetection = 0;
            newObj.positionHistory.push_back(currentDetections[i].box);

            m_trackedObjects[newTrackId] = std::move(newObj);
            updatedTrackIds.push_back(newTrackId);
        }
    }

    // Paso 4: Actualizar objetos que no fueron detectados en este frame
    // Usar predicción de velocidad para mantener seguimiento continuo
    std::vector<int> trackIdsToRemove;
    for (auto& [trackId, trackedObj] : m_trackedObjects) {
        if (std::find(updatedTrackIds.begin(), updatedTrackIds.end(), trackId) == updatedTrackIds.end()) {
            trackedObj.framesWithoutDetection++;

            // Predicción de movimiento: usar velocidad para seguir el objeto
            // incluso sin detectar nuevamente en este frame
            if (trackedObj.framesWithoutDetection <= 3) {  // Solo los primeros 3 frames sin deteccion
                trackedObj.box = PredictNextPosition(trackedObj);
            }

            // Si lleva demasiados frames sin deteccion, remover del tracking
            if (trackedObj.framesWithoutDetection > maxFramesWithoutDetection) {
                trackIdsToRemove.push_back(trackId);
            }
        }
    }

    // Remover objetos que desaparecieron
    for (int trackId : trackIdsToRemove) {
        m_trackedObjects.erase(trackId);
    }

    // Paso 5: Construir vector de detecciones rastreadas para output
    std::vector<Detection> result;
    for (const auto& [trackId, trackedObj] : m_trackedObjects) {
        Detection det;
        det.box = trackedObj.box;
        det.confidence = trackedObj.confidence;
        det.className = trackedObj.className;
        result.push_back(std::move(det));
    }

    return result;
}

void DetectionTracker::Reset() {
    m_trackedObjects.clear();
    m_nextTrackId = 0;
    LOG_INFO("Detection tracker reseteado");
}

std::vector<std::pair<size_t, int>> DetectionTracker::AssociateDetections(
    const std::vector<Detection>& currentDetections
) {
    std::vector<std::pair<size_t, int>> associations;

    // Para cada deteccion actual, encontrar el objeto rastreado mas cercano
    for (size_t i = 0; i < currentDetections.size(); ++i) {
        const auto& det = currentDetections[i];

        float bestIoU = kIoUThreshold;
        int bestTrackId = -1;

        // Buscar en objetos rastreados
        for (auto& [trackId, trackedObj] : m_trackedObjects) {
            float iou = CalculateIoU(trackedObj.box, det.box);
            if (iou > bestIoU) {
                bestIoU = iou;
                bestTrackId = trackId;
            }
        }

        if (bestTrackId != -1) {
            associations.push_back({ i, bestTrackId });
        }
    }

    return associations;
}

float DetectionTracker::CalculateIoU(const BoundingBox& box1, const BoundingBox& box2) {
    // Calcular interseccion
    float x1_left   = box1.x;
    float y1_top    = box1.y;
    float x1_right  = box1.x + box1.width;
    float y1_bottom = box1.y + box1.height;

    float x2_left   = box2.x;
    float y2_top    = box2.y;
    float x2_right  = box2.x + box2.width;
    float y2_bottom = box2.y + box2.height;

    float inter_left   = std::max(x1_left, x2_left);
    float inter_top    = std::max(y1_top, y2_top);
    float inter_right  = std::min(x1_right, x2_right);
    float inter_bottom = std::min(y1_bottom, y2_bottom);

    float intersection = 0.0f;
    if (inter_right > inter_left && inter_bottom > inter_top) {
        intersection = (inter_right - inter_left) * (inter_bottom - inter_top);
    }

    // Calcular union
    float area1 = box1.width * box1.height;
    float area2 = box2.width * box2.height;
    float unionArea = area1 + area2 - intersection;

    if (unionArea < 0.0001f) {
        return 0.0f;
    }

    return intersection / unionArea;
}

BoundingBox DetectionTracker::InterpolateBox(
    const BoundingBox& old_box,
    const BoundingBox& new_box,
    float alpha
) {
    // Interpolacion lineal suave entre posiciones antiguas y nuevas
    // alpha = 0.0 -> completamente vieja
    // alpha = 1.0 -> completamente nueva
    // alpha = 0.25 -> 25% nueva, 75% vieja (mas suave)

    BoundingBox result;
    result.x      = old_box.x      + (new_box.x      - old_box.x)      * alpha;
    result.y      = old_box.y      + (new_box.y      - old_box.y)      * alpha;
    result.width  = old_box.width  + (new_box.width  - old_box.width)  * alpha;
    result.height = old_box.height + (new_box.height - old_box.height) * alpha;

    return result;
}

void DetectionTracker::UpdateVelocity(TrackedObject& obj) {
    // Estimar velocidad basada en dos posiciones recientes
    if (obj.positionHistory.size() < 2) {
        return;
    }

    const auto& recent = obj.positionHistory.back();
    const auto& previous = obj.positionHistory[obj.positionHistory.size() - 2];

    // Calcular velocidad como cambio de centro entre frames
    float recentCenterX = recent.x + recent.width * 0.5f;
    float recentCenterY = recent.y + recent.height * 0.5f;
    float previousCenterX = previous.x + previous.width * 0.5f;
    float previousCenterY = previous.y + previous.height * 0.5f;

    obj.velocityX = recentCenterX - previousCenterX;
    obj.velocityY = recentCenterY - previousCenterY;
}

BoundingBox DetectionTracker::PredictNextPosition(const TrackedObject& obj) {
    // Predice la siguiente posicion basada en la velocidad estimada
    // Usa el factor de predicción para reducir overshooting
    BoundingBox predicted = obj.box;
    predicted.x += obj.velocityX * kVelocityPredictionFactor;
    predicted.y += obj.velocityY * kVelocityPredictionFactor;
    return predicted;
}

} // namespace antipop::detector
