#pragma once

#include "next/renderer/dx12/device.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace Next {

// Constant Buffer for passing uniforms to shaders
// Design principles:
// - 16-byte aligned for HLSL compatibility
// - Update frequency optimized (map/unmap for dynamic data)
// - RAII for automatic cleanup
class DX12ConstantBuffer {
public:
    DX12ConstantBuffer();
    ~DX12ConstantBuffer();

    // Initialize constant buffer
    // size: Must be multiple of 16 bytes (HLSL alignment requirement)
    bool Initialize(DX12Device* device, size_t size);

    void Shutdown();

    // Update buffer data (CPU side)
    bool UpdateData(const void* data, size_t size);

    // GPU access
    ID3D12Resource* GetResource() const { return resource_.Get(); }
    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const {
        return resource_ ? resource_->GetGPUVirtualAddress() : 0;
    }

    size_t GetSize() const { return size_; }
    bool IsInitialized() const { return initialized_; }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    size_t size_;
    bool initialized_;

    // Constant buffers must be 256-byte aligned for hardware requirements
    static constexpr size_t CONSTANT_BUFFER_ALIGNMENT = 256;
};

} // namespace Next
