#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/command_queue.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include <d3d12.h>
#include <wrl/client.h>

namespace Next {

// Forward declarations
class DX12CommandQueue;

// DX12 Swapchain Wrapper
class DX12Swapchain {
public:
    DX12Swapchain();
    ~DX12Swapchain();

    // Initialization
    bool Initialize(DX12Device* device, DX12CommandQueue* commandQueue, HWND hwnd, UINT width, UINT height);
    void Shutdown();

    // Swapchain Access
    IDXGISwapChain3* GetSwapChain() const { return swapchain_.Get(); }

    // Buffer Management
    UINT GetCurrentBackBufferIndex() const;
    ID3D12Resource* GetRenderTarget(UINT index) const;
    ID3D12Resource* GetCurrentRenderTarget() const;

    // RTV Access
    D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView(UINT index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetView() const;

    // Present
    void Present(UINT syncInterval = 1, UINT flags = 0);

    // Resize
    bool Resize(UINT width, UINT height);

    // Get Properties
    UINT GetWidth() const { return width_; }
    UINT GetHeight() const { return height_; }
    DXGI_FORMAT GetFormat() const { return format_; }

private:
    bool CreateRenderTargetViews();

    DX12Device* device_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> renderTargets_;
    DX12RTVHeap rtvHeap_;

    UINT width_;
    UINT height_;
    UINT bufferCount_;
    DXGI_FORMAT format_;

    bool initialized_;
};

} // namespace Next
