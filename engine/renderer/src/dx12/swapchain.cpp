#include "next/renderer/dx12/swapchain.h"
#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/command_queue.h"
#include "next/foundation/logger.h"
#include <dxgi1_5.h>
#include <windows.h>

namespace Next {

namespace {

bool SupportsPresentAllowTearing(IDXGIFactory4* factory) {
    if (!factory) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
    HRESULT hr = factory->QueryInterface(IID_PPV_ARGS(&factory5));
    if (FAILED(hr) || !factory5) {
        return false;
    }

    BOOL allowTearing = FALSE;
    hr = factory5->CheckFeatureSupport(
        DXGI_FEATURE_PRESENT_ALLOW_TEARING,
        &allowTearing,
        sizeof(allowTearing));
    return SUCCEEDED(hr) && allowTearing == TRUE;
}

} // namespace

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

    if (width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid swapchain dimensions: %ux%u", width, height);
        return false;
    }

    device_ = device;
    width_ = width;
    height_ = height;

    // Check present tearing support
    UINT allowTearing = 0;
    if (SupportsPresentAllowTearing(device->GetFactory())) {
        allowTearing = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        NEXT_LOG_DEBUG("Present allows tearing");
    }

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
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.Flags = allowTearing;
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

    // Disable automatic Alt+Enter fullscreen toggle to avoid window-state surprises.
    HRESULT hrWindow = device->GetFactory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(hrWindow)) {
        NEXT_LOG_WARNING("Failed to disable Alt+Enter window association: 0x%X", hrWindow);
    }

    // Query DX12U interface
    hr = swapChain.As(&swapchain_);
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to query IDXGISwapChain3 interface: 0x%X", hr);
        Shutdown();
        return false;
    }

    // Create render targets
    for (UINT i = 0; i < bufferCount_; ++i) {
        Microsoft::WRL::ComPtr<ID3D12Resource> rt;
        hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&rt));
        if (FAILED(hr) || !rt) {
            NEXT_LOG_ERROR("Failed to get swapchain buffer %u: 0x%X", i, hr);
            Shutdown();
            return false;
        }
        renderTargets_.push_back(rt);
    }

    // Create RTV descriptor heap and views
    if (!CreateRenderTargetViews()) {
        NEXT_LOG_ERROR("Failed to create render target views");
        Shutdown();
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
    width_ = 0;
    height_ = 0;
    initialized_ = false;
}

UINT DX12Swapchain::GetCurrentBackBufferIndex() const {
    if (!swapchain_) {
        return 0;
    }

    return swapchain_->GetCurrentBackBufferIndex();
}

ID3D12Resource* DX12Swapchain::GetRenderTarget(UINT index) const {
    if (!initialized_ || !swapchain_ || index >= renderTargets_.size()) {
        return nullptr;
    }

    return renderTargets_[index].Get();
}

ID3D12Resource* DX12Swapchain::GetCurrentRenderTarget() const {
    return GetRenderTarget(GetCurrentBackBufferIndex());
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Swapchain::GetRenderTargetView(UINT index) const {
    if (!initialized_ || index >= renderTargets_.size()) {
        return {};
    }

    return rtvHeap_.GetCPUDescriptorHandle(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Swapchain::GetCurrentRenderTargetView() const {
    return GetRenderTargetView(GetCurrentBackBufferIndex());
}

bool DX12Swapchain::Present(UINT syncInterval, UINT flags) {
    if (!swapchain_ || !initialized_) {
        return false;
    }

    HRESULT hr = swapchain_->Present(syncInterval, flags);
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Swapchain present failed: 0x%X", hr);
        if ((hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) &&
            device_ &&
            device_->GetDevice()) {
            HRESULT deviceReason = device_->GetDevice()->GetDeviceRemovedReason();
            NEXT_LOG_ERROR("DX12 device removed/reset reason: 0x%X", deviceReason);
        }
        return false;
    }

    return true;
}

bool DX12Swapchain::Resize(UINT width, UINT height) {
    if (!initialized_) {
        return false;
    }

    if (width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid swapchain resize dimensions: %ux%u", width, height);
        return false;
    }

    NEXT_LOG_INFO("Resizing swapchain to %ux%u", width, height);

    // Flush current frame before resize
    // Note: Caller should have already waited for GPU

    // Release old render targets
    renderTargets_.clear();

    // Check tearing support through DXGI factory.
    UINT allowTearing = 0;
    if (device_ && SupportsPresentAllowTearing(device_->GetFactory())) {
        allowTearing = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        NEXT_LOG_DEBUG("Using tearing mode for swapchain resize");
    } else {
        NEXT_LOG_DEBUG("Tearing not supported, using flip discard");
    }

    // Resize swapchain
    HRESULT hr = swapchain_->ResizeBuffers(
        0,  // Default to current buffer count
        width,
        height,
        format_,
        allowTearing
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to resize swapchain: 0x%X", hr);
        rtvHeap_.Shutdown();
        renderTargets_.clear();
        initialized_ = false;
        return false;
    }

    width_ = width;
    height_ = height;

    // Recreate render targets
    for (UINT i = 0; i < bufferCount_; ++i) {
        Microsoft::WRL::ComPtr<ID3D12Resource> rt;
        hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&rt));
        if (FAILED(hr) || !rt) {
            NEXT_LOG_ERROR("Failed to get resized swapchain buffer %u: 0x%X", i, hr);
            renderTargets_.clear();
            rtvHeap_.Shutdown();
            initialized_ = false;
            return false;
        }
        renderTargets_.push_back(rt);
    }

    // Recreate RTV descriptor heap and views
    if (!CreateRenderTargetViews()) {
        NEXT_LOG_ERROR("Failed to recreate render target views after resize");
        rtvHeap_.Shutdown();
        renderTargets_.clear();
        initialized_ = false;
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
    if (renderTargets_.size() < bufferCount_) {
        NEXT_LOG_ERROR("Cannot create RTVs: only %zu/%u swapchain buffers are available",
                       renderTargets_.size(),
                       bufferCount_);
        return false;
    }

    NEXT_LOG_DEBUG("Creating RTV descriptor heap...");

    // Create RTV heap
    rtvHeap_.Shutdown();
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
