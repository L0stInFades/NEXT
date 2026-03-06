#pragma once

#include "next/renderer/dx12/device.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

namespace Next {

// DX12 Buffer Wrapper (Vertex Buffer, Index Buffer, Constant Buffer)
class DX12Buffer {
public:
    DX12Buffer();
    ~DX12Buffer();

    // Initialization
    bool Initialize(
        DX12Device* device,
        size_t size,
        D3D12_HEAP_TYPE heapType,
        D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE);

    void Shutdown();

    // Data Upload
    bool UploadData(const void* data, size_t size);

    // Buffer Access
    ID3D12Resource* GetResource() const { return resource_.Get(); }
    size_t GetSize() const { return size_; }
    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const {
        return resource_ ? resource_->GetGPUVirtualAddress() : 0;
    }

    // Vertex Buffer View
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView(UINT stride) const;

    // Index Buffer View
    D3D12_INDEX_BUFFER_VIEW GetIndexBufferView(DXGI_FORMAT format) const;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    size_t size_;
    bool initialized_;
};

} // namespace Next
