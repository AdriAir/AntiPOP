// GpuPreprocess.cu : Implementacion CUDA del preprocesado de imagen para YOLOv8.
//
// Kernel que lee una textura DXGI (BGRA uint8) y produce un tensor float CHW RGB
// normalizado [0,1] a 640x640 listo para inferencia ONNX.
//
// Rendimiento tipico: ~0.05-0.1ms en cualquier GPU RTX (vs ~3-5ms en CPU)
//
// COMPILACION:
//   Este archivo requiere nvcc (CUDA Toolkit). Agregar como item CUDA al proyecto
//   de Visual Studio o usar un .vcxproj separado para compilar a .obj/.lib.
//
//   nvcc -c GpuPreprocess.cu -o GpuPreprocess.obj -I"path/to/d3d11" --std=c++17
//
// REQUIERE: CUDA Toolkit 11.0+ (sm_75 para RTX 20xx, sm_86 para 30xx, etc.)

#ifdef ANTIPOP_USE_CUDA

#include "GpuPreprocess.h"

#include <cuda_runtime.h>
#include <cuda_d3d11_interop.h>

#include <cstdio>

namespace antipop::detector {

// ============================================================================
// CUDA Kernel: resize BGRA -> RGB CHW normalizado
// ============================================================================
// Cada thread procesa un pixel del tensor de salida.
// Nearest-neighbor resampling con lectura desde cudaArray (texture memory).
__global__ void PreprocessKernel(
    cudaTextureObject_t texObj,
    float* __restrict__ output,
    uint32_t srcW, uint32_t srcH,
    uint32_t dstW, uint32_t dstH
) {
    const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= dstW || y >= dstH) return;

    // Coordenadas en la imagen fuente (nearest-neighbor)
    const float srcX = static_cast<float>(x) * srcW / dstW;
    const float srcY = static_cast<float>(y) * srcH / dstH;

    // tex2D devuelve float4 normalizado [0,1] automaticamente
    // cuando el cudaArray es de tipo uint8 y readMode es cudaReadModeNormalizedFloat
    const float4 pixel = tex2D<float4>(texObj, srcX + 0.5f, srcY + 0.5f);

    // BGRA format en DXGI: pixel.x=B, pixel.y=G, pixel.z=R, pixel.w=A
    const uint32_t planeSize = dstW * dstH;
    const uint32_t idx = y * dstW + x;

    output[0 * planeSize + idx] = pixel.z;  // R
    output[1 * planeSize + idx] = pixel.y;  // G
    output[2 * planeSize + idx] = pixel.x;  // B
}

// ============================================================================
// GpuPreprocess implementation
// ============================================================================

GpuPreprocess::GpuPreprocess() = default;

GpuPreprocess::~GpuPreprocess() {
    Shutdown();
}

bool GpuPreprocess::Initialize(
    ID3D11Texture2D* sharedTexture,
    uint32_t srcWidth, uint32_t srcHeight,
    uint32_t dstWidth, uint32_t dstHeight
) {
    if (m_initialized) return true;
    if (!sharedTexture) return false;

    m_srcWidth  = srcWidth;
    m_srcHeight = srcHeight;
    m_dstWidth  = dstWidth;
    m_dstHeight = dstHeight;

    cudaError_t err;

    // 1. Crear CUDA stream
    cudaStream_t stream;
    err = cudaStreamCreate(&stream);
    if (err != cudaSuccess) {
        fprintf(stderr, "[GpuPreprocess] cudaStreamCreate fallo: %s\n",
                cudaGetErrorString(err));
        return false;
    }
    m_cudaStream = stream;

    // 2. Registrar textura D3D11 con CUDA para interop
    cudaGraphicsResource_t resource;
    err = cudaGraphicsD3D11RegisterResource(
        &resource,
        sharedTexture,
        cudaGraphicsRegisterFlagsNone
    );
    if (err != cudaSuccess) {
        fprintf(stderr, "[GpuPreprocess] cudaGraphicsD3D11RegisterResource fallo: %s\n",
                cudaGetErrorString(err));
        cudaStreamDestroy(stream);
        m_cudaStream = nullptr;
        return false;
    }
    m_cudaResource = resource;

    // 3. Pre-alocar tensor de salida en GPU memory [3, dstH, dstW]
    const size_t tensorSize = 3ULL * dstHeight * dstWidth * sizeof(float);
    err = cudaMalloc(&m_d_outputTensor, tensorSize);
    if (err != cudaSuccess) {
        fprintf(stderr, "[GpuPreprocess] cudaMalloc fallo: %s\n",
                cudaGetErrorString(err));
        cudaGraphicsUnregisterResource(resource);
        cudaStreamDestroy(stream);
        m_cudaResource = nullptr;
        m_cudaStream = nullptr;
        return false;
    }

    m_initialized = true;
    return true;
}

float* GpuPreprocess::Process() {
    if (!m_initialized) return nullptr;

    auto stream = static_cast<cudaStream_t>(m_cudaStream);
    auto resource = static_cast<cudaGraphicsResource_t>(m_cudaResource);

    // 1. Mapear textura D3D11 como cudaArray
    cudaError_t err = cudaGraphicsMapResources(1, &resource, stream);
    if (err != cudaSuccess) return nullptr;

    cudaArray_t cuArray;
    err = cudaGraphicsSubResourceGetMappedArray(&cuArray, resource, 0, 0);
    if (err != cudaSuccess) {
        cudaGraphicsUnmapResources(1, &resource, stream);
        return nullptr;
    }

    // 2. Crear texture object para lectura eficiente
    cudaResourceDesc resDesc = {};
    resDesc.resType = cudaResourceTypeArray;
    resDesc.res.array.array = cuArray;

    cudaTextureDesc texDesc = {};
    texDesc.addressMode[0] = cudaAddressModeClamp;
    texDesc.addressMode[1] = cudaAddressModeClamp;
    texDesc.filterMode = cudaFilterModePoint;  // Nearest-neighbor
    texDesc.readMode = cudaReadModeNormalizedFloat;  // uint8 -> [0,1]
    texDesc.normalizedCoords = 0;  // Coordenadas en pixels

    cudaTextureObject_t texObj;
    err = cudaCreateTextureObject(&texObj, &resDesc, &texDesc, nullptr);
    if (err != cudaSuccess) {
        cudaGraphicsUnmapResources(1, &resource, stream);
        return nullptr;
    }

    // 3. Lanzar kernel: ceil(640/16) x ceil(640/16) = 40x40 thread blocks
    dim3 block(16, 16);
    dim3 grid(
        (m_dstWidth  + block.x - 1) / block.x,
        (m_dstHeight + block.y - 1) / block.y
    );

    PreprocessKernel<<<grid, block, 0, stream>>>(
        texObj, m_d_outputTensor,
        m_srcWidth, m_srcHeight,
        m_dstWidth, m_dstHeight
    );

    // 4. Cleanup
    cudaDestroyTextureObject(texObj);
    cudaGraphicsUnmapResources(1, &resource, stream);

    // Sincronizar stream para asegurar que el kernel termino
    cudaStreamSynchronize(stream);

    return m_d_outputTensor;
}

void GpuPreprocess::Shutdown() {
    if (m_d_outputTensor) {
        cudaFree(m_d_outputTensor);
        m_d_outputTensor = nullptr;
    }
    if (m_cudaResource) {
        cudaGraphicsUnregisterResource(
            static_cast<cudaGraphicsResource_t>(m_cudaResource));
        m_cudaResource = nullptr;
    }
    if (m_cudaStream) {
        cudaStreamDestroy(static_cast<cudaStream_t>(m_cudaStream));
        m_cudaStream = nullptr;
    }
    m_initialized = false;
}

} // namespace antipop::detector

#endif // ANTIPOP_USE_CUDA
