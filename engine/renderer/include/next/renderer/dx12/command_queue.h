#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/fence.h"
#include <d3d12.h>
#include <wrl/client.h>

namespace Next {

// Forward declarations
class DX12CommandList;

// DX12 Command Queue Wrapper with frame-in-flight support
class DX12CommandQueue {
public:
    static const uint32_t MAX_FRAME_IN_FLIGHT = 2;  // Double buffering

    DX12CommandQueue();
    ~DX12CommandQueue();

    // Initialization
    bool Initialize(DX12Device* device, D3D12_COMMAND_LIST_TYPE type);
    void Shutdown();

    // Queue Access
    ID3D12CommandQueue* GetQueue() const { return commandQueue_.Get(); }

    // Command Execution
    uint64_t ExecuteCommandList(ID3D12CommandList* commandList);
    uint64_t ExecuteCommandList(DX12CommandList* commandList);

    // Frame Synchronization (frame-in-flight)
    void BeginFrame();
    void EndFrame();
    void WaitForGPU();
    void WaitForFrame(uint32_t frameIndex);
    uint64_t GetCurrentFenceValue() const { return currentFenceValue_; }

    // Legacy Flush (for shutdown)
    void Flush();

    // Get Timestamp frequency
    uint64_t GetTimestampFrequency() const { return timestampFrequency_; }

    // Frame tracking
    uint32_t GetFrameIndex() const { return frameIndex_; }

private:
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    DX12Fence fence_;

    uint64_t currentFenceValue_;
    uint64_t frameFenceValues_[MAX_FRAME_IN_FLIGHT];
    uint32_t frameIndex_;

    uint64_t timestampFrequency_;
    bool initialized_;
};

} // namespace Next
