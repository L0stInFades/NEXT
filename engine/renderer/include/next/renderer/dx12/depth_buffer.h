#pragma once

#include "next/renderer/dx12/device.h"
#include <d3d12.h>
#include <wrl/client.h>

namespace Next {

// Forward declaration
class DX12DescriptorHeap;

// Depth Buffer for 3D rendering
// Design principles:
// - Automatic DSV heap management
// - Resize support (for window resize)
// - Resource state tracking
class DX12DepthBuffer {
public:
    DX12DepthBuffer();
    ~DX12DepthBuffer();

    // Create depth buffer with specified dimensions
    bool Initialize(DX12Device* device, DX12DescriptorHeap* dsvHeap,
                    uint32_t width, uint32_t height,
                    DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT);

    void Shutdown();

    // Resize depth buffer (for window resize)
    bool Resize(uint32_t width, uint32_t height);

    // Resource access
    ID3D12Resource* GetResource() const { return resource_.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const { return dsvHandle_; }

    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    DXGI_FORMAT GetFormat() const { return format_; }
    bool IsInitialized() const { return initialized_; }

    // Clear depth buffer to value (1.0 = far plane)
    void Clear(ID3D12GraphicsCommandList* commandList, float depthValue = 1.0f, uint8_t stencilValue = 0);

private:
    bool CreateDepthTexture();
    bool CreateDSVDescriptor();

    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_;

    uint32_t width_;
    uint32_t height_;
    DXGI_FORMAT format_;

    DX12Device* device_;
    DX12DescriptorHeap* dsvHeap_;

    bool initialized_;
};

} // namespace Next
