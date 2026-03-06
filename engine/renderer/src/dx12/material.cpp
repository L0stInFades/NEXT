#include "next/renderer/dx12/material.h"
#include "next/foundation/logger.h"

namespace Next {

DX12Material::DX12Material()
    : device_(nullptr), heapManager_(nullptr), legacyHeap_(nullptr),
      initialized_(false), useLegacyMode_(false) {
}

DX12Material::~DX12Material() {
    Shutdown();
}

bool DX12Material::Initialize(DX12Device* device, DX12DescriptorHeapManager* heapManager) {
    if (!device || !heapManager) {
        NEXT_LOG_ERROR("Invalid device or heap manager for material");
        return false;
    }

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

// Legacy initialization for backward compatibility
bool DX12Material::Initialize(DX12Device* device, DX12CBVSRVUAVHeap* srvHeap) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or SRV heap for material");
        return false;
    }

    device_ = device;
    heapManager_ = nullptr;  // Not using heap manager in legacy mode

    // Initialize descriptor allocations to use fixed heap offsets
    // For legacy mode, we'll use fixed descriptor slots
    albedoAllocation_ = DescriptorAllocation();
    normalAllocation_ = DescriptorAllocation();
    metallicAllocation_ = DescriptorAllocation();
    roughnessAllocation_ = DescriptorAllocation();
    aoAllocation_ = DescriptorAllocation();

    // Set up fixed descriptor handles for legacy heap
    // Slot 0: Albedo, Slot 1: Normal, Slot 2: Metallic, Slot 3: Roughness, Slot 4: AO
    UINT descriptorSize = device->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = srvHeap->GetCPUDescriptorHandle(0);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = srvHeap->GetGPUDescriptorHandle(0);

    // Set up allocations with fixed slots (0-4)
    albedoAllocation_.cpuHandle = cpuBase;
    albedoAllocation_.gpuHandle.ptr = gpuBase.ptr + 0 * descriptorSize;
    albedoAllocation_.offset = 0;
    albedoAllocation_.count = 1;

    normalAllocation_.cpuHandle.ptr = cpuBase.ptr + 1 * descriptorSize;
    normalAllocation_.gpuHandle.ptr = gpuBase.ptr + 1 * descriptorSize;
    normalAllocation_.offset = 1;
    normalAllocation_.count = 1;

    metallicAllocation_.cpuHandle.ptr = cpuBase.ptr + 2 * descriptorSize;
    metallicAllocation_.gpuHandle.ptr = gpuBase.ptr + 2 * descriptorSize;
    metallicAllocation_.offset = 2;
    metallicAllocation_.count = 1;

    roughnessAllocation_.cpuHandle.ptr = cpuBase.ptr + 3 * descriptorSize;
    roughnessAllocation_.gpuHandle.ptr = gpuBase.ptr + 3 * descriptorSize;
    roughnessAllocation_.offset = 3;
    roughnessAllocation_.count = 1;

    aoAllocation_.cpuHandle.ptr = cpuBase.ptr + 4 * descriptorSize;
    aoAllocation_.gpuHandle.ptr = gpuBase.ptr + 4 * descriptorSize;
    aoAllocation_.offset = 4;
    aoAllocation_.count = 1;

    // Store the heap for texture initialization
    legacyHeap_ = srvHeap;
    useLegacyMode_ = true;
    initialized_ = true;

    NEXT_LOG_INFO("PBR Material initialized with legacy SRV heap (fixed slots 0-4)");

    return true;
}

bool DX12Material::LoadAlbedoMap(const wchar_t* filename, ID3D12CommandQueue* queue) {
    DX12DescriptorHeap* srvHeap = nullptr;

    if (useLegacyMode_) {
        // Use legacy heap (fixed slots already set up in Initialize)
        srvHeap = legacyHeap_;
        if (!srvHeap) {
            NEXT_LOG_ERROR("Legacy SRV heap is null");
            return false;
        }
    } else {
        // Allocate descriptor from heap manager
        srvHeap = heapManager_->GetHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (!srvHeap) {
            NEXT_LOG_ERROR("No CBV_SRV_UAV heap available in heap manager");
            return false;
        }

        // Allocate descriptor for albedo map
        albedoAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
        if (albedoAllocation_.count == 0) {
            NEXT_LOG_ERROR("Failed to allocate descriptor for albedo map");
            return false;
        }
    }

    if (!albedoMap_.Initialize(device_, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize albedo map texture");
        if (!useLegacyMode_) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, albedoAllocation_);
            albedoAllocation_ = DescriptorAllocation();
        }
        return false;
    }

    if (!albedoMap_.LoadFromFile(filename, queue, albedoAllocation_.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to load albedo map: %ls", filename);
        if (!useLegacyMode_) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, albedoAllocation_);
            albedoAllocation_ = DescriptorAllocation();
        }
        return false;
    }

    material_.setUseAlbedoMap(true);
    NEXT_LOG_INFO("Loaded albedo map: %ls (descriptor offset: %u)", filename, albedoAllocation_.offset);
    return true;
}

// Helper method to get SRV heap for texture loading
DX12DescriptorHeap* DX12Material::GetSRVHeapForLoading() {
    if (useLegacyMode_) {
        return legacyHeap_;
    } else {
        return heapManager_->GetHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}

bool DX12Material::LoadNormalMap(const wchar_t* filename, ID3D12CommandQueue* queue) {
    DX12DescriptorHeap* srvHeap = GetSRVHeapForLoading();
    if (!srvHeap) {
        NEXT_LOG_ERROR("No SRV heap available for normal map");
        return false;
    }

    if (!useLegacyMode_) {
        normalAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
        if (normalAllocation_.count == 0) {
            NEXT_LOG_ERROR("Failed to allocate descriptor for normal map");
            return false;
        }
    }

    if (!normalMap_.Initialize(device_, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize normal map texture");
        if (!useLegacyMode_) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, normalAllocation_);
            normalAllocation_ = DescriptorAllocation();
        }
        return false;
    }

    if (!normalMap_.LoadFromFile(filename, queue, normalAllocation_.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to load normal map: %ls", filename);
        if (!useLegacyMode_) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, normalAllocation_);
            normalAllocation_ = DescriptorAllocation();
        }
        return false;
    }

    material_.setUseNormalMap(true);
    NEXT_LOG_INFO("Loaded normal map: %ls (descriptor offset: %u)", filename, normalAllocation_.offset);
    return true;
}

bool DX12Material::LoadMetallicMap(const wchar_t* filename, ID3D12CommandQueue* queue) {
    DX12DescriptorHeap* srvHeap = GetSRVHeapForLoading();
    if (!srvHeap) {
        NEXT_LOG_ERROR("No SRV heap available for metallic map");
        return false;
    }

    if (!useLegacyMode_) {
        metallicAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
        if (metallicAllocation_.count == 0) {
            NEXT_LOG_ERROR("Failed to allocate descriptor for metallic map");
            return false;
        }
    }

    if (!metallicMap_.Initialize(device_, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize metallic map texture");
        if (!useLegacyMode_) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, metallicAllocation_);
            metallicAllocation_ = DescriptorAllocation();
        }
        return false;
    }

    if (!metallicMap_.LoadFromFile(filename, queue, metallicAllocation_.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to load metallic map: %ls", filename);
        if (!useLegacyMode_) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, metallicAllocation_);
            metallicAllocation_ = DescriptorAllocation();
        }
        return false;
    }

    material_.setUseMetallicMap(true);
    NEXT_LOG_INFO("Loaded metallic map: %ls (descriptor offset: %u)", filename, metallicAllocation_.offset);
    return true;
}

bool DX12Material::LoadRoughnessMap(const wchar_t* filename, ID3D12CommandQueue* queue) {
    DX12DescriptorHeap* srvHeap = GetSRVHeapForLoading();
    if (!srvHeap) {
        NEXT_LOG_ERROR("No SRV heap available for roughness map");
        return false;
    }

    if (!useLegacyMode_) {
        roughnessAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
        if (roughnessAllocation_.count == 0) {
            NEXT_LOG_ERROR("Failed to allocate descriptor for roughness map");
            return false;
        }
    }

    if (!roughnessMap_.Initialize(device_, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize roughness map texture");
        if (!useLegacyMode_) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, roughnessAllocation_);
            roughnessAllocation_ = DescriptorAllocation();
        }
        return false;
    }

    if (!roughnessMap_.LoadFromFile(filename, queue, roughnessAllocation_.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to load roughness map: %ls", filename);
        if (!useLegacyMode_) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, roughnessAllocation_);
            roughnessAllocation_ = DescriptorAllocation();
        }
        return false;
    }

    material_.setUseRoughnessMap(true);
    NEXT_LOG_INFO("Loaded roughness map: %ls (descriptor offset: %u)", filename, roughnessAllocation_.offset);
    return true;
}

bool DX12Material::LoadAOMap(const wchar_t* filename, ID3D12CommandQueue* queue) {
    DX12DescriptorHeap* srvHeap = GetSRVHeapForLoading();
    if (!srvHeap) {
        NEXT_LOG_ERROR("No SRV heap available for AO map");
        return false;
    }

    if (!useLegacyMode_) {
        aoAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
        if (aoAllocation_.count == 0) {
            NEXT_LOG_ERROR("Failed to allocate descriptor for AO map");
            return false;
        }
    }

    if (!aoMap_.Initialize(device_, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize AO map texture");
        if (!useLegacyMode_) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, aoAllocation_);
            aoAllocation_ = DescriptorAllocation();
        }
        return false;
    }

    if (!aoMap_.LoadFromFile(filename, queue, aoAllocation_.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to load AO map: %ls", filename);
        if (!useLegacyMode_) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, aoAllocation_);
            aoAllocation_ = DescriptorAllocation();
        }
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
