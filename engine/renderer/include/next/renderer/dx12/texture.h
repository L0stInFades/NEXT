#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <string>

namespace Next {

// Forward declaration
class DX12CommandQueue;

// DX12 Texture Wrapper with WIC image loading support
class DX12Texture {
public:
    DX12Texture();
    ~DX12Texture();

    // Initialization
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap);
    bool LoadFromFile(const wchar_t* filename, ID3D12CommandQueue* commandQueue);
    bool LoadFromFile(const wchar_t* filename, ID3D12CommandQueue* commandQueue, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor);
    void Shutdown();

    // Synchronization helper - wait for texture upload to complete
    void WaitForUpload(ID3D12CommandQueue* commandQueue);

    // Texture Access
    ID3D12Resource* GetResource() const { return texture_.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle() const { return gpuDescriptorHandle_; }

    // Texture Info
    UINT GetWidth() const { return width_; }
    UINT GetHeight() const { return height_; }
    UINT GetMipLevels() const { return mipLevels_; }
    DXGI_FORMAT GetFormat() const { return format_; }

private:
    // WIC Image Loading
    bool LoadImageFromWIC(const wchar_t* filename, std::vector<uint8_t>& textureData,
                         UINT& width, UINT& height, DXGI_FORMAT& format);

    // Create texture resource and upload to GPU
    bool CreateTextureResource(const void* data, UINT width, UINT height, DXGI_FORMAT format, ID3D12CommandQueue* commandQueue);
    bool CreateShaderResourceView(DXGI_FORMAT format);
    bool CreateShaderResourceView(DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor);

    Microsoft::WRL::ComPtr<ID3D12Resource> texture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> textureUpload_;

    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle_;

    UINT width_;
    UINT height_;
    UINT mipLevels_;
    DXGI_FORMAT format_;

    bool initialized_;
};

} // namespace Next
