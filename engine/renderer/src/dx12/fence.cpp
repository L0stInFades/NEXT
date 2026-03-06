#include "next/renderer/dx12/fence.h"
#include "next/foundation/logger.h"

namespace Next {

DX12Fence::DX12Fence()
    : fenceEvent_(nullptr), currentValue_(0), initialized_(false) {
}

DX12Fence::~DX12Fence() {
    Shutdown();
}

bool DX12Fence::Initialize(DX12Device* device, uint64_t initialValue) {
    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for fence");
        return false;
    }

    NEXT_LOG_DEBUG("Initializing DX12 Fence...");

    // Create fence
    HRESULT hr = device->GetDevice()->CreateFence(
        initialValue,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&fence_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create fence: 0x%X", hr);
        return false;
    }

    currentValue_ = initialValue;

    // Create event for fence synchronization (reusable)
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        NEXT_LOG_ERROR("Failed to create fence event");
        fence_.Reset();
        return false;
    }

    initialized_ = true;
    NEXT_LOG_DEBUG("Fence initialized successfully (initial value: %llu)", initialValue);
    return true;
}

void DX12Fence::Shutdown() {
    if (!initialized_) {
        return;
    }

    NEXT_LOG_DEBUG("Shutting down fence...");

    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }

    fence_.Reset();
    currentValue_ = 0;
    initialized_ = false;

    NEXT_LOG_DEBUG("Fence shutdown complete");
}

uint64_t DX12Fence::Signal(ID3D12CommandQueue* queue) {
    if (!queue || !initialized_) {
        return 0;
    }

    // Signal the fence with current value
    queue->Signal(fence_.Get(), currentValue_);

    return currentValue_++;
}

void DX12Fence::Wait(uint64_t value) {
    if (!initialized_) {
        return;
    }

    // Check if fence has already reached the value
    uint64_t completedValue = fence_->GetCompletedValue();
    if (completedValue >= value) {
        return;  // Already completed
    }

    // Set event to trigger when fence reaches value
    HRESULT hr = fence_->SetEventOnCompletion(value, fenceEvent_);
    if (SUCCEEDED(hr)) {
        // Wait for the event (with timeout for debugging)
        DWORD waitResult = WaitForSingleObject(fenceEvent_, 5000);  // 5 second timeout

        if (waitResult == WAIT_TIMEOUT) {
            NEXT_LOG_WARNING("Fence wait timeout (value: %llu, completed: %llu)",
                          value, fence_->GetCompletedValue());
        } else if (waitResult == WAIT_FAILED) {
            NEXT_LOG_ERROR("Fence wait failed (value: %llu)", value);
        }
    } else {
        NEXT_LOG_ERROR("Failed to set fence event: 0x%X", hr);
    }
}

void DX12Fence::WaitCPU(uint64_t value) {
    if (!initialized_) {
        return;
    }

    // Busy-wait spin loop for low-latency scenarios
    while (fence_->GetCompletedValue() < value) {
        // Yield to other threads
        YieldProcessor();
    }
}

} // namespace Next
