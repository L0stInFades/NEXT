#include "next/renderer/dx12/depth_buffer.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include "next/foundation/logger.h"

namespace Next {

DX12DepthBuffer::DX12DepthBuffer()
    : dsvHandle_()
    , width_(0)
    , height_(0)
    , format_(DXGI_FORMAT_D32_FLOAT)
    , device_(nullptr)
    , dsvHeap_(nullptr)
    , initialized_(false) {
}

DX12DepthBuffer::~DX12DepthBuffer() {
    Shutdown();
}

bool DX12DepthBuffer::Initialize(DX12Device* device, DX12DescriptorHeap* dsvHeap,
                                  uint32_t width, uint32_t height,
                                  DXGI_FORMAT format) {
    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for depth buffer");
        return false;
    }

    if (!dsvHeap) {
        NEXT_LOG_ERROR("Invalid DSV heap for depth buffer");
        return false;
    }

    NEXT_LOG_INFO("Creating depth buffer: %ux%u, format=%u", width, height, format);

    device_ = device;
    dsvHeap_ = dsvHeap;
    width_ = width;
    height_ = height;
    format_ = format;

    // Create depth texture resource
    if (!CreateDepthTexture()) {
        NEXT_LOG_ERROR("Failed to create depth texture");
        return false;
    }

    // Create DSV descriptor
    if (!CreateDSVDescriptor()) {
        NEXT_LOG_ERROR("Failed to create DSV descriptor");
        resource_.Reset();
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("Depth buffer created successfully");
    return true;
}

bool DX12DepthBuffer::CreateDepthTexture() {
    // Describe depth texture
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;  // Depth buffer only needs 1 mip level
    desc.Format = format_;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    // Create optimized clear value
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = format_;
    clearValue.DepthStencil.Depth = 1.0f;  // Far plane
    clearValue.DepthStencil.Stencil = 0;

    // Create heap properties (default heap for depth buffer)
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    // Create resource in DEPTH_WRITE state
    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&resource_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create depth texture: 0x%X", hr);
        return false;
    }

    NEXT_LOG_DEBUG("Depth texture created: %ux%u", width_, height_);
    return true;
}

bool DX12DepthBuffer::CreateDSVDescriptor() {
    // Allocate descriptor from DSV heap (use index 0)
    dsvHandle_ = dsvHeap_->GetCPUDescriptorHandle(0);

    // Create DSV description
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = format_;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    // Create DSV
    device_->GetDevice()->CreateDepthStencilView(
        resource_.Get(),
        &dsvDesc,
        dsvHandle_
    );

    NEXT_LOG_DEBUG("DSV descriptor created");
    return true;
}

void DX12DepthBuffer::Shutdown() {
    resource_.Reset();
    dsvHandle_.ptr = 0;
    width_ = 0;
    height_ = 0;
    device_ = nullptr;
    dsvHeap_ = nullptr;
    initialized_ = false;
}

bool DX12DepthBuffer::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot resize uninitialized depth buffer");
        return false;
    }

    NEXT_LOG_INFO("Resizing depth buffer: %ux%u -> %ux%u", width_, height_, width, height);

    // Only resize if size actually changed
    if (width_ == width && height_ == height) {
        NEXT_LOG_DEBUG("Depth buffer size unchanged, skipping resize");
        return true;
    }

    // Release old resources but keep device and heap pointers
    if (resource_) {
        resource_.Reset();
    }

    // Update size
    width_ = width;
    height_ = height;

    // Recreate depth texture
    if (!CreateDepthTexture()) {
        NEXT_LOG_ERROR("Failed to recreate depth texture");
        return false;
    }

    // Recreate DSV descriptor
    if (!CreateDSVDescriptor()) {
        NEXT_LOG_ERROR("Failed to recreate DSV descriptor");
        resource_.Reset();
        return false;
    }

    NEXT_LOG_INFO("Depth buffer resized successfully to %ux%u", width, height);
    return true;
}

void DX12DepthBuffer::Clear(ID3D12GraphicsCommandList* commandList, float depthValue, uint8_t stencilValue) {
    if (!initialized_ || !resource_) {
        NEXT_LOG_ERROR("Cannot clear uninitialized depth buffer");
        return;
    }

    // Clear depth stencil view
    commandList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, depthValue, stencilValue, 0, nullptr);
}

} // namespace Next
