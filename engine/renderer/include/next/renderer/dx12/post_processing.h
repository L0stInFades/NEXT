#pragma once

#include "next/renderer/dx12/constant_buffer.h"
#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include "next/renderer/dx12/pipeline_state.h"
#include "next/renderer/dx12/root_signature.h"
#include "next/renderer/dx12/shader.h"
#include <d3d12.h>
#include <wrl/client.h>

namespace Next {

// Forward declarations
class DX12Device;
class DX12DescriptorHeap;

// Post-Processing Pipeline (UE5/RAGE Style)
// Design principles:
// - Sustainable Experimental: Easy to add new post effects
// - Advanced: Bloom, Eye Adaptation, Color Grading (UE5 quality)
// - Refactor Friendly: Modular effect chain
class PostProcessing {
public:
    PostProcessing();
    ~PostProcessing();

    // Initialize post-processing
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                    D3D12_CPU_DESCRIPTOR_HANDLE inputSrvCPU,
                    D3D12_GPU_DESCRIPTOR_HANDLE inputSrvGPU,
                    uint32_t width,
                    uint32_t height,
                    DXGI_FORMAT outputFormat = DXGI_FORMAT_R8G8B8A8_UNORM);

    // Apply post-processing chain
    void Process(ID3D12GraphicsCommandList* commandList,
                 ID3D12Resource* inputFrame,
                 ID3D12Resource* outputFrame,
                 D3D12_CPU_DESCRIPTOR_HANDLE outputRTV,
                 ID3D12Resource* globalIlluminationFrame = nullptr);

    // Resize
    bool Resize(uint32_t width, uint32_t height);

    // Bloom Parameters (UE5-style)
    struct BloomParameters {
        float intensity = 1.0f;          // Bloom intensity
        float threshold = 1.0f;          // Brightness threshold
        float softKnee = 0.5f;           // Soft knee transition
        float radius = 0.8f;             // Bloom radius
        uint32_t iterations = 5;         // Blur iterations
    };

    // Eye Adaptation Parameters (UE5-style)
    struct EyeAdaptationParameters {
        float minLuminance = 0.1f;       // Minimum exposure
        float maxLuminance = 10.0f;      // Maximum exposure
        float speedUp = 3.0f;            // Adaptation speed (bright)
        float speedDown = 1.0f;          // Adaptation speed (dark)
        float preExposure = 1.0f;        // Pre-exposure offset
        float exposureBias = 0.0f;       // Exposure bias
    };

    // Color Grading Parameters (RAGE-style)
    struct ColorGradingParameters {
        float contrast = 1.0f;           // Contrast
        float saturation = 1.0f;         // Saturation
        float gamma = 2.2f;              // Gamma
        float temperature = 0.0f;        // Color temperature
        float tint = 0.0f;               // Color tint
        float vibrance = 0.0f;           // Vibrance
    };

    // Set parameters
    void SetBloomParameters(const BloomParameters& params) { bloom_ = params; }
    void SetEyeAdaptationParameters(const EyeAdaptationParameters& params) { eyeAdaptation_ = params; }
    void SetColorGradingParameters(const ColorGradingParameters& params) { colorGrading_ = params; }

    // Get parameters
    const BloomParameters& GetBloomParameters() const { return bloom_; }
    const EyeAdaptationParameters& GetEyeAdaptationParameters() const { return eyeAdaptation_; }
    const ColorGradingParameters& GetColorGradingParameters() const { return colorGrading_; }

    // Cleanup
    void Shutdown();

    bool IsInitialized() const { return initialized_; }

private:
    // Create intermediate resources
    bool CreateIntermediateResources();
    bool CreateIntermediateDescriptors();
    bool CreatePipelineResources(DXGI_FORMAT outputFormat);
    bool UpdateInputShaderResources(ID3D12Resource* inputFrame,
                                    ID3D12Resource* globalIlluminationFrame,
                                    D3D12_CPU_DESCRIPTOR_HANDLE descriptorStart);
    bool UpdateConstants(DX12ConstantBuffer& constantsBuffer,
                         bool globalIlluminationAvailable,
                         float passMode);
    void TransitionIntermediateResource(ID3D12GraphicsCommandList* commandList,
                                        ID3D12Resource* resource,
                                        D3D12_RESOURCE_STATES& currentState,
                                        D3D12_RESOURCE_STATES targetState);
    void RenderFullscreenPass(ID3D12GraphicsCommandList* commandList,
                              DX12PipelineState& pipelineState,
                              D3D12_CPU_DESCRIPTOR_HANDLE outputRTV,
                              D3D12_GPU_DESCRIPTOR_HANDLE inputSrvTable,
                              D3D12_GPU_VIRTUAL_ADDRESS constantsAddress,
                              uint32_t passWidth,
                              uint32_t passHeight);

    // Bloom
    bool CreateBloomResources();

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE inputSrvCPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE inputSrvGPU_;
    D3D12_CPU_DESCRIPTOR_HANDLE globalIlluminationSrvCPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE globalIlluminationSrvGPU_;

    DX12RootSignature rootSignature_;
    DX12VertexShader vertexShader_;
    DX12PixelShader pixelShader_;
    DX12PipelineState pipelineState_;
    DX12PipelineState intermediatePipelineState_;
    DX12ConstantBuffer sceneConstantsBuffer_;
    DX12ConstantBuffer bloomConstantsBuffer_;
    DX12ConstantBuffer finalConstantsBuffer_;

    // Intermediate resources
    Microsoft::WRL::ComPtr<ID3D12Resource> bloomBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> gradedBuffer_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> intermediateRtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> postSrvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE bloomRTV_;
    D3D12_CPU_DESCRIPTOR_HANDLE gradedRTV_;
    D3D12_CPU_DESCRIPTOR_HANDLE sceneAndGiSrvCPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE sceneAndGiSrvGPU_;
    D3D12_CPU_DESCRIPTOR_HANDLE gradedAndNullSrvCPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE gradedAndNullSrvGPU_;
    D3D12_CPU_DESCRIPTOR_HANDLE gradedAndBloomSrvCPU_;
    D3D12_GPU_DESCRIPTOR_HANDLE gradedAndBloomSrvGPU_;
    UINT intermediateRtvDescriptorSize_;
    UINT postSrvDescriptorSize_;
    D3D12_RESOURCE_STATES bloomState_;
    D3D12_RESOURCE_STATES gradedState_;

    // Dimensions
    uint32_t width_;
    uint32_t height_;
    uint32_t bloomWidth_;
    uint32_t bloomHeight_;

    // Parameters
    BloomParameters bloom_;
    EyeAdaptationParameters eyeAdaptation_;
    ColorGradingParameters colorGrading_;
    float globalIlluminationIntensity_ = 1.0f;

    bool initialized_;
};

} // namespace Next
