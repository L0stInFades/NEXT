#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include "next/renderer/dx12/descriptor_allocator.h"
#include "next/renderer/dx12/light.h"
#include "next/renderer/dx12/texture.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace Next {

// PBR Material Wrapper
// Manages material parameters and texture bindings for PBR rendering
// Now uses proper descriptor allocation for multiple textures
class DX12Material {
public:
    DX12Material();
    ~DX12Material();

    // Initialization
    bool Initialize(DX12Device* device, DX12DescriptorHeapManager* heapManager);
    // Legacy initialization for backward compatibility with renderer
    bool Initialize(DX12Device* device, DX12CBVSRVUAVHeap* srvHeap);
    void Shutdown();

    // Material Parameters
    void SetAlbedo(const Vec3& albedo) { material_.albedo = albedo; }
    void SetMetallic(float metallic) { material_.metallic = metallic; }
    void SetRoughness(float roughness) { material_.setRoughness(roughness); }
    void SetAO(float ao) { material_.setAO(ao); }

    Vec3 GetAlbedo() const { return material_.albedo; }
    float GetMetallic() const { return material_.metallic; }
    float GetRoughness() const { return material_.roughness(); }
    float GetAO() const { return material_.ao(); }

    // Texture Loading
    bool LoadAlbedoMap(const wchar_t* filename, ID3D12CommandQueue* queue);
    bool LoadNormalMap(const wchar_t* filename, ID3D12CommandQueue* queue);
    bool LoadMetallicMap(const wchar_t* filename, ID3D12CommandQueue* queue);
    bool LoadRoughnessMap(const wchar_t* filename, ID3D12CommandQueue* queue);
    bool LoadAOMap(const wchar_t* filename, ID3D12CommandQueue* queue);

    // Material Access
    const PBRMaterial& GetMaterialData() const { return material_; }
    PBRMaterial& GetMaterialData() { return material_; }

    // Texture descriptor handles (for shader binding)
    D3D12_GPU_DESCRIPTOR_HANDLE GetAlbedoHandle() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetNormalHandle() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetMetallicHandle() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetRoughnessHandle() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetAOHandle() const;

    // Check if textures are loaded
    bool HasAlbedoMap() const { return material_.useAlbedoMap(); }
    bool HasNormalMap() const { return material_.useNormalMap(); }
    bool HasMetallicMap() const { return material_.useMetallicMap(); }
    bool HasRoughnessMap() const { return material_.useRoughnessMap(); }
    bool HasAOMap() const { return material_.useAOMap(); }

private:
    // Helper method to get SRV heap (supports both legacy and heap manager modes)
    DX12DescriptorHeap* GetSRVHeapForLoading();
    PBRMaterial material_;

    DX12Texture albedoMap_;
    DX12Texture normalMap_;
    DX12Texture metallicMap_;
    DX12Texture roughnessMap_;
    DX12Texture aoMap_;

    // Descriptor allocations for proper cleanup
    DescriptorAllocation albedoAllocation_;
    DescriptorAllocation normalAllocation_;
    DescriptorAllocation metallicAllocation_;
    DescriptorAllocation roughnessAllocation_;
    DescriptorAllocation aoAllocation_;

    DX12Device* device_;
    DX12DescriptorHeapManager* heapManager_;
    DX12CBVSRVUAVHeap* legacyHeap_;  // For backward compatibility

    bool initialized_;
    bool useLegacyMode_;  // True if using legacy heap instead of heap manager
};

} // namespace Next
