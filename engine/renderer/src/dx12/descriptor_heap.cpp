#include "next/renderer/dx12/descriptor_heap.h"
#include "next/foundation/logger.h"

namespace Next {

//=============================================================================
// DX12DescriptorHeap
//=============================================================================

DX12DescriptorHeap::DX12DescriptorHeap()
    : descriptorSize_(0), numDescriptors_(0), initialized_(false) {
    memset(&heapDesc_, 0, sizeof(heapDesc_));
}

DX12DescriptorHeap::~DX12DescriptorHeap() {
    Shutdown();
}

bool DX12DescriptorHeap::Initialize(
    DX12Device* device,
    D3D12_DESCRIPTOR_HEAP_TYPE type,
    D3D12_DESCRIPTOR_HEAP_FLAGS flags,
    UINT numDescriptors) {

    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for descriptor heap");
        return false;
    }

    if (numDescriptors == 0) {
        NEXT_LOG_ERROR("Descriptor heap must contain at least one descriptor");
        return false;
    }

    Shutdown();

    NEXT_LOG_DEBUG("Initializing DX12 Descriptor Heap (Type: %d, Count: %u)", type, numDescriptors);

    // Fill heap description
    heapDesc_.Type = type;
    heapDesc_.NumDescriptors = numDescriptors;
    heapDesc_.Flags = flags;
    heapDesc_.NodeMask = 0;  // Single GPU

    // Create descriptor heap
    HRESULT hr = device->GetDevice()->CreateDescriptorHeap(
        &heapDesc_,
        IID_PPV_ARGS(&heap_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create descriptor heap: 0x%X", hr);
        memset(&heapDesc_, 0, sizeof(heapDesc_));
        return false;
    }

    // Get descriptor size
    descriptorSize_ = device->GetDevice()->GetDescriptorHandleIncrementSize(type);
    numDescriptors_ = numDescriptors;

    initialized_ = true;
    NEXT_LOG_DEBUG("Descriptor heap created successfully (Size: %u bytes)", descriptorSize_);
    return true;
}

void DX12DescriptorHeap::Shutdown() {
    heap_.Reset();
    memset(&heapDesc_, 0, sizeof(heapDesc_));
    descriptorSize_ = 0;
    numDescriptors_ = 0;
    initialized_ = false;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::GetCPUDescriptorHandle(UINT index) const {
    if (!heap_ || index >= numDescriptors_) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * descriptorSize_;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12DescriptorHeap::GetGPUDescriptorHandle(UINT index) const {
    if (!heap_ || index >= numDescriptors_ || !IsShaderVisible()) {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = {};
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE handle = heap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += index * descriptorSize_;
    return handle;
}

//=============================================================================
// DX12RTVHeap
//=============================================================================

bool DX12RTVHeap::Initialize(DX12Device* device, UINT numDescriptors) {
    // RTV heaps are not shader visible
    return DX12DescriptorHeap::Initialize(
        device,
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        numDescriptors
    );
}

//=============================================================================
// DX12DSVHeap
//=============================================================================

bool DX12DSVHeap::Initialize(DX12Device* device, UINT numDescriptors) {
    // DSV heaps are not shader visible
    return DX12DescriptorHeap::Initialize(
        device,
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        numDescriptors
    );
}

//=============================================================================
// DX12CBVSRVUAVHeap
//=============================================================================

bool DX12CBVSRVUAVHeap::Initialize(DX12Device* device, UINT numDescriptors, bool shaderVisible) {
    D3D12_DESCRIPTOR_HEAP_FLAGS flags = shaderVisible
        ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
        : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    return DX12DescriptorHeap::Initialize(
        device,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        flags,
        numDescriptors
    );
}

//=============================================================================
// DX12SamplerHeap
//=============================================================================

bool DX12SamplerHeap::Initialize(DX12Device* device, UINT numDescriptors, bool shaderVisible) {
    D3D12_DESCRIPTOR_HEAP_FLAGS flags = shaderVisible
        ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
        : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    return DX12DescriptorHeap::Initialize(
        device,
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
        flags,
        numDescriptors
    );
}

} // namespace Next
