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
            return false;
        }
        allocators_.push_back(allocator);
    }
    currentAllocator_ = allocators_[0];

    // Create command list (ID3D12GraphicsCommandList4 for DX12U)
    HRESULT hr = device->GetDevice()->CreateCommandList1(
        0,  // node mask (single GPU)
        type,
        D3D12_COMMAND_LIST_FLAG_NONE,
        IID_PPV_ARGS(&commandList_)
    );
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create command list: 0x%X", hr);
        return false;
    }

    initialized_ = true;
    return true;
}

void DX12CommandList::Shutdown() {
    if (!initialized_) {
        return;
    }

    commandList_.Reset();
    currentAllocator_.Reset();
    allocators_.clear();
    initialized_ = false;
}

void DX12CommandList::Reset(uint32_t frameIndex) {
    if (!initialized_) {
        return;
    }

    if (allocators_.empty()) {
        return;
    }

    const uint32_t idx = frameIndex % static_cast<uint32_t>(allocators_.size());
    currentAllocator_ = allocators_[idx];

    // CommandQueue::BeginFrame() is expected to have waited for this frame index before we reset.
    currentAllocator_->Reset();
    commandList_->Reset(currentAllocator_.Get(), nullptr);
}

void DX12CommandList::Close() {
    if (!initialized_) {
        return;
    }

    commandList_->Close();
}

void DX12CommandList::OMSetRenderTargets(
    UINT numRTVs,
    const D3D12_CPU_DESCRIPTOR_HANDLE* rtvDescriptors,
    BOOL depthStencil,
    D3D12_CPU_DESCRIPTOR_HANDLE dsvDescriptor) {

    if (!initialized_ || !commandList_) {
        return;
    }

    // Set render targets using CPU descriptor handles
    // The 4th parameter needs to be a pointer to the descriptor handle
    commandList_->OMSetRenderTargets(numRTVs, rtvDescriptors, depthStencil, depthStencil ? &dsvDescriptor : nullptr);
}

void DX12CommandList::ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const float color[4]) {
    if (!initialized_ || !commandList_) {
        return;
    }

    commandList_->ClearRenderTargetView(rtv, color, 0, nullptr);
}

void DX12CommandList::ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE dsv, float depth, UINT8 stencil) {
    commandList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, depth, stencil, 0, nullptr);
}

void DX12CommandList::SetPipelineState(ID3D12PipelineState* pso) {
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
    commandList_->ResourceBarrier(numBarriers, barriers);
}

void DX12CommandList::RSSetShadingRate(D3D12_SHADING_RATE shadingRate, D3D12_SHADING_RATE_COMBINER combiner) {
    // DX12U Variable Rate Shading - TODO: Phase 2
    // For now, this is a placeholder
    // RSSetShadingRate is part of Enhanced Barriers model
    NEXT_LOG_WARNING("VRS not yet implemented (DX12U Phase 2)");
}

void DX12CommandList::RSSetViewports(UINT numViewports, const D3D12_VIEWPORT* viewports) {
    commandList_->RSSetViewports(numViewports, viewports);
}

void DX12CommandList::RSSetScissorRects(UINT numRects, const D3D12_RECT* rects) {
    commandList_->RSSetScissorRects(numRects, rects);
}

// Command Allocator Pool
DX12CommandAllocatorPool::DX12CommandAllocatorPool(DX12Device* device, D3D12_COMMAND_LIST_TYPE type)
    : currentAllocator_(0) {
    // Create initial allocators
    for (int i = 0; i < 4; ++i) {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        device->GetDevice()->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator));
        allocators_.push_back(allocator);
    }
}

DX12CommandAllocatorPool::~DX12CommandAllocatorPool() {
    allocators_.clear();
}

ID3D12CommandAllocator* DX12CommandAllocatorPool::GetAllocator() {
    size_t index = currentAllocator_ % allocators_.size();
    currentAllocator_++;
    return allocators_[index].Get();
}

void DX12CommandAllocatorPool::ResetAllocators() {
    for (auto& allocator : allocators_) {
        allocator->Reset();
    }
    currentAllocator_ = 0;
}

} // namespace Next
