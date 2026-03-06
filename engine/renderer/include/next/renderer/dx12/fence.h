#pragma once

#include "next/renderer/dx12/device.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <windows.h>

namespace Next {

// DX12 Fence Wrapper for efficient GPU synchronization
class DX12Fence {
public:
    DX12Fence();
    ~DX12Fence();

    // Initialization
    bool Initialize(DX12Device* device, uint64_t initialValue = 0);
    void Shutdown();

    // Fence operations
    uint64_t Signal(ID3D12CommandQueue* queue);
    void Wait(uint64_t value);
    void WaitCPU(uint64_t value);

    // Get current fence value
    uint64_t GetCurrentValue() const { return currentValue_; }
    uint64_t GetCompletedValue() const { return fence_->GetCompletedValue(); }

    // Fence access
    ID3D12Fence* GetFence() const { return fence_.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_;
    uint64_t currentValue_;
    bool initialized_;
};

} // namespace Next
