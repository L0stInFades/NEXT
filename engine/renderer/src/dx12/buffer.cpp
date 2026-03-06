#include "next/renderer/dx12/buffer.h"
#include "next/foundation/logger.h"

namespace Next {

DX12Buffer::DX12Buffer()
    : size_(0), initialized_(false) {
}

DX12Buffer::~DX12Buffer() {
    Shutdown();
}

bool DX12Buffer::Initialize(
    DX12Device* device,
    size_t size,
    D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_FLAGS resourceFlags) {

    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for buffer");
        return false;
    }

    NEXT_LOG_DEBUG("Creating buffer (%zu bytes, Heap Type: %d)", size, heapType);

    size_ = size;

    // Describe buffer resource
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = resourceFlags;

    // Create upload heap resource
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = heapType;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    if (heapType == D3D12_HEAP_TYPE_UPLOAD) {
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    }

    D3D12_RESOURCE_STATES resourceState = (heapType == D3D12_HEAP_TYPE_UPLOAD)
        ? D3D12_RESOURCE_STATE_GENERIC_READ
        : D3D12_RESOURCE_STATE_COMMON;

    HRESULT hr = device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        resourceState,
        nullptr,
        IID_PPV_ARGS(&resource_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create buffer: 0x%X", hr);
        return false;
    }

    initialized_ = true;
    NEXT_LOG_DEBUG("Buffer created successfully");
    return true;
}

bool DX12Buffer::UploadData(const void* data, size_t size) {
    if (!initialized_ || !resource_) {
        NEXT_LOG_ERROR("Buffer not initialized");
        return false;
    }

    if (size > size_) {
        NEXT_LOG_ERROR("Data size (%zu) exceeds buffer size (%zu)", size, size_);
        return false;
    }

    // Map the buffer
    void* pData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };  // We don't intend to read from this resource on the CPU

    HRESULT hr = resource_->Map(0, &readRange, &pData);
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to map buffer: 0x%X", hr);
        return false;
    }

    // Copy data
    memcpy(pData, data, size);

    // Unmap
    resource_->Unmap(0, nullptr);

    // Often called every frame for dynamic buffers; keep at trace to avoid log spam.
    NEXT_LOG_TRACE("Uploaded %zu bytes to buffer", size);
    return true;
}

void DX12Buffer::Shutdown() {
    resource_.Reset();
    size_ = 0;
    initialized_ = false;
}

D3D12_VERTEX_BUFFER_VIEW DX12Buffer::GetVertexBufferView(UINT stride) const {
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = resource_->GetGPUVirtualAddress();
    vbv.SizeInBytes = (UINT)size_;
    vbv.StrideInBytes = stride;
    return vbv;
}

D3D12_INDEX_BUFFER_VIEW DX12Buffer::GetIndexBufferView(DXGI_FORMAT format) const {
    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = resource_->GetGPUVirtualAddress();
    ibv.SizeInBytes = (UINT)size_;
    ibv.Format = format;
    return ibv;
}

} // namespace Next
