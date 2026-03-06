#include "next/renderer/dx12/constant_buffer.h"
#include "next/foundation/logger.h"
#include <cstring>

namespace Next {

DX12ConstantBuffer::DX12ConstantBuffer()
    : size_(0), initialized_(false) {
}

DX12ConstantBuffer::~DX12ConstantBuffer() {
    Shutdown();
}

bool DX12ConstantBuffer::Initialize(DX12Device* device, size_t size) {
    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for constant buffer");
        return false;
    }

    // Align size to 256 bytes (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)
    // Hardware requirement for constant buffers
    size_t alignedSize = (size + CONSTANT_BUFFER_ALIGNMENT - 1)
                        / CONSTANT_BUFFER_ALIGNMENT * CONSTANT_BUFFER_ALIGNMENT;

    NEXT_LOG_DEBUG("Creating constant buffer: requested=%zu bytes, aligned=%zu bytes",
                   size, alignedSize);

    size_ = alignedSize;

    // Create upload heap resource for constant buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = size_;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create constant buffer: 0x%X", hr);
        return false;
    }

    // Map the buffer and initialize to zero
    void* pData = nullptr;
    D3D12_RANGE readRange = {0, 0};

    hr = resource_->Map(0, &readRange, &pData);
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to map constant buffer: 0x%X", hr);
        resource_.Reset();
        return false;
    }

    // Initialize to zero
    memset(pData, 0, size_);

    // Unmap (will be remapped during UpdateData)
    resource_->Unmap(0, nullptr);

    initialized_ = true;
    NEXT_LOG_INFO("Constant buffer created successfully (%zu bytes, aligned to %zu bytes)",
                 size, size_);
    return true;
}

bool DX12ConstantBuffer::UpdateData(const void* data, size_t size) {
    if (!initialized_ || !resource_) {
        NEXT_LOG_ERROR("Constant buffer not initialized");
        return false;
    }

    if (size > size_) {
        NEXT_LOG_ERROR("Data size (%zu) exceeds constant buffer size (%zu)", size, size_);
        return false;
    }

    // Map the buffer
    void* pData = nullptr;
    D3D12_RANGE readRange = {0, 0};  // We don't read from this resource on CPU

    HRESULT hr = resource_->Map(0, &readRange, &pData);
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to map constant buffer: 0x%X", hr);
        return false;
    }

    // Copy data
    memcpy(pData, data, size);

    // Unmap
    D3D12_RANGE writtenRange = {0, size};  // Specify the range we wrote to
    resource_->Unmap(0, &writtenRange);

    return true;
}

void DX12ConstantBuffer::Shutdown() {
    resource_.Reset();
    size_ = 0;
    initialized_ = false;
}

} // namespace Next
