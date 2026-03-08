// PipelineState.h : Estado compartido entre los 3 threads del pipeline.
//
// Arquitectura de 3 threads:
//   Thread 1 (Captura):    Captura DXGI, escribe frames en buffer CPU
//   Thread 2 (Inferencia): Lee frames, ejecuta ONNX + tracking, publica detecciones
//   Thread 3 (Overlay):    Lee detecciones, repinta a 60 FPS fijo
//
// Comunicacion lock-free:
//   - Captura -> Inferencia: atomic frameId + shared frame buffer
//   - Inferencia -> Overlay: double buffer con atomic slot index
//
// El overlay corre a 60 FPS constante independiente del ritmo de inferencia.
// Si la inferencia tarda 50ms, el overlay simplemente repinta las ultimas
// detecciones conocidas. Frame skipping es implicito.

#pragma once

#include "../../framework.h"
#include "../detector/Detection.h"
#include "../capture/IScreenCapture.h"

namespace antipop::pipeline {

// Resultado de deteccion con timestamp para el double buffer
struct DetectionSlot {
    std::vector<detector::Detection> detections;
    uint64_t frameId = 0;
    std::chrono::steady_clock::time_point timestamp;
};

// Metricas de rendimiento por etapa
struct PipelineMetrics {
    double captureMs    = 0.0;
    double preprocessMs = 0.0;
    double inferenceMs  = 0.0;
    double postprocMs   = 0.0;
    double overlayMs    = 0.0;
    double totalMs      = 0.0;
    double fps          = 0.0;      // FPS de inferencia
    double overlayFps   = 0.0;      // FPS real del hilo overlay
    uint64_t framesSkipped = 0;
    int detectionCount  = 0;        // Detecciones en el ultimo frame
    bool usingGpu       = false;    // True si CUDA EP esta activo
};

// Estado compartido entre los 3 threads del pipeline.
// Disenado para minima contention: 2 atomics, 0 mutexes en hot path.
struct PipelineState {
    // ---- Control de vida ----
    std::atomic<bool> running{ false };

    // ---- Captura -> Inferencia ----
    // El thread de captura escribe frames aqui y incrementa frameId.
    // El thread de inferencia lee cuando frameId > lastProcessedId.
    // Mutex necesario porque CapturedFrame contiene std::vector.
    std::mutex frameMutex;
    std::vector<capture::CapturedFrame> latestFrames;
    std::atomic<uint64_t> captureFrameId{ 0 };

    // ---- Inferencia -> Overlay ----
    // Double buffer: inferencia escribe en slot inactivo, luego swap atomico.
    // Overlay lee del slot activo sin lock.
    DetectionSlot slots[2];
    std::atomic<int> activeSlot{ 0 };  // Overlay lee este slot

    // ---- Metricas (actualizadas periodicamente, lectura informativa) ----
    PipelineMetrics metrics;
    std::atomic<bool> metricsUpdated{ false };
};

// Timer de alta precision para profiling de cada etapa del pipeline
class PerfTimer {
public:
    PerfTimer() {
        QueryPerformanceFrequency(&m_freq);
        Reset();
    }

    void Reset() {
        QueryPerformanceCounter(&m_start);
    }

    [[nodiscard]] double ElapsedMs() const {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - m_start.QuadPart) * 1000.0
               / static_cast<double>(m_freq.QuadPart);
    }

private:
    LARGE_INTEGER m_freq{};
    LARGE_INTEGER m_start{};
};

} // namespace antipop::pipeline
