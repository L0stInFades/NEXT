#include "next/renderer/dx12/swapchain.h"
#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/command_queue.h"
#include "next/foundation/logger.h"
#include <windows.h>

namespace Next {

DX12Swapchain::DX12Swapchain()
    : device_(nullptr), width_(0), height_(0), bufferCount_(2), format_(DXGI_FORMAT_R8G8B8A8_UNORM), initialized_(false) {
}

DX12Swapchain::~DX12Swapchain() {
    Shutdown();
}

bool DX12Swapchain::Initialize(DX12Device* device, DX12CommandQueue* commandQueue, HWND hwnd, UINT width, UINT height) {
    NEXT_LOG_INFO("Initializing DX12 Swapchain...");

    if (!device || !device->GetDevice() || !commandQueue || !commandQueue->GetQueue()) {
        NEXT_LOG_ERROR("Invalid parameters for swapchain");
        return false;
    }

    device_ = device;
    width_ = width;
    height_ = height;

    // Use legacy DXGI_SWAP_CHAIN_DESC for simplicity
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = bufferCount_;
    swapChainDesc.BufferDesc.Width = width;
    swapChainDesc.BufferDesc.Height = height;
    swapChainDesc.BufferDesc.Format = format_;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_STRETCHED;
    swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;

    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
    HRESULT hr = device->GetFactory()->CreateSwapChain(
        commandQueue->GetQueue(),
        &swapChainDesc,
        &swapChain
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create swapchain: 0x%X", hr);
        return false;
    }

    // Query DX12U interface
    hr = swapChain.As(&swapchain_);
    if (FAILED(hr)) {
        NEXT_LOG_WARNING("Failed to query DX12U swapchain interface: 0x%X", hr);
        // Continue with legacy interface
    }

    // Create render targets
    for (UINT i = 0; i < bufferCount_; ++i) {
        Microsoft::WRL::ComPtr<ID3D12Resource> rt;
        hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&rt));
        if (SUCCEEDED(hr)) {
            renderTargets_.push_back(rt);
        }
    }

    // Create RTV descriptor heap and views
    if (!CreateRenderTargetViews()) {
        NEXT_LOG_ERROR("Failed to create render target views");
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("Swapchain initialized successfully (%u buffers, %ux%u)", bufferCount_, width, height);
    return true;
}

void DX12Swapchain::Shutdown() {
    rtvHeap_.Shutdown();
    renderTargets_.clear();
    swapchain_.Reset();
    device_ = nullptr;
    initialized_ = false;
}

UINT DX12Swapchain::GetCurrentBackBufferIndex() const {
    if (!swapchain_) {
        return 0;
    }

    return swapchain_->GetCurrentBackBufferIndex();
}

ID3D12Resource* DX12Swapchain::GetRenderTarget(UINT index) const {
    if (!swapchain_ || index >= renderTargets_.size()) {
        return nullptr;
    }

    return renderTargets_[index].Get();
}

ID3D12Resource* DX12Swapchain::GetCurrentRenderTarget() const {
    return GetRenderTarget(GetCurrentBackBufferIndex());
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Swapchain::GetRenderTargetView(UINT index) const {
    return rtvHeap_.GetCPUDescriptorHandle(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Swapchain::GetCurrentRenderTargetView() const {
    return GetRenderTargetView(GetCurrentBackBufferIndex());
}

void DX12Swapchain::Present(UINT syncInterval, UINT flags) {
    if (!swapchain_ || !initialized_) {
        return;
    }

    swapchain_->Present(syncInterval, flags);
}

bool DX12Swapchain::Resize(UINT width, UINT height) {
    if (!initialized_) {
        return false;
    }

    NEXT_LOG_INFO("Resizing swapchain to %ux%u", width, height);

    // Flush current frame before resize
    // Note: Caller should have already waited for GPU

    // Release old render targets
    renderTargets_.clear();

    // Check tearing support
    UINT allowTearing = 0;
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    HRESULT hr = swapchain_->GetDesc(&swapChainDesc);

    if (SUCCEEDED(hr)) {
        // Check if tearing is supported
        // (Fullscreen modes typically support tearing)
        BOOL supportTearing = FALSE;
        swapChainDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING ?
            supportTearing = TRUE : supportTearing;

        // Only use ALLOW_TEARING if tearing is actually supported
        if (supportTearing) {
            allowTearing = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            NEXT_LOG_DEBUG("Using tearing mode for swapchain resize");
        } else {
            NEXT_LOG_DEBUG("Tearing not supported, using flip discard");
        }
    }

    // Resize swapchain
    hr = swapchain_->ResizeBuffers(
        0,  // Default to current buffer count
        width,
        height,
        format_,
        allowTearing
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to resize swapchain: 0x%X", hr);
        return false;
    }

    width_ = width;
    height_ = height;

    // Recreate render targets
    for (UINT i = 0; i < bufferCount_; ++i) {
        Microsoft::WRL::ComPtr<ID3D12Resource> rt;
        hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&rt));
        if (SUCCEEDED(hr)) {
            renderTargets_.push_back(rt);
        }
    }

    // Recreate RTV descriptor heap and views
    if (!CreateRenderTargetViews()) {
        NEXT_LOG_ERROR("Failed to recreate render target views after resize");
        return false;
    }

    NEXT_LOG_INFO("Swapchain resized successfully, RTV recreated");
    return true;
}

bool DX12Swapchain::CreateRenderTargetViews() {
    if (!device_ || !device_->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for RTV creation");
        return false;
    }

    NEXT_LOG_DEBUG("Creating RTV descriptor heap...");

    // Create RTV heap
    if (!rtvHeap_.Initialize(device_, bufferCount_)) {
        NEXT_LOG_ERROR("Failed to create RTV heap");
        return false;
    }

    // Create RTV for each swapchain buffer
    for (UINT i = 0; i < bufferCount_; ++i) {
        if (!renderTargets_[i]) {
            NEXT_LOG_ERROR("Invalid render target at index %u", i);
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_.GetCPUDescriptorHandle(i);

        // Create render target view
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = format_;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.Texture2D.PlaneSlice = 0;

        device_->GetDevice()->CreateRenderTargetView(
            renderTargets_[i].Get(),
            &rtvDesc,
            rtvHandle
        );

        NEXT_LOG_DEBUG("  Created RTV for buffer %u", i);
    }

    NEXT_LOG_INFO("Render target views created successfully (%u RTVs)", bufferCount_);
    return true;
}

} // namespace Next
