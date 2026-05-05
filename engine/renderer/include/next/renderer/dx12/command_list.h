#pragma once

#include "next/renderer/dx12/device.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

namespace Next {

// DX12 Command List Wrapper (ID3D12GraphicsCommandList6 for DX12U mesh dispatch)
class DX12CommandList {
public:
    DX12CommandList();
    ~DX12CommandList();

    // Initialization
    // NOTE: D3D12 command allocators cannot be reset until the GPU is done with them.
    // We keep one allocator per frame-in-flight to avoid device removal under normal double-buffering.
    bool Initialize(DX12Device* device, D3D12_COMMAND_LIST_TYPE type, uint32_t framesInFlight);
    void Shutdown();

    // Command List Access
    ID3D12GraphicsCommandList6* GetCommandList() const { return commandList_.Get(); }
    ID3D12CommandAllocator* GetAllocator() const { return currentAllocator_.Get(); }

    // Recording
    bool Reset(uint32_t frameIndex);
    bool Close();

    // Render Target
    void OMSetRenderTargets(
        UINT numRTVs,
        const D3D12_CPU_DESCRIPTOR_HANDLE* rtvDescriptors,
        BOOL depthStencil,
        D3D12_CPU_DESCRIPTOR_HANDLE dsvDescriptor = {});

    void ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const float color[4]);
    void ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE dsv, float depth, UINT8 stencil);

    // Pipeline State
    void SetPipelineState(ID3D12PipelineState* pso);
    void SetGraphicsRootSignature(ID3D12RootSignature* rootSig);

    // Descriptor Heaps
    void SetDescriptorHeaps(
        UINT numDescriptorHeaps,
        ID3D12DescriptorHeap* const* pDescriptorHeaps);

    // Primitive Drawing
    void IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology);
    void IASetVertexBuffers(UINT startSlot, UINT numViews, const D3D12_VERTEX_BUFFER_VIEW* views);
    void IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* view, DXGI_FORMAT format);
    void DrawIndexedInstanced(UINT indexCount, UINT instanceCount, UINT startIndexLocation, int baseVertexLocation, UINT startInstanceLocation);
    void DrawInstanced(UINT vertexCount, UINT instanceCount, UINT startVertexLocation, UINT startInstanceLocation);
    void ExecuteIndirect(ID3D12CommandSignature* commandSignature, UINT maxCommandCount,
                         ID3D12Resource* argumentBuffer, UINT64 argumentBufferOffset);

    // Resource Barriers (DX12U Enhanced Barriers)
    void ResourceBarrier(UINT numBarriers, const D3D12_RESOURCE_BARRIER* barriers);

    // DX12U Specific
    void RSSetShadingRate(D3D12_SHADING_RATE shadingRate, D3D12_SHADING_RATE_COMBINER combiner);
    void DispatchMesh(UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ);

    // Viewport and Scissor
    void RSSetViewports(UINT numViewports, const D3D12_VIEWPORT* viewports);
    void RSSetScissorRects(UINT numRects, const D3D12_RECT* rects);

private:
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList6> commandList_;  // DX12U
    std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> allocators_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> currentAllocator_;
    D3D12_COMMAND_LIST_TYPE type_;
    bool initialized_;
};

// Command List Allocator Pool (for multi-threaded recording)
class DX12CommandAllocatorPool {
public:
    DX12CommandAllocatorPool(DX12Device* device, D3D12_COMMAND_LIST_TYPE type);
    ~DX12CommandAllocatorPool();

    ID3D12CommandAllocator* GetAllocator();
    void ResetAllocators();

private:
    std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> allocators_;
    size_t currentAllocator_;
};

} // namespace Next
