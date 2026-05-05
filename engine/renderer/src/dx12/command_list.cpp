#include "next/renderer/dx12/command_list.h"
#include "next/foundation/logger.h"

namespace Next {

DX12CommandList::DX12CommandList()
    : type_(D3D12_COMMAND_LIST_TYPE_DIRECT), initialized_(false) {
}

DX12CommandList::~DX12CommandList() {
    Shutdown();
}

bool DX12CommandList::Initialize(DX12Device* device, D3D12_COMMAND_LIST_TYPE type, uint32_t framesInFlight) {
    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for command list");
        return false;
    }
    if (framesInFlight == 0) {
        NEXT_LOG_ERROR("Invalid framesInFlight for command list");
        return false;
    }

    NEXT_LOG_DEBUG("Initializing DX12 Command List...");

    type_ = type;

    // Create one allocator per frame-in-flight. Resetting an allocator early can lead to GPU hangs/device removal.
    allocators_.clear();
    allocators_.reserve(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        HRESULT hrAlloc = device->GetDevice()->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator));
        if (FAILED(hrAlloc)) {
            NEXT_LOG_ERROR("Failed to create command allocator[%u]: 0x%X", i, hrAlloc);
            Shutdown();
            return false;
        }
        allocators_.push_back(allocator);
    }
    currentAllocator_ = allocators_[0];

    // Create command list (ID3D12GraphicsCommandList6 for DX12U mesh dispatch)
    HRESULT hr = device->GetDevice()->CreateCommandList1(
        0,  // node mask (single GPU)
        type,
        D3D12_COMMAND_LIST_FLAG_NONE,
        IID_PPV_ARGS(&commandList_)
    );
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create command list: 0x%X", hr);
        Shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

void DX12CommandList::Shutdown() {
    if (!initialized_ && !commandList_ && !currentAllocator_ && allocators_.empty()) {
        return;
    }

    commandList_.Reset();
    currentAllocator_.Reset();
    allocators_.clear();
    initialized_ = false;
}

bool DX12CommandList::Reset(uint32_t frameIndex) {
    if (!initialized_ || !commandList_) {
        NEXT_LOG_ERROR("Cannot reset uninitialized command list");
        return false;
    }

    if (allocators_.empty()) {
        NEXT_LOG_ERROR("Cannot reset command list without command allocators");
        return false;
    }

    const uint32_t idx = frameIndex % static_cast<uint32_t>(allocators_.size());
    currentAllocator_ = allocators_[idx];

    // CommandQueue::BeginFrame() is expected to have waited for this frame index before we reset.
    HRESULT hr = currentAllocator_->Reset();
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to reset command allocator[%u]: 0x%X", idx, hr);
        return false;
    }

    hr = commandList_->Reset(currentAllocator_.Get(), nullptr);
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to reset command list: 0x%X", hr);
        return false;
    }

    return true;
}

bool DX12CommandList::Close() {
    if (!initialized_ || !commandList_) {
        NEXT_LOG_ERROR("Cannot close uninitialized command list");
        return false;
    }

    HRESULT hr = commandList_->Close();
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to close command list: 0x%X", hr);
        return false;
    }

    return true;
}

void DX12CommandList::OMSetRenderTargets(
    UINT numRTVs,
    const D3D12_CPU_DESCRIPTOR_HANDLE* rtvDescriptors,
    BOOL depthStencil,
    D3D12_CPU_DESCRIPTOR_HANDLE dsvDescriptor) {

    if (!initialized_ || !commandList_) {
        return;
    }

    if (numRTVs > 0 && !rtvDescriptors) {
        NEXT_LOG_ERROR("Invalid RTV descriptor array for OMSetRenderTargets");
        return;
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE* dsvDescriptorPtr = nullptr;
    if (depthStencil) {
        if (dsvDescriptor.ptr == 0) {
            NEXT_LOG_WARNING("Depth-stencil enabled but descriptor handle is null; ignoring depth-stencil for this pass");
        } else {
            dsvDescriptorPtr = &dsvDescriptor;
        }
    }

    // Set render targets using CPU descriptor handles
    // The 4th parameter needs to be a pointer to the descriptor handle.
    commandList_->OMSetRenderTargets(numRTVs, rtvDescriptors, depthStencil, dsvDescriptorPtr);
}

void DX12CommandList::ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const float color[4]) {
    if (!initialized_ || !commandList_) {
        return;
    }

    commandList_->ClearRenderTargetView(rtv, color, 0, nullptr);
}

void DX12CommandList::ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE dsv, float depth, UINT8 stencil) {
    if (!initialized_ || !commandList_) {
        return;
    }

    commandList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, depth, stencil, 0, nullptr);
}

void DX12CommandList::SetPipelineState(ID3D12PipelineState* pso) {
    if (!initialized_ || !commandList_ || !pso) {
        return;
    }

    commandList_->SetPipelineState(pso);
}

void DX12CommandList::SetGraphicsRootSignature(ID3D12RootSignature* rootSig) {
    if (!initialized_ || !commandList_) {
        return;
    }

    commandList_->SetGraphicsRootSignature(rootSig);
}

void DX12CommandList::SetDescriptorHeaps(
    UINT numDescriptorHeaps,
    ID3D12DescriptorHeap* const* pDescriptorHeaps) {

    if (!initialized_ || !commandList_) {
        return;
    }

    commandList_->SetDescriptorHeaps(numDescriptorHeaps, pDescriptorHeaps);
}

void DX12CommandList::IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology) {
    if (!initialized_ || !commandList_) {
        return;
    }

    commandList_->IASetPrimitiveTopology(topology);
}

void DX12CommandList::IASetVertexBuffers(UINT startSlot, UINT numViews, const D3D12_VERTEX_BUFFER_VIEW* views) {
    if (!initialized_ || !commandList_) {
        return;
    }

    commandList_->IASetVertexBuffers(startSlot, numViews, views);
}

void DX12CommandList::IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* view, DXGI_FORMAT format) {
    if (!initialized_ || !commandList_) {
        return;
    }

    commandList_->IASetIndexBuffer(view);
}

void DX12CommandList::DrawIndexedInstanced(UINT indexCount, UINT instanceCount, UINT startIndexLocation, int baseVertexLocation, UINT startInstanceLocation) {
    if (!initialized_ || !commandList_) {
        return;
    }

    commandList_->DrawIndexedInstanced(indexCount, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
}

void DX12CommandList::DrawInstanced(UINT vertexCount, UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation) {
    if (!initialized_ || !commandList_) {
        return;
    }

    commandList_->DrawInstanced(vertexCount, instanceCount, startVertexLocation, startInstanceLocation);
}

void DX12CommandList::ExecuteIndirect(ID3D12CommandSignature* commandSignature, UINT maxCommandCount,
                                      ID3D12Resource* argumentBuffer, UINT64 argumentBufferOffset) {
    if (!initialized_ || !commandList_ || !commandSignature || !argumentBuffer) {
        return;
    }

    commandList_->ExecuteIndirect(commandSignature, maxCommandCount, argumentBuffer, argumentBufferOffset, nullptr, 0);
}

void DX12CommandList::ResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER* barriers) {
    if (!initialized_ || !commandList_ || numBarriers == 0 || !barriers) {
        return;
    }

    commandList_->ResourceBarrier(numBarriers, barriers);
}

void DX12CommandList::RSSetShadingRate(D3D12_SHADING_RATE shadingRate, D3D12_SHADING_RATE_COMBINER combiner) {
    if (!initialized_ || !commandList_) {
        return;
    }

    D3D12_SHADING_RATE_COMBINER combiners[2] = {
        combiner,
        D3D12_SHADING_RATE_COMBINER_PASSTHROUGH
    };
    commandList_->RSSetShadingRate(shadingRate, combiners);
}

void DX12CommandList::DispatchMesh(UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ) {
    if (!initialized_ || !commandList_) {
        return;
    }

    constexpr UINT kMaxDispatchDimension = 65535;
    constexpr uint64_t kMaxDispatchGroups = 1ull << 22;
    const uint64_t totalGroups =
        static_cast<uint64_t>(threadGroupCountX) *
        static_cast<uint64_t>(threadGroupCountY) *
        static_cast<uint64_t>(threadGroupCountZ);

    if (threadGroupCountX == 0 || threadGroupCountY == 0 || threadGroupCountZ == 0 ||
        threadGroupCountX > kMaxDispatchDimension ||
        threadGroupCountY > kMaxDispatchDimension ||
        threadGroupCountZ > kMaxDispatchDimension ||
        totalGroups > kMaxDispatchGroups) {
        NEXT_LOG_ERROR("Invalid DispatchMesh dimensions: %u x %u x %u",
                       threadGroupCountX,
                       threadGroupCountY,
                       threadGroupCountZ);
        return;
    }

    commandList_->DispatchMesh(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
}

void DX12CommandList::RSSetViewports(UINT numViewports, const D3D12_VIEWPORT* viewports) {
    if (!initialized_ || !commandList_ || numViewports == 0 || !viewports) {
        return;
    }

    commandList_->RSSetViewports(numViewports, viewports);
}

void DX12CommandList::RSSetScissorRects(UINT numRects, const D3D12_RECT* rects) {
    if (!initialized_ || !commandList_ || numRects == 0 || !rects) {
        return;
    }

    commandList_->RSSetScissorRects(numRects, rects);
}

// Command Allocator Pool
DX12CommandAllocatorPool::DX12CommandAllocatorPool(DX12Device* device, D3D12_COMMAND_LIST_TYPE type)
    : currentAllocator_(0) {
    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for command allocator pool");
        return;
    }

    // Create initial allocators
    for (int i = 0; i < 4; ++i) {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        HRESULT hr = device->GetDevice()->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator));
        if (FAILED(hr)) {
            NEXT_LOG_ERROR("Failed to create pooled command allocator[%d]: 0x%X", i, hr);
            continue;
        }
        allocators_.push_back(allocator);
    }
}

DX12CommandAllocatorPool::~DX12CommandAllocatorPool() {
    allocators_.clear();
}

ID3D12CommandAllocator* DX12CommandAllocatorPool::GetAllocator() {
    if (allocators_.empty()) {
        return nullptr;
    }

    size_t index = currentAllocator_ % allocators_.size();
    currentAllocator_++;
    return allocators_[index].Get();
}

void DX12CommandAllocatorPool::ResetAllocators() {
    for (auto& allocator : allocators_) {
        if (!allocator) {
            continue;
        }
        HRESULT hr = allocator->Reset();
        if (FAILED(hr)) {
            NEXT_LOG_ERROR("Failed to reset pooled command allocator: 0x%X", hr);
        }
    }
    currentAllocator_ = 0;
}

} // namespace Next
