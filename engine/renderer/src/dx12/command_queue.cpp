#include "next/renderer/dx12/command_queue.h"
#include "next/renderer/dx12/command_list.h"
#include "next/foundation/logger.h"

namespace Next {

DX12CommandQueue::DX12CommandQueue()
    : currentFenceValue_(0), frameIndex_(0), timestampFrequency_(0), initialized_(false) {
    memset(frameFenceValues_, 0, sizeof(frameFenceValues_));
}

DX12CommandQueue::~DX12CommandQueue() {
    Shutdown();
}

bool DX12CommandQueue::Initialize(DX12Device* device, D3D12_COMMAND_LIST_TYPE type) {
    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for command queue");
        return false;
    }

    NEXT_LOG_INFO("Initializing DX12 Command Queue...");

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = type;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;  // Single GPU for now

    HRESULT hr = device->GetDevice()->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create command queue: 0x%X", hr);
        return false;
    }

    // Create fence for synchronization
    if (!fence_.Initialize(device, 0)) {
        NEXT_LOG_ERROR("Failed to initialize fence");
        return false;
    }

    currentFenceValue_ = 0;

    // Get timestamp frequency
    commandQueue_->GetTimestampFrequency(&timestampFrequency_);

    initialized_ = true;
    NEXT_LOG_INFO("Command Queue initialized successfully with frame-in-flight support (%d frames)", MAX_FRAME_IN_FLIGHT);
    return true;
}

void DX12CommandQueue::Shutdown() {
    if (!initialized_) {
        return;
    }

    NEXT_LOG_INFO("Shutting down Command Queue...");

    // Wait for all operations to complete
    Flush();

    fence_.Shutdown();
    commandQueue_.Reset();

    initialized_ = false;
    NEXT_LOG_INFO("Command Queue shutdown complete");
}

uint64_t DX12CommandQueue::ExecuteCommandList(ID3D12CommandList* commandList) {
    if (!commandList || !initialized_) {
        return 0;
    }

    ID3D12CommandList* lists[] = { commandList };
    commandQueue_->ExecuteCommandLists(_countof(lists), lists);

    // Signal fence (but don't wait yet)
    uint64_t fenceValue = fence_.Signal(commandQueue_.Get());
    currentFenceValue_ = fenceValue;

    return fenceValue;
}

uint64_t DX12CommandQueue::ExecuteCommandList(DX12CommandList* commandList) {
    if (!commandList || !initialized_) {
        return 0;
    }

    return ExecuteCommandList(commandList->GetCommandList());
}

void DX12CommandQueue::BeginFrame() {
    if (!initialized_) {
        return;
    }

    // Wait for this frame to be available (frame-in-flight)
    WaitForFrame(frameIndex_);
}

void DX12CommandQueue::EndFrame() {
    if (!initialized_) {
        return;
    }

    // Store fence value for this frame
    frameFenceValues_[frameIndex_] = currentFenceValue_;

    // Move to next frame
    frameIndex_ = (frameIndex_ + 1) % MAX_FRAME_IN_FLIGHT;
}

void DX12CommandQueue::WaitForGPU() {
    if (!initialized_) {
        return;
    }

    // Wait for all pending frames to complete
    Flush();
}

void DX12CommandQueue::WaitForFrame(uint32_t frameIndex) {
    if (!initialized_ || frameIndex >= MAX_FRAME_IN_FLIGHT) {
        return;
    }

    uint64_t fenceValue = frameFenceValues_[frameIndex];
    if (fenceValue == 0) {
        return;  // No work submitted for this frame yet
    }

    // Wait for fence
    fence_.Wait(fenceValue);
}

void DX12CommandQueue::Flush() {
    if (!initialized_) {
        return;
    }

    // Signal and wait for fence
    uint64_t fenceValue = fence_.Signal(commandQueue_.Get());
    fence_.Wait(fenceValue);
}

} // namespace Next
