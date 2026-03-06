#pragma once

#include "next/renderer/dx12/device.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

namespace Next {

// DX12 Descriptor Heap Wrapper
class DX12DescriptorHeap {
public:
    DX12DescriptorHeap();
    ~DX12DescriptorHeap();

    // Initialization
    bool Initialize(
        DX12Device* device,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        D3D12_DESCRIPTOR_HEAP_FLAGS flags,
        UINT numDescriptors);

    void Shutdown();

    // Descriptor Access
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(UINT index) const;

    // Heap Properties
    ID3D12DescriptorHeap* GetHeap() const { return heap_.Get(); }
    UINT GetDescriptorSize() const { return descriptorSize_; }
    UINT GetNumDescriptors() const { return numDescriptors_; }

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc_;
    UINT descriptorSize_;
    UINT numDescriptors_;
    bool initialized_;
};

// RTV (Render Target View) Descriptor Heap
class DX12RTVHeap : public DX12DescriptorHeap {
public:
    bool Initialize(DX12Device* device, UINT numDescriptors);
};

// DSV (Depth Stencil View) Descriptor Heap
class DX12DSVHeap : public DX12DescriptorHeap {
public:
    bool Initialize(DX12Device* device, UINT numDescriptors);
};

// CBV_SRV_UAV (Shader Resource View) Descriptor Heap
class DX12CBVSRVUAVHeap : public DX12DescriptorHeap {
public:
    bool Initialize(DX12Device* device, UINT numDescriptors, bool shaderVisible = true);
};

// Sampler Descriptor Heap
class DX12SamplerHeap : public DX12DescriptorHeap {
public:
    bool Initialize(DX12Device* device, UINT numDescriptors, bool shaderVisible = true);
};

} // namespace Next
