#include "next/renderer/dx12/pbr_material.h"
#include "next/foundation/logger.h"

namespace Next {

PBRMaterialAsset::PBRMaterialAsset()
    : albedoTexture_(nullptr)
    , normalTexture_(nullptr)
    , metallicTexture_(nullptr)
    , roughnessTexture_(nullptr)
    , aoTexture_(nullptr)
    , emissiveTexture_(nullptr)
    , device_(nullptr)
    , srvHeap_(nullptr)
    , albedoSRVIndex_(0)
    , normalSRVIndex_(0)
    , metallicSRVIndex_(0)
    , roughnessSRVIndex_(0)
    , aoSRVIndex_(0)
    , emissiveSRVIndex_(0)
    , initialized_(false) {
}

PBRMaterialAsset::~PBRMaterialAsset() {
    Shutdown();
}

bool PBRMaterialAsset::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or SRV heap for PBR material");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;

    // Initialize default parameters
    params_ = PBRMaterialParameters();

    initialized_ = true;
    NEXT_LOG_INFO("PBR material initialized");
    return true;
}

bool PBRMaterialAsset::LoadAlbedoTexture(const wchar_t* filename, ID3D12CommandQueue* commandQueue) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot load texture: material not initialized");
        return false;
    }

    albedoTexture_ = new DX12Texture();
    if (!albedoTexture_->Initialize(device_, srvHeap_)) {
        NEXT_LOG_ERROR("Failed to initialize albedo texture");
        delete albedoTexture_;
        albedoTexture_ = nullptr;
        return false;
    }

    if (!albedoTexture_->LoadFromFile(filename, commandQueue)) {
        NEXT_LOG_ERROR("Failed to load albedo texture: %ls", filename);
        delete albedoTexture_;
        albedoTexture_ = nullptr;
        return false;
    }

    NEXT_LOG_INFO("Loaded albedo texture: %ls (%ux%u)", filename,
                  albedoTexture_->GetWidth(), albedoTexture_->GetHeight());
    return true;
}

bool PBRMaterialAsset::LoadNormalTexture(const wchar_t* filename, ID3D12CommandQueue* commandQueue) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot load texture: material not initialized");
        return false;
    }

    normalTexture_ = new DX12Texture();
    if (!normalTexture_->Initialize(device_, srvHeap_)) {
        NEXT_LOG_ERROR("Failed to initialize normal texture");
        delete normalTexture_;
        normalTexture_ = nullptr;
        return false;
    }

    if (!normalTexture_->LoadFromFile(filename, commandQueue)) {
        NEXT_LOG_ERROR("Failed to load normal texture: %ls", filename);
        delete normalTexture_;
        normalTexture_ = nullptr;
        return false;
    }

    NEXT_LOG_INFO("Loaded normal texture: %ls (%ux%u)", filename,
                  normalTexture_->GetWidth(), normalTexture_->GetHeight());
    return true;
}

bool PBRMaterialAsset::LoadMetallicTexture(const wchar_t* filename, ID3D12CommandQueue* commandQueue) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot load texture: material not initialized");
        return false;
    }

    metallicTexture_ = new DX12Texture();
    if (!metallicTexture_->Initialize(device_, srvHeap_)) {
        NEXT_LOG_ERROR("Failed to initialize metallic texture");
        delete metallicTexture_;
        metallicTexture_ = nullptr;
        return false;
    }

    if (!metallicTexture_->LoadFromFile(filename, commandQueue)) {
        NEXT_LOG_ERROR("Failed to load metallic texture: %ls", filename);
        delete metallicTexture_;
        metallicTexture_ = nullptr;
        return false;
    }

    NEXT_LOG_INFO("Loaded metallic texture: %ls (%ux%u)", filename,
                  metallicTexture_->GetWidth(), metallicTexture_->GetHeight());
    return true;
}

bool PBRMaterialAsset::LoadRoughnessTexture(const wchar_t* filename, ID3D12CommandQueue* commandQueue) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot load texture: material not initialized");
        return false;
    }

    roughnessTexture_ = new DX12Texture();
    if (!roughnessTexture_->Initialize(device_, srvHeap_)) {
        NEXT_LOG_ERROR("Failed to initialize roughness texture");
        delete roughnessTexture_;
        roughnessTexture_ = nullptr;
        return false;
    }

    if (!roughnessTexture_->LoadFromFile(filename, commandQueue)) {
        NEXT_LOG_ERROR("Failed to load roughness texture: %ls", filename);
        delete roughnessTexture_;
        roughnessTexture_ = nullptr;
        return false;
    }

    NEXT_LOG_INFO("Loaded roughness texture: %ls (%ux%u)", filename,
                  roughnessTexture_->GetWidth(), roughnessTexture_->GetHeight());
    return true;
}

bool PBRMaterialAsset::LoadAOTexture(const wchar_t* filename, ID3D12CommandQueue* commandQueue) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot load texture: material not initialized");
        return false;
    }

    aoTexture_ = new DX12Texture();
    if (!aoTexture_->Initialize(device_, srvHeap_)) {
        NEXT_LOG_ERROR("Failed to initialize AO texture");
        delete aoTexture_;
        aoTexture_ = nullptr;
        return false;
    }

    if (!aoTexture_->LoadFromFile(filename, commandQueue)) {
        NEXT_LOG_ERROR("Failed to load AO texture: %ls", filename);
        delete aoTexture_;
        aoTexture_ = nullptr;
        return false;
    }

    NEXT_LOG_INFO("Loaded AO texture: %ls (%ux%u)", filename,
                  aoTexture_->GetWidth(), aoTexture_->GetHeight());
    return true;
}

bool PBRMaterialAsset::LoadEmissiveTexture(const wchar_t* filename, ID3D12CommandQueue* commandQueue) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot load texture: material not initialized");
        return false;
    }

    emissiveTexture_ = new DX12Texture();
    if (!emissiveTexture_->Initialize(device_, srvHeap_)) {
        NEXT_LOG_ERROR("Failed to initialize emissive texture");
        delete emissiveTexture_;
        emissiveTexture_ = nullptr;
        return false;
    }

    if (!emissiveTexture_->LoadFromFile(filename, commandQueue)) {
        NEXT_LOG_ERROR("Failed to load emissive texture: %ls", filename);
        delete emissiveTexture_;
        emissiveTexture_ = nullptr;
        return false;
    }

    NEXT_LOG_INFO("Loaded emissive texture: %ls (%ux%u)", filename,
                  emissiveTexture_->GetWidth(), emissiveTexture_->GetHeight());
    return true;
}

void PBRMaterialAsset::SetParameters(const PBRMaterialParameters& params) {
    params_ = params;
}

D3D12_GPU_DESCRIPTOR_HANDLE PBRMaterialAsset::GetAlbedoSRV() const {
    return albedoTexture_ ? albedoTexture_->GetGPUDescriptorHandle() : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_GPU_DESCRIPTOR_HANDLE PBRMaterialAsset::GetNormalSRV() const {
    return normalTexture_ ? normalTexture_->GetGPUDescriptorHandle() : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_GPU_DESCRIPTOR_HANDLE PBRMaterialAsset::GetMetallicSRV() const {
    return metallicTexture_ ? metallicTexture_->GetGPUDescriptorHandle() : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_GPU_DESCRIPTOR_HANDLE PBRMaterialAsset::GetRoughnessSRV() const {
    return roughnessTexture_ ? roughnessTexture_->GetGPUDescriptorHandle() : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_GPU_DESCRIPTOR_HANDLE PBRMaterialAsset::GetAOSRV() const {
    return aoTexture_ ? aoTexture_->GetGPUDescriptorHandle() : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_GPU_DESCRIPTOR_HANDLE PBRMaterialAsset::GetEmissiveSRV() const {
    return emissiveTexture_ ? emissiveTexture_->GetGPUDescriptorHandle() : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

void PBRMaterialAsset::Shutdown() {
    if (albedoTexture_) {
        albedoTexture_->Shutdown();
        delete albedoTexture_;
        albedoTexture_ = nullptr;
    }

    if (normalTexture_) {
        normalTexture_->Shutdown();
        delete normalTexture_;
        normalTexture_ = nullptr;
    }

    if (metallicTexture_) {
        metallicTexture_->Shutdown();
        delete metallicTexture_;
        metallicTexture_ = nullptr;
    }

    if (roughnessTexture_) {
        roughnessTexture_->Shutdown();
        delete roughnessTexture_;
        roughnessTexture_ = nullptr;
    }

    if (aoTexture_) {
        aoTexture_->Shutdown();
        delete aoTexture_;
        aoTexture_ = nullptr;
    }

    if (emissiveTexture_) {
        emissiveTexture_->Shutdown();
        delete emissiveTexture_;
        emissiveTexture_ = nullptr;
    }

    device_ = nullptr;
    srvHeap_ = nullptr;
    initialized_ = false;

    NEXT_LOG_INFO("PBR material shutdown complete");
}

} // namespace Next
