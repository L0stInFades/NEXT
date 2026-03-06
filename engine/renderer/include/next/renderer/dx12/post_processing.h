#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_heap.h"
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
                    uint32_t width, uint32_t height);

    // Apply post-processing chain
    void Process(ID3D12GraphicsCommandList* commandList,
                 ID3D12Resource* inputFrame,
                 ID3D12Resource* outputFrame);

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

    // Bloom
    bool CreateBloomResources();
    void ApplyBloom(ID3D12GraphicsCommandList* commandList, ID3D12Resource* input);

    // Eye Adaptation
    bool CreateEyeAdaptationResources();
    void ApplyEyeAdaptation(ID3D12GraphicsCommandList* commandList, ID3D12Resource* input);

    // Color Grading
    void ApplyColorGrading(ID3D12GraphicsCommandList* commandList, ID3D12Resource* input);

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // Intermediate resources
    Microsoft::WRL::ComPtr<ID3D12Resource> bloomBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> adaptedLuminance_;
    Microsoft::WRL::ComPtr<ID3D12Resource> gradedBuffer_;

    // Dimensions
    uint32_t width_;
    uint32_t height_;

    // Parameters
    BloomParameters bloom_;
    EyeAdaptationParameters eyeAdaptation_;
    ColorGradingParameters colorGrading_;

    bool initialized_;
};

} // namespace Next
