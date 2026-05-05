#include "next/renderer/dx12/texture.h"
#include "next/foundation/logger.h"
#include "next/renderer/dx12/fence.h"
#include <wincodec.h>
#include <comdef.h>
#include <vector>
#include <mutex>

namespace Next {

// Helper macro for WIC HRESULT handling
#define WIC_HR_ERROR(hr, msg) \
    if (FAILED(hr)) { \
        _com_error err(hr); \
        NEXT_LOG_ERROR("%s: %s (0x%X)", msg, err.ErrorMessage(), hr); \
        return false; \
    }

namespace {

void CreateCheckerboardTextureData(std::vector<uint8_t>& textureData,
                                   UINT& width,
                                   UINT& height,
                                   DXGI_FORMAT& format) {
    constexpr UINT kSize = 128;
    constexpr UINT kBytesPerPixel = 4;
    width = kSize;
    height = kSize;
    format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureData.resize(width * height * kBytesPerPixel);

    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const bool checker = (((x / 16) ^ (y / 16)) & 1u) != 0;
            const UINT offset = (y * width + x) * kBytesPerPixel;
            textureData[offset + 0] = checker ? 230 : 58;
            textureData[offset + 1] = checker ? 238 : 72;
            textureData[offset + 2] = checker ? 218 : 96;
            textureData[offset + 3] = 255;
        }
    }
}

} // namespace

DX12Texture::DX12Texture()
    : device_(nullptr), srvHeap_(nullptr), width_(0), height_(0),
      mipLevels_(1), format_(DXGI_FORMAT_R8G8B8A8_UNORM), initialized_(false) {
    gpuDescriptorHandle_.ptr = 0;
}

DX12Texture::~DX12Texture() {
    Shutdown();
}

bool DX12Texture::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or SRV heap for texture");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;

    initialized_ = true;
    return true;
}

bool DX12Texture::LoadFromFile(const wchar_t* filename, ID3D12CommandQueue* commandQueue) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Texture not initialized");
        return false;
    }

    (void)filename;
    (void)commandQueue;
    NEXT_LOG_ERROR("Texture loading requires an explicit descriptor allocation");
    return false;
}

bool DX12Texture::LoadFromFile(const wchar_t* filename, ID3D12CommandQueue* commandQueue, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Texture not initialized");
        return false;
    }

    if (!commandQueue) {
        NEXT_LOG_ERROR("Invalid command queue for texture loading");
        return false;
    }

    NEXT_LOG_INFO("Loading texture from file with pre-allocated descriptor: %ls", filename);

    // Load image data using WIC
    std::vector<uint8_t> textureData;
    UINT width, height;
    DXGI_FORMAT format;

    if (!LoadImageFromWIC(filename, textureData, width, height, format)) {
        NEXT_LOG_WARNING("Failed to load image from WIC; using procedural checkerboard texture");
        CreateCheckerboardTextureData(textureData, width, height, format);
    }

    width_ = width;
    height_ = height;
    format_ = format;

    // Create texture resource and upload to GPU
    if (!CreateTextureResource(textureData.data(), width, height, format, commandQueue)) {
        NEXT_LOG_ERROR("Failed to create texture resource");
        return false;
    }

    // Create shader resource view with provided descriptor
    if (!CreateShaderResourceView(format, cpuDescriptor)) {
        NEXT_LOG_ERROR("Failed to create shader resource view with provided descriptor");
        return false;
    }

    NEXT_LOG_INFO("Texture loaded successfully (%ux%u, format: %d)", width_, height_, format_);
    return true;
}

bool DX12Texture::LoadImageFromWIC(const wchar_t* filename,
                                   std::vector<uint8_t>& textureData,
                                   UINT& width, UINT& height,
                                   DXGI_FORMAT& format) {
    // Initialize WIC factory
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&wicFactory));
    WIC_HR_ERROR(hr, "Failed to create WIC factory");

    // Create decoder
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(filename, nullptr,
                                               GENERIC_READ,
                                               WICDecodeMetadataCacheOnDemand,
                                               &decoder);
    WIC_HR_ERROR(hr, "Failed to create WIC decoder from file");

    // Get first frame
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    WIC_HR_ERROR(hr, "Failed to get frame from decoder");

    // Get image size
    hr = frame->GetSize(&width, &height);
    WIC_HR_ERROR(hr, "Failed to get image size");

    // Convert to RGBA32 format
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(&converter);
    WIC_HR_ERROR(hr, "Failed to create format converter");

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                              WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeCustom);
    WIC_HR_ERROR(hr, "Failed to initialize format converter");

    // Allocate buffer for pixel data
    const UINT bytesPerPixel = 4; // RGBA
    const UINT rowPitch = width * bytesPerPixel;
    const UINT imageSize = rowPitch * height;
    textureData.resize(imageSize);

    // Copy pixel data
    hr = converter->CopyPixels(nullptr, rowPitch, imageSize, textureData.data());
    WIC_HR_ERROR(hr, "Failed to copy pixels from WIC converter");

    format = DXGI_FORMAT_R8G8B8A8_UNORM;
    return true;
}

bool DX12Texture::CreateTextureResource(const void* data, UINT width, UINT height, DXGI_FORMAT format, ID3D12CommandQueue* commandQueue) {
    if (!device_ || !device_->GetDevice() || !commandQueue || !data || width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid parameters for texture resource creation");
        return false;
    }

    texture_.Reset();
    textureUpload_.Reset();

    // Describe texture
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels = 1;
    textureDesc.Format = format;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    // Calculate required size for upload heap
    const UINT64 uploadBufferSize = ((UINT64)width * 4 + 255) & ~255ULL;  // 256-byte aligned
    const UINT64 totalSize = uploadBufferSize * height;

    // Create texture resource in default heap
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create texture resource: 0x%X", hr);
        return false;
    }

    // Create upload heap
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProps.CreationNodeMask = 1;
    uploadHeapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Alignment = 0;
    uploadDesc.Width = totalSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.SampleDesc.Quality = 0;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = device_->GetDevice()->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&textureUpload_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create texture upload heap: 0x%X", hr);
        return false;
    }

    // Map and copy data to upload heap
    void* pMappedData = nullptr;
    hr = textureUpload_->Map(0, nullptr, &pMappedData);
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to map upload heap: 0x%X", hr);
        return false;
    }

    const UINT8* pSrcData = reinterpret_cast<const UINT8*>(data);
    UINT8* pDstData = reinterpret_cast<UINT8*>(pMappedData);
    const UINT srcRowPitch = width * 4;  // 4 bytes per pixel

    for (UINT y = 0; y < height; ++y) {
        memcpy(pDstData + y * uploadBufferSize, pSrcData + y * srcRowPitch, srcRowPitch);
    }

    textureUpload_->Unmap(0, nullptr);

    // Create temporary command list for upload
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
    hr = device_->GetDevice()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&commandAllocator)
    );
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create command allocator for texture upload: 0x%X", hr);
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
    hr = device_->GetDevice()->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&commandList)
    );
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create command list for texture upload: 0x%X", hr);
        return false;
    }

    // Copy texture data
    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = texture_.Get();
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = textureUpload_.Get();
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint.Offset = 0;
    srcLocation.PlacedFootprint.Footprint.Format = format;
    srcLocation.PlacedFootprint.Footprint.Width = width;
    srcLocation.PlacedFootprint.Footprint.Height = height;
    srcLocation.PlacedFootprint.Footprint.Depth = 1;
    srcLocation.PlacedFootprint.Footprint.RowPitch = uploadBufferSize;

    commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

    // Resource barrier: Copy Dest -> Pixel Shader Resource
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // Execute command list
    hr = commandList->Close();
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to close texture upload command list: 0x%X", hr);
        return false;
    }

    ID3D12CommandList* lists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, lists);

    // Wait for upload to complete using proper fence mechanism
    if (!WaitForUpload(commandQueue)) {
        NEXT_LOG_ERROR("Texture upload did not complete");
        return false;
    }

    // Upload heap can be released after upload completes
    textureUpload_.Reset();

    return true;
}

bool DX12Texture::CreateShaderResourceView(DXGI_FORMAT format) {
    (void)format;
    NEXT_LOG_ERROR("Texture SRV creation requires an explicit descriptor allocation");
    return false;
}

bool DX12Texture::CreateShaderResourceView(DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor) {
    // Use provided CPU descriptor handle
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = cpuDescriptor;

    if (cpuHandle.ptr == 0) {
        NEXT_LOG_ERROR("Invalid CPU descriptor handle for SRV creation");
        return false;
    }

    if (!srvHeap_) {
        NEXT_LOG_ERROR("Invalid SRV heap for shader resource view");
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = srvHeap_->GetCPUDescriptorHandle(0);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = srvHeap_->GetGPUDescriptorHandle(0);
    UINT descriptorSize = srvHeap_->GetDescriptorSize();
    UINT descriptorCount = srvHeap_->GetNumDescriptors();

    if (descriptorSize == 0) {
        NEXT_LOG_ERROR("Invalid descriptor size for SRV creation");
        return false;
    }

    const SIZE_T heapStart = cpuBase.ptr;
    const SIZE_T heapEnd = heapStart + static_cast<SIZE_T>(descriptorSize) * descriptorCount;
    if (cpuHandle.ptr < heapStart || cpuHandle.ptr >= heapEnd) {
        NEXT_LOG_ERROR("SRV descriptor handle does not belong to the texture SRV heap");
        return false;
    }

    const SIZE_T byteOffset = cpuHandle.ptr - heapStart;
    if ((byteOffset % descriptorSize) != 0) {
        NEXT_LOG_ERROR("SRV descriptor handle is not aligned to descriptor size");
        return false;
    }

    UINT64 offset = byteOffset / descriptorSize;
    gpuDescriptorHandle_.ptr = gpuBase.ptr + offset * descriptorSize;

    // Create SRV description
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    device_->GetDevice()->CreateShaderResourceView(texture_.Get(), &srvDesc, cpuHandle);

    return true;
}

void DX12Texture::Shutdown() {
    if (!initialized_) {
        return;
    }

    texture_.Reset();
    textureUpload_.Reset();

    width_ = 0;
    height_ = 0;
    mipLevels_ = 1;
    format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
    gpuDescriptorHandle_.ptr = 0;

    device_ = nullptr;
    srvHeap_ = nullptr;
    initialized_ = false;
}

bool DX12Texture::WaitForUpload(ID3D12CommandQueue* commandQueue) {
    if (!commandQueue || !device_) {
        NEXT_LOG_ERROR("Invalid command queue or device for texture upload wait");
        return false;
    }

    static DX12Fence uploadFence;
    static DX12Device* uploadFenceDevice = nullptr;
    static std::mutex uploadFenceMutex;

    std::lock_guard<std::mutex> lock(uploadFenceMutex);

    // Reinitialize shared upload fence if device changed.
    if (uploadFenceDevice != device_) {
        uploadFence.Shutdown();
        if (!uploadFence.Initialize(device_)) {
            NEXT_LOG_ERROR("Failed to initialize shared texture upload fence");
            return false;
        }
        uploadFenceDevice = device_;
    }

    const uint64_t fenceValue = uploadFence.Signal(commandQueue);
    if (fenceValue == 0) {
        NEXT_LOG_ERROR("Failed to signal shared texture upload fence");
        return false;
    }

    // Block on the shared fence (no per-upload fence/event creation).
    if (!uploadFence.Wait(fenceValue)) {
        NEXT_LOG_ERROR("Texture upload fence wait failed (value: %llu)", fenceValue);
        return false;
    }

    NEXT_LOG_DEBUG("Texture upload completed (fence value: %llu)", fenceValue);
    return true;
}

} // namespace Next
