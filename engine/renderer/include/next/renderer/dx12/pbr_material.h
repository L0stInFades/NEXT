#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include "next/renderer/dx12/texture.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <string>

namespace Next {

// Forward declarations
class DX12Device;
class DX12DescriptorHeap;

// PBR Material Parameters (physically based rendering)
// Design principles:
// - Sustainable Experimental: Easy to add/remove texture channels
// - Advanced: Full PBR workflow support (Albedo, Normal, Metallic, Roughness, AO, Emissive)
// - Refactor Friendly: Clear separation of texture and scalar parameters
struct PBRMaterialParameters {
    // Base color (RGB) [0, 1]
    float albedo[3] = {1.0f, 1.0f, 1.0f};

    // Metallic workflow [0, 1]
    // 0.0 = dielectric (non-metal), 1.0 = conductor (metal)
    float metallic = 0.0f;

    // Surface roughness [0, 1]
    // 0.0 = smooth (mirror), 1.0 = matte (diffuse)
    float roughness = 0.5f;

    // Ambient occlusion [0, 1]
    // 0.0 = full occlusion, 1.0 = no occlusion
    float ao = 1.0f;

    // Emissive color (RGB) [0, ∞]
    float emissive[3] = {0.0f, 0.0f, 0.0f};

    // Normal map scale [0, 1]
    float normalScale = 1.0f;

    // Alpha cutoff (for alpha test) [0, 1]
    float alphaCutoff = 0.5f;

    // Two-sided rendering
    bool twoSided = false;

    // Padding for shader alignment (16-byte boundary)
    float _padding = 0.0f;
};

// PBR Material Asset with texture support
class PBRMaterialAsset {
public:
    PBRMaterialAsset();
    ~PBRMaterialAsset();

    // Initialize material (without textures - use scalar parameters only)
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap);

    // Load textures from files
    bool LoadAlbedoTexture(const wchar_t* filename, ID3D12CommandQueue* commandQueue);
    bool LoadNormalTexture(const wchar_t* filename, ID3D12CommandQueue* commandQueue);
    bool LoadMetallicTexture(const wchar_t* filename, ID3D12CommandQueue* commandQueue);
    bool LoadRoughnessTexture(const wchar_t* filename, ID3D12CommandQueue* commandQueue);
    bool LoadAOTexture(const wchar_t* filename, ID3D12CommandQueue* commandQueue);
    bool LoadEmissiveTexture(const wchar_t* filename, ID3D12CommandQueue* commandQueue);

    // Set material parameters
    void SetParameters(const PBRMaterialParameters& params);
    const PBRMaterialParameters& GetParameters() const { return params_; }

    // Texture access
    bool HasAlbedoTexture() const { return albedoTexture_ != nullptr; }
    bool HasNormalTexture() const { return normalTexture_ != nullptr; }
    bool HasMetallicTexture() const { return metallicTexture_ != nullptr; }
    bool HasRoughnessTexture() const { return roughnessTexture_ != nullptr; }
    bool HasAOTexture() const { return aoTexture_ != nullptr; }
    bool HasEmissiveTexture() const { return emissiveTexture_ != nullptr; }

    // Get GPU descriptor handles (for shader binding)
    D3D12_GPU_DESCRIPTOR_HANDLE GetAlbedoSRV() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetNormalSRV() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetMetallicSRV() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetRoughnessSRV() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetAOSRV() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetEmissiveSRV() const;

    // Check if texture is loaded
    bool IsInitialized() const { return initialized_; }

    // Cleanup
    void Shutdown();

private:
    // Textures (owned by material)
    DX12Texture* albedoTexture_;
    DX12Texture* normalTexture_;
    DX12Texture* metallicTexture_;
    DX12Texture* roughnessTexture_;
    DX12Texture* aoTexture_;
    DX12Texture* emissiveTexture_;

    // Material parameters
    PBRMaterialParameters params_;

    // Device and descriptor heap
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // SRV heap indices (for binding to shaders)
    uint32_t albedoSRVIndex_;
    uint32_t normalSRVIndex_;
    uint32_t metallicSRVIndex_;
    uint32_t roughnessSRVIndex_;
    uint32_t aoSRVIndex_;
    uint32_t emissiveSRVIndex_;

    bool initialized_;
};

} // namespace Next
