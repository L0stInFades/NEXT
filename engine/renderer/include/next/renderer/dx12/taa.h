#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include "next/renderer/dx12/constant_buffer.h"
#include "next/renderer/dx12/pipeline_state.h"
#include "next/renderer/dx12/root_signature.h"
#include "next/renderer/dx12/shader.h"
#include "next/renderer/math/math.h"
#include <d3d12.h>
#include <wrl/client.h>

namespace Next {

// Forward declarations
class DX12Device;
class DX12DescriptorHeap;

// Temporal Anti-Aliasing (TAA) - UE5 Style
// Design principles:
// - Sustainable Experimental: Easy to tune TAA parameters
// - Advanced: History rectification, clamping, anti-ghosting (UE5 quality)
// - Refactor Friendly: Self-contained temporal effect
class TemporalAA {
public:
    TemporalAA();
    ~TemporalAA();

    // Initialize TAA
    bool Initialize(DX12Device* device,
                    DX12DescriptorHeap* srvHeap,
                    D3D12_CPU_DESCRIPTOR_HANDLE srvCPU,
                    D3D12_GPU_DESCRIPTOR_HANDLE srvGPU,
                    uint32_t width,
                    uint32_t height,
                    DXGI_FORMAT outputFormat = DXGI_FORMAT_R8G8B8A8_UNORM);

    // Update TAA history (call before rendering current frame)
    void UpdateHistory(ID3D12GraphicsCommandList* commandList,
                      ID3D12Resource* currentFrame,
                      ID3D12Resource* motionVectors);

    // Apply TAA (call after rendering current frame)
    void Resolve(ID3D12GraphicsCommandList* commandList,
                 ID3D12Resource* currentFrame,
                 ID3D12Resource* outputFrame,
                 D3D12_CPU_DESCRIPTOR_HANDLE outputRTV,
                 ID3D12Resource* motionVectors = nullptr);

    // Resize (for window resize)
    bool Resize(uint32_t width, uint32_t height);

    // TAA Parameters (UE5-style tuning)
    struct TAAParameters {
        float blendFactor = 0.9f;          // History weight (0.0-1.0)
        float sharpening = 0.0f;           // Sharpening amount
        float antiGhosting = 0.5f;         // Anti-ghosting strength
        float velocityScale = 1.0f;        // Motion vector scale
        float rectificationBias = 0.01f;   // History rectification
        float padding[2] = {0.0f, 0.0f};
    };

    void SetParameters(const TAAParameters& params) { params_ = params; }
    const TAAParameters& GetParameters() const { return params_; }

    // Jittered projection matrix (for TAA sample distribution)
    Mat4 GetJitteredProjectionMatrix(const Mat4& projection, float jitterX, float jitterY);

    // Generate Halton sequence for jitter (UE5-style)
    void GetHaltonSequence(uint32_t frameIndex, float& outX, float& outY);

    // Cleanup
    void Shutdown();

    bool IsInitialized() const { return initialized_; }

private:
    // Create resources
    bool CreateHistoryResources();
    bool CreateVelocityBuffer();
    bool CreatePipelineResources(DXGI_FORMAT outputFormat);
    bool UpdateShaderResources(ID3D12Resource* currentFrame, ID3D12Resource* motionVectors);
    bool UpdateConstants(bool historyValid, bool velocityAvailable);
    void CopyResolvedFrameToHistory(ID3D12GraphicsCommandList* commandList,
                                    ID3D12Resource* outputFrame);

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGPU_;

    DX12RootSignature rootSignature_;
    DX12VertexShader vertexShader_;
    DX12PixelShader pixelShader_;
    DX12PipelineState pipelineState_;
    DX12ConstantBuffer constantsBuffer_;

    // History buffers (double-buffered)
    Microsoft::WRL::ComPtr<ID3D12Resource> historyBuffer_[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> velocityBuffer_;

    // SRV/UAV descriptors
    D3D12_GPU_DESCRIPTOR_HANDLE historySRV_[2];
    D3D12_GPU_DESCRIPTOR_HANDLE historyUAV_[2];
    D3D12_GPU_DESCRIPTOR_HANDLE velocitySRV_;
    D3D12_GPU_DESCRIPTOR_HANDLE velocityUAV_;

    // Dimensions
    uint32_t width_;
    uint32_t height_;
    DXGI_FORMAT outputFormat_;

    // Current history index
    uint32_t historyIndex_;
    bool historyValid_;

    // Parameters
    TAAParameters params_;

    bool initialized_;
};

} // namespace Next
