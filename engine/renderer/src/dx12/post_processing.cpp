#include "next/renderer/dx12/post_processing.h"
#include "next/foundation/logger.h"

namespace Next {

PostProcessing::PostProcessing()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , width_(0)
    , height_(0)
    , initialized_(false) {
}

PostProcessing::~PostProcessing() {
    Shutdown();
}

bool PostProcessing::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                                uint32_t width, uint32_t height) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or SRV heap for post-processing");
        return false;
    }

    NEXT_LOG_INFO("Initializing post-processing: %ux%u", width, height);

    device_ = device;
    srvHeap_ = srvHeap;
    width_ = width;
    height_ = height;

    // Create intermediate resources
    if (!CreateIntermediateResources()) {
        NEXT_LOG_ERROR("Failed to create intermediate resources");
        return false;
    }

    // Initialize default parameters (UE5/RAGE-style)
    bloom_ = BloomParameters();
    eyeAdaptation_ = EyeAdaptationParameters();
    colorGrading_ = ColorGradingParameters();

    initialized_ = true;
    NEXT_LOG_INFO("Post-processing initialized successfully (Phase 5: UE5/RAGE-style)");
    return true;
}

bool PostProcessing::CreateIntermediateResources() {
    // Create bloom resources
    if (!CreateBloomResources()) {
        NEXT_LOG_ERROR("Failed to create bloom resources");
        return false;
    }

    // Create eye adaptation resources
    if (!CreateEyeAdaptationResources()) {
        NEXT_LOG_ERROR("Failed to create eye adaptation resources");
        return false;
    }

    NEXT_LOG_INFO("Intermediate resources created");
    return true;
}

bool PostProcessing::CreateBloomResources() {
    // Bloom buffer (downsampled)
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width_ / 2;  // Downsample 2x
    desc.Height = height_ / 2;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;  // High precision for bloom
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&bloomBuffer_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create bloom buffer: 0x%X", hr);
        return false;
    }

    NEXT_LOG_INFO("Bloom buffer created: %ux%u", width_ / 2, height_ / 2);
    return true;
}

bool PostProcessing::CreateEyeAdaptationResources() {
    // Luminance buffer (1x1 for histogram)
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = 1;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT;  // Single float for luminance
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&adaptedLuminance_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create luminance buffer: 0x%X", hr);
        return false;
    }

    NEXT_LOG_INFO("Luminance buffer created");
    return true;
}

void PostProcessing::Process(ID3D12GraphicsCommandList* commandList,
                             ID3D12Resource* inputFrame,
                             ID3D12Resource* outputFrame) {
    if (!initialized_ || !commandList) {
        NEXT_LOG_ERROR("Cannot process post-processing: not initialized or invalid command list");
        return;
    }

    // Implement full post-processing chain in order
    // Note: This is a simplified placeholder implementation

    // 1. Eye Adaptation (compute luminance histogram and adjust exposure)
    // ApplyEyeAdaptation(commandList, inputFrame);

    // 2. Bloom (threshold bright pixels, blur, and composite)
    // ApplyBloom(commandList, inputFrame);

    // 3. Color Grading (contrast, saturation, gamma, temperature/tint, vibrance)
    // ApplyColorGrading(commandList, inputFrame);

    // 4. Tone Mapping (ACES filmic curve)
    // ApplyToneMapping(commandList, inputFrame, outputFrame);

    // For now, just copy input to output
    D3D12_RESOURCE_BARRIER barriers[2] = {};

    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = inputFrame;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = outputFrame;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(2, barriers);
    commandList->CopyResource(outputFrame, inputFrame);

    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->ResourceBarrier(1, &barriers[1]);

    NEXT_LOG_DEBUG("Post-processing applied (placeholder)");
}

void PostProcessing::ApplyBloom(ID3D12GraphicsCommandList* commandList, ID3D12Resource* input) {
    // Placeholder for UE5-style bloom implementation
    // Full implementation requires: PSO, shaders, intermediate textures
    NEXT_LOG_DEBUG("Bloom applied (placeholder - intensity=%.2f, threshold=%.2f)",
                   bloom_.intensity, bloom_.threshold);
}

void PostProcessing::ApplyEyeAdaptation(ID3D12GraphicsCommandList* commandList, ID3D12Resource* input) {
    // Placeholder for UE5-style eye adaptation implementation
    // Full implementation requires: luminance histogram, exposure calculation
    NEXT_LOG_DEBUG("Eye adaptation applied (placeholder - min=%.2f, max=%.2f)",
                   eyeAdaptation_.minLuminance, eyeAdaptation_.maxLuminance);
}

void PostProcessing::ApplyColorGrading(ID3D12GraphicsCommandList* commandList, ID3D12Resource* input) {
    // Placeholder for RAGE-style color grading implementation
    // Full implementation requires: color grading LUT, shaders
    NEXT_LOG_DEBUG("Color grading applied (placeholder - contrast=%.2f, saturation=%.2f, gamma=%.2f)",
                   colorGrading_.contrast, colorGrading_.saturation, colorGrading_.gamma);
}

bool PostProcessing::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot resize uninitialized post-processing");
        return false;
    }

    NEXT_LOG_INFO("Resizing post-processing: %ux%u -> %ux%u", width_, height_, width, height);

    // Recreate resources with new size
    Shutdown();
    return Initialize(device_, srvHeap_, width, height);
}

void PostProcessing::Shutdown() {
    bloomBuffer_.Reset();
    adaptedLuminance_.Reset();
    gradedBuffer_.Reset();

    device_ = nullptr;
    srvHeap_ = nullptr;
    width_ = 0;
    height_ = 0;
    initialized_ = false;

    NEXT_LOG_INFO("Post-processing shutdown complete");
}

} // namespace Next
