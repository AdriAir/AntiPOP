// GpuPreprocess.h : Preprocesado de imagen en GPU usando CUDA.
//
// Reemplaza el bucle CPU de OnnxDetector::Preprocess() con un kernel CUDA
// que ejecuta resize + BGRA->RGB + normalización en ~0.1ms (vs ~3-5ms en CPU).
//
// Flujo zero-copy:
//   ID3D11Texture2D (DXGI) -> cudaGraphicsD3D11RegisterResource -> cudaArray
//   -> CUDA kernel (resize+normalize) -> float* en GPU memory
//   -> Ort::Value::CreateTensor con "Cuda" MemoryInfo -> Inferencia GPU
//
// REQUIERE: CUDA Toolkit + Microsoft.ML.OnnxRuntime.Gpu NuGet
// Si ANTIPOP_USE_CUDA no esta definido, se usa el path CPU como fallback.

#pragma once

#ifdef ANTIPOP_USE_CUDA

#include <cstdint>
#include <d3d11.h>

namespace antipop {
namespace detector {

class GpuPreprocess {
public:
    GpuPreprocess();
    ~GpuPreprocess();

    GpuPreprocess(const GpuPreprocess&) = delete;
    GpuPreprocess& operator=(const GpuPreprocess&) = delete;

    // Inicializa buffers CUDA y registra la textura D3D11 para interop.
    // Llamar una vez despues de crear la textura de captura.
    // sharedTexture: textura GPU D3D11 compartida con la captura DXGI
    // srcWidth/Height: dimensiones de la textura fuente
    // dstWidth/Height: dimensiones del tensor de salida (640x640)
    [[nodiscard]] bool Initialize(
        ID3D11Texture2D* sharedTexture,
        uint32_t srcWidth, uint32_t srcHeight,
        uint32_t dstWidth = 640, uint32_t dstHeight = 640
    );

    // Ejecuta el preprocesado en GPU:
    // 1. Mapea la textura D3D11 como cudaArray
    // 2. Lanza kernel CUDA: resize + BGRA->RGB + normalizar a [0,1]
    // 3. Escribe resultado en tensor GPU pre-alocado
    // Retorna puntero al tensor float* en GPU memory (propiedad de esta clase)
    [[nodiscard]] float* Process();

    // Devuelve el puntero al tensor de salida en GPU memory.
    // Valido despues de llamar a Process().
    [[nodiscard]] float* GetOutputTensor() const { return m_d_outputTensor; }

    // Devuelve el tamaño del tensor de salida en floats
    [[nodiscard]] size_t GetOutputTensorSize() const {
        return 3ULL * m_dstWidth * m_dstHeight;
    }

    // Libera todos los recursos CUDA
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

private:
    // Recursos CUDA
    void* m_cudaResource = nullptr;  // cudaGraphicsResource_t (opaco para evitar header CUDA)
    void* m_cudaStream = nullptr;    // cudaStream_t
    float* m_d_outputTensor = nullptr;  // Tensor de salida en GPU [3, dstH, dstW]

    uint32_t m_srcWidth = 0, m_srcHeight = 0;
    uint32_t m_dstWidth = 0, m_dstHeight = 0;

    bool m_initialized = false;
};

} // namespace detector
} // namespace antipop

#endif // ANTIPOP_USE_CUDA
