#include "next/renderer/dx12/taa.h"
#include "next/foundation/logger.h"
#include <cmath>

namespace Next {

TemporalAA::TemporalAA()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , width_(0)
    , height_(0)
    , historyIndex_(0)
    , initialized_(false) {
}

TemporalAA::~TemporalAA() {
    Shutdown();
}

bool TemporalAA::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                            uint32_t width, uint32_t height) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or SRV heap for TAA");
        return false;
    }

    NEXT_LOG_INFO("Initializing TAA: %ux%u", width, height);

    device_ = device;
    srvHeap_ = srvHeap;
    width_ = width;
    height_ = height;

    // Create history resources
    if (!CreateHistoryResources()) {
        NEXT_LOG_ERROR("Failed to create history resources");
        return false;
    }

    // Create velocity buffer
    if (!CreateVelocityBuffer()) {
        NEXT_LOG_ERROR("Failed to create velocity buffer");
        return false;
    }

    // Initialize default parameters (UE5-style)
    params_ = TAAParameters();

    initialized_ = true;
    NEXT_LOG_INFO("TAA initialized successfully (Phase 5: UE5-style TAA)");
    return true;
}

bool TemporalAA::CreateHistoryResources() {
    // Create two history buffers (double-buffered)
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;  // High precision for history
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

    // Create two history buffers
    for (int i = 0; i < 2; i++) {
        HRESULT hr = device_->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&historyBuffer_[i])
        );

        if (FAILED(hr)) {
            NEXT_LOG_ERROR("Failed to create history buffer %d: 0x%X", i, hr);
            return false;
        }

        // SRV/UAV descriptors should be allocated via descriptor heap
        // For now, we skip descriptor creation as it requires proper heap management
        // In full implementation: use srvHeap_->Allocate() to get descriptor handles
    }

    NEXT_LOG_INFO("History buffers created: 2x %ux%u", width_, height_);
    return true;
}

bool TemporalAA::CreateVelocityBuffer() {
    // Velocity buffer stores motion vectors (2D)
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16_FLOAT;  // Motion vectors (2D)
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
        IID_PPV_ARGS(&velocityBuffer_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create velocity buffer: 0x%X", hr);
        return false;
    }

    NEXT_LOG_INFO("Velocity buffer created: %ux%u", width_, height_);
    return true;
}

void TemporalAA::UpdateHistory(ID3D12GraphicsCommandList* commandList,
                               ID3D12Resource* currentFrame,
                               ID3D12Resource* motionVectors) {
    if (!initialized_ || !commandList) {
        NEXT_LOG_ERROR("Cannot update history: not initialized or invalid command list");
        return;
    }

    // Implement history update with current frame
    // 1. Resolve current frame into history buffer
    // 2. Apply history rectification to handle disocclusions
    // 3. Store motion vectors for velocity-aware sampling

    // Transition resources for copy operation
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = currentFrame;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = historyBuffer_[historyIndex_].Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(2, barriers);

    // Copy current frame to history buffer
    commandList->CopyResource(historyBuffer_[historyIndex_].Get(), currentFrame);

    // Transition back to shader resource
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = historyBuffer_[historyIndex_].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // Store motion vectors if provided
    if (motionVectors && velocityBuffer_) {
        D3D12_RESOURCE_BARRIER velBarrier = {};
        velBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        velBarrier.Transition.pResource = velocityBuffer_.Get();
        velBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        velBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        velBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &velBarrier);
        commandList->CopyResource(velocityBuffer_.Get(), motionVectors);
        velBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        velBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        commandList->ResourceBarrier(1, &velBarrier);
    }

    NEXT_LOG_DEBUG("History %u updated", historyIndex_);
}

void TemporalAA::Resolve(ID3D12GraphicsCommandList* commandList,
                         ID3D12Resource* currentFrame,
                         ID3D12Resource* outputFrame) {
    if (!initialized_ || !commandList) {
        NEXT_LOG_ERROR("Cannot resolve TAA: not initialized or invalid command list");
        return;
    }

    // Implement TAA resolve with proper temporal filtering
    // 1. Sample current frame and history using motion vectors for reprojection
    // 2. Apply clamping and anti-ghosting to prevent streaking
    // 3. Blend based on blendFactor (typically 0.9 for 90% history, 10% current)
    // 4. Apply sharpening to counteract blur from temporal blending

    // Note: In a full implementation, you would:
    // - Set pipeline state with TAA compute shader
    // - Set root parameters (history SRV, current SRV, velocity SRV, output UAV)
    // - Dispatch compute shader
    // For now, this is a placeholder showing the proper resource barriers

    // Transition output frame for UAV access
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = outputFrame;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // TODO: Set pipeline state and dispatch TAA compute shader
    // commandList->SetPipelineState(taaPSO_.Get());
    // commandList->SetComputeRootSignature(taaRootSignature_.Get());
    // commandList->SetComputeRootDescriptorTable(0, historySRV_[historyIndex_]);
    // ... set other parameters ...
    // commandList->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);

    // Transition output back to render target
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->ResourceBarrier(1, &barrier);

    NEXT_LOG_DEBUG("TAA resolved (blend=%.2f)", params_.blendFactor);

    // Swap history buffers
    historyIndex_ = (historyIndex_ + 1) % 2;
}

Mat4 TemporalAA::GetJitteredProjectionMatrix(const Mat4& projection, float jitterX, float jitterY) {
    // Apply sub-pixel jitter to projection matrix (UE5-style)
    // This distributes samples across pixels for better temporal integration

    Mat4 jittered = projection;

    // Jitter is applied in projection matrix
    // Jitter range: [-0.5, 0.5] pixels in normalized device coordinates
    float jitterX_NDC = jitterX / width_ * 2.0f;
    float jitterY_NDC = jitterY / height_ * 2.0f;

    // Modify projection matrix elements (translation)
    // Note: Mat4 implementation may vary - adjust indices as needed
    // jittered.data[2][0] += jitterX_NDC;
    // jittered.data[2][1] += jitterY_NDC;

    return jittered;
}

void TemporalAA::GetHaltonSequence(uint32_t frameIndex, float& outX, float& outY) {
    // Generate Halton sequence (2,3) for jitter (UE5-style)
    // This provides better distribution than random jitter

    // Halton(2) for X
    float x = 0.0f;
    float f = 0.5f;
    uint32_t index = frameIndex;
    while (index > 0) {
        if (index & 1) x += f;
        f *= 0.5f;
        index >>= 1;
    }
    outX = x;

    // Halton(3) for Y
    float y = 0.0f;
    f = 0.333333f;
    index = frameIndex;
    while (index > 0) {
        if (index % 3 == 1) y += f;
        else if (index % 3 == 2) y += 2.0f * f;
        f *= 0.333333f;
        index /= 3;
    }
    outY = y;

    // Convert to [-0.5, 0.5] range
    outX -= 0.5f;
    outY -= 0.5f;
}

bool TemporalAA::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot resize uninitialized TAA");
        return false;
    }

    NEXT_LOG_INFO("Resizing TAA: %ux%u -> %ux%u", width_, height_, width, height);

    // Recreate resources with new size
    Shutdown();
    return Initialize(device_, srvHeap_, width, height);
}

void TemporalAA::Shutdown() {
    historyBuffer_[0].Reset();
    historyBuffer_[1].Reset();
    velocityBuffer_.Reset();

    device_ = nullptr;
    srvHeap_ = nullptr;
    width_ = 0;
    height_ = 0;
    historyIndex_ = 0;
    initialized_ = false;

    NEXT_LOG_INFO("TAA shutdown complete");
}

} // namespace Next
