#include "next/renderer/dx12/sampler.h"
#include "next/foundation/logger.h"

namespace Next {

DX12Sampler::DX12Sampler()
    : device_(nullptr), samplerHeap_(nullptr), initialized_(false) {
    gpuDescriptorHandle_.ptr = 0;
}

DX12Sampler::~DX12Sampler() {
    Shutdown();
}

bool DX12Sampler::Initialize(DX12Device* device, DX12DescriptorHeap* samplerHeap) {
    if (!device || !samplerHeap) {
        NEXT_LOG_ERROR("Invalid device or sampler heap");
        return false;
    }

    device_ = device;
    samplerHeap_ = samplerHeap;

    initialized_ = true;
    return true;
}

bool DX12Sampler::Create(D3D12_FILTER filter,
                        D3D12_TEXTURE_ADDRESS_MODE addressU,
                        D3D12_TEXTURE_ADDRESS_MODE addressV,
                        D3D12_TEXTURE_ADDRESS_MODE addressW) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Sampler not initialized");
        return false;
    }

    // Get descriptor handle from sampler heap
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = samplerHeap_->GetCPUDescriptorHandle(0);
    gpuDescriptorHandle_ = samplerHeap_->GetGPUDescriptorHandle(0);

    return Create(cpuHandle, filter, addressU, addressV, addressW);
}

bool DX12Sampler::Create(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                        D3D12_FILTER filter,
                        D3D12_TEXTURE_ADDRESS_MODE addressU,
                        D3D12_TEXTURE_ADDRESS_MODE addressV,
                        D3D12_TEXTURE_ADDRESS_MODE addressW) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Sampler not initialized");
        return false;
    }

    if (cpuHandle.ptr == 0) {
        NEXT_LOG_ERROR("Invalid sampler descriptor handle");
        return false;
    }

    // Create sampler description
    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = filter;
    samplerDesc.AddressU = addressU;
    samplerDesc.AddressV = addressV;
    samplerDesc.AddressW = addressW;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.BorderColor[0] = 1.0f;
    samplerDesc.BorderColor[1] = 1.0f;
    samplerDesc.BorderColor[2] = 1.0f;
    samplerDesc.BorderColor[3] = 1.0f;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

    device_->GetDevice()->CreateSampler(&samplerDesc, cpuHandle);

    NEXT_LOG_DEBUG("Sampler created successfully");
    return true;
}

void DX12Sampler::Shutdown() {
    if (!initialized_) {
        return;
    }

    gpuDescriptorHandle_.ptr = 0;
    device_ = nullptr;
    samplerHeap_ = nullptr;
    initialized_ = false;
}

} // namespace Next
