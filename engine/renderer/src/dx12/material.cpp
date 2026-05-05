#include "next/renderer/dx12/material.h"
#include "next/foundation/logger.h"

namespace Next {

DX12Material::DX12Material()
    : device_(nullptr), heapManager_(nullptr), initialized_(false) {
}

DX12Material::~DX12Material() {
    Shutdown();
}

bool DX12Material::Initialize(DX12Device* device, DX12DescriptorHeapManager* heapManager) {
    if (!device || !heapManager) {
        NEXT_LOG_ERROR("Invalid device or heap manager for material");
        return false;
    }

    if (!device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid D3D12 device for material");
        return false;
    }

    Shutdown();

    device_ = device;
    heapManager_ = heapManager;

    // Initialize descriptor allocations to empty
    albedoAllocation_ = DescriptorAllocation();
    normalAllocation_ = DescriptorAllocation();
    metallicAllocation_ = DescriptorAllocation();
    roughnessAllocation_ = DescriptorAllocation();
    aoAllocation_ = DescriptorAllocation();

    initialized_ = true;

    NEXT_LOG_DEBUG("PBR Material initialized with descriptor manager (Albedo: %.2f, %.2f, %.2f, Metallic: %.2f, Roughness: %.2f)",
                   material_.albedo.x, material_.albedo.y, material_.albedo.z,
                   material_.metallic, material_.roughness());

    return true;
}

bool DX12Material::LoadAlbedoMap(const wchar_t* filename, ID3D12CommandQueue* queue) {
    if (!initialized_ || !device_ || !heapManager_) {
        NEXT_LOG_ERROR("Material not initialized for albedo map loading");
        return false;
    }

    if (!filename || !queue) {
        NEXT_LOG_ERROR("Invalid albedo map load request");
        return false;
    }

    DX12DescriptorHeap* srvHeap = GetSRVHeapForLoading();
    if (!srvHeap) {
        NEXT_LOG_ERROR("No SRV heap available for albedo map");
        return false;
    }

    if (albedoAllocation_.count > 0) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, albedoAllocation_);
        albedoAllocation_ = DescriptorAllocation();
        albedoMap_.Shutdown();
        material_.setUseAlbedoMap(false);
    }

    albedoAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    if (albedoAllocation_.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate descriptor for albedo map");
        return false;
    }

    if (!albedoMap_.Initialize(device_, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize albedo map texture");
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, albedoAllocation_);
        albedoAllocation_ = DescriptorAllocation();
        return false;
    }

    if (!albedoMap_.LoadFromFile(filename, queue, albedoAllocation_.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to load albedo map: %ls", filename);
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, albedoAllocation_);
        albedoAllocation_ = DescriptorAllocation();
        return false;
    }

    material_.setUseAlbedoMap(true);
    NEXT_LOG_INFO("Loaded albedo map: %ls (descriptor offset: %u)", filename, albedoAllocation_.offset);
    return true;
}

// Helper method to get SRV heap for texture loading
DX12DescriptorHeap* DX12Material::GetSRVHeapForLoading() {
    if (!heapManager_) {
        return nullptr;
    }
    return heapManager_->GetHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

bool DX12Material::LoadNormalMap(const wchar_t* filename, ID3D12CommandQueue* queue) {
    if (!initialized_ || !device_ || !heapManager_) {
        NEXT_LOG_ERROR("Material not initialized for normal map loading");
        return false;
    }

    if (!filename || !queue) {
        NEXT_LOG_ERROR("Invalid normal map load request");
        return false;
    }

    DX12DescriptorHeap* srvHeap = GetSRVHeapForLoading();
    if (!srvHeap) {
        NEXT_LOG_ERROR("No SRV heap available for normal map");
        return false;
    }

    if (normalAllocation_.count > 0) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, normalAllocation_);
        normalAllocation_ = DescriptorAllocation();
        normalMap_.Shutdown();
        material_.setUseNormalMap(false);
    }

    normalAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    if (normalAllocation_.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate descriptor for normal map");
        return false;
    }

    if (!normalMap_.Initialize(device_, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize normal map texture");
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, normalAllocation_);
        normalAllocation_ = DescriptorAllocation();
        return false;
    }

    if (!normalMap_.LoadFromFile(filename, queue, normalAllocation_.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to load normal map: %ls", filename);
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, normalAllocation_);
        normalAllocation_ = DescriptorAllocation();
        return false;
    }

    material_.setUseNormalMap(true);
    NEXT_LOG_INFO("Loaded normal map: %ls (descriptor offset: %u)", filename, normalAllocation_.offset);
    return true;
}

bool DX12Material::LoadMetallicMap(const wchar_t* filename, ID3D12CommandQueue* queue) {
    if (!initialized_ || !device_ || !heapManager_) {
        NEXT_LOG_ERROR("Material not initialized for metallic map loading");
        return false;
    }

    if (!filename || !queue) {
        NEXT_LOG_ERROR("Invalid metallic map load request");
        return false;
    }

    DX12DescriptorHeap* srvHeap = GetSRVHeapForLoading();
    if (!srvHeap) {
        NEXT_LOG_ERROR("No SRV heap available for metallic map");
        return false;
    }

    if (metallicAllocation_.count > 0) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, metallicAllocation_);
        metallicAllocation_ = DescriptorAllocation();
        metallicMap_.Shutdown();
        material_.setUseMetallicMap(false);
    }

    metallicAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    if (metallicAllocation_.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate descriptor for metallic map");
        return false;
    }

    if (!metallicMap_.Initialize(device_, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize metallic map texture");
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, metallicAllocation_);
        metallicAllocation_ = DescriptorAllocation();
        return false;
    }

    if (!metallicMap_.LoadFromFile(filename, queue, metallicAllocation_.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to load metallic map: %ls", filename);
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, metallicAllocation_);
        metallicAllocation_ = DescriptorAllocation();
        return false;
    }

    material_.setUseMetallicMap(true);
    NEXT_LOG_INFO("Loaded metallic map: %ls (descriptor offset: %u)", filename, metallicAllocation_.offset);
    return true;
}

bool DX12Material::LoadRoughnessMap(const wchar_t* filename, ID3D12CommandQueue* queue) {
    if (!initialized_ || !device_ || !heapManager_) {
        NEXT_LOG_ERROR("Material not initialized for roughness map loading");
        return false;
    }

    if (!filename || !queue) {
        NEXT_LOG_ERROR("Invalid roughness map load request");
        return false;
    }

    DX12DescriptorHeap* srvHeap = GetSRVHeapForLoading();
    if (!srvHeap) {
        NEXT_LOG_ERROR("No SRV heap available for roughness map");
        return false;
    }

    if (roughnessAllocation_.count > 0) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, roughnessAllocation_);
        roughnessAllocation_ = DescriptorAllocation();
        roughnessMap_.Shutdown();
        material_.setUseRoughnessMap(false);
    }

    roughnessAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    if (roughnessAllocation_.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate descriptor for roughness map");
        return false;
    }

    if (!roughnessMap_.Initialize(device_, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize roughness map texture");
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, roughnessAllocation_);
        roughnessAllocation_ = DescriptorAllocation();
        return false;
    }

    if (!roughnessMap_.LoadFromFile(filename, queue, roughnessAllocation_.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to load roughness map: %ls", filename);
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, roughnessAllocation_);
        roughnessAllocation_ = DescriptorAllocation();
        return false;
    }

    material_.setUseRoughnessMap(true);
    NEXT_LOG_INFO("Loaded roughness map: %ls (descriptor offset: %u)", filename, roughnessAllocation_.offset);
    return true;
}

bool DX12Material::LoadAOMap(const wchar_t* filename, ID3D12CommandQueue* queue) {
    if (!initialized_ || !device_ || !heapManager_) {
        NEXT_LOG_ERROR("Material not initialized for AO map loading");
        return false;
    }

    if (!filename || !queue) {
        NEXT_LOG_ERROR("Invalid AO map load request");
        return false;
    }

    DX12DescriptorHeap* srvHeap = GetSRVHeapForLoading();
    if (!srvHeap) {
        NEXT_LOG_ERROR("No SRV heap available for AO map");
        return false;
    }

    if (aoAllocation_.count > 0) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, aoAllocation_);
        aoAllocation_ = DescriptorAllocation();
        aoMap_.Shutdown();
        material_.setUseAOMap(false);
    }

    aoAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    if (aoAllocation_.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate descriptor for AO map");
        return false;
    }

    if (!aoMap_.Initialize(device_, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize AO map texture");
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, aoAllocation_);
        aoAllocation_ = DescriptorAllocation();
        return false;
    }

    if (!aoMap_.LoadFromFile(filename, queue, aoAllocation_.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to load AO map: %ls", filename);
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, aoAllocation_);
        aoAllocation_ = DescriptorAllocation();
        return false;
    }

    material_.setUseAOMap(true);
    NEXT_LOG_INFO("Loaded AO map: %ls (descriptor offset: %u)", filename, aoAllocation_.offset);
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12Material::GetAlbedoHandle() const {
    return HasAlbedoMap() ? albedoAllocation_.gpuHandle : D3D12_GPU_DESCRIPTOR_HANDLE();
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12Material::GetNormalHandle() const {
    return HasNormalMap() ? normalAllocation_.gpuHandle : D3D12_GPU_DESCRIPTOR_HANDLE();
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12Material::GetMetallicHandle() const {
    return HasMetallicMap() ? metallicAllocation_.gpuHandle : D3D12_GPU_DESCRIPTOR_HANDLE();
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12Material::GetRoughnessHandle() const {
    return HasRoughnessMap() ? roughnessAllocation_.gpuHandle : D3D12_GPU_DESCRIPTOR_HANDLE();
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12Material::GetAOHandle() const {
    return HasAOMap() ? aoAllocation_.gpuHandle : D3D12_GPU_DESCRIPTOR_HANDLE();
}

void DX12Material::Shutdown() {
    if (!initialized_) {
        return;
    }

    // Release all descriptor allocations
    if (heapManager_) {
        if (albedoAllocation_.count > 0) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, albedoAllocation_);
        }
        if (normalAllocation_.count > 0) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, normalAllocation_);
        }
        if (metallicAllocation_.count > 0) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, metallicAllocation_);
        }
        if (roughnessAllocation_.count > 0) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, roughnessAllocation_);
        }
        if (aoAllocation_.count > 0) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, aoAllocation_);
        }
    }

    // Clear allocations
    albedoAllocation_ = DescriptorAllocation();
    normalAllocation_ = DescriptorAllocation();
    metallicAllocation_ = DescriptorAllocation();
    roughnessAllocation_ = DescriptorAllocation();
    aoAllocation_ = DescriptorAllocation();

    // Shutdown textures
    albedoMap_.Shutdown();
    normalMap_.Shutdown();
    metallicMap_.Shutdown();
    roughnessMap_.Shutdown();
    aoMap_.Shutdown();

    material_ = PBRMaterial();

    device_ = nullptr;
    heapManager_ = nullptr;
    initialized_ = false;
}

} // namespace Next
