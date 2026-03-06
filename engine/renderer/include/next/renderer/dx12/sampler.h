#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include <d3d12.h>

namespace Next {

// DX12 Sampler Wrapper
class DX12Sampler {
public:
    DX12Sampler();
    ~DX12Sampler();

    // Initialization
    bool Initialize(DX12Device* device, DX12DescriptorHeap* samplerHeap);
    void Shutdown();

    // Create sampler with specified parameters
    bool Create(D3D12_FILTER filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                D3D12_TEXTURE_ADDRESS_MODE addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                D3D12_TEXTURE_ADDRESS_MODE addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                D3D12_TEXTURE_ADDRESS_MODE addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    bool Create(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                D3D12_FILTER filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                D3D12_TEXTURE_ADDRESS_MODE addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                D3D12_TEXTURE_ADDRESS_MODE addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                D3D12_TEXTURE_ADDRESS_MODE addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    // Sampler Access
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle() const { return gpuDescriptorHandle_; }

    void SetGPUDescriptorHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { gpuDescriptorHandle_ = handle; }

private:
    DX12Device* device_;
    DX12DescriptorHeap* samplerHeap_;

    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle_;

    bool initialized_;
};

} // namespace Next
