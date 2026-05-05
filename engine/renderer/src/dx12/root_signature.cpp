#include "next/renderer/dx12/root_signature.h"
#include "next/foundation/logger.h"

namespace Next {

DX12RootSignature::DX12RootSignature()
    : initialized_(false) {
}

DX12RootSignature::~DX12RootSignature() {
    Shutdown();
}

bool DX12RootSignature::Initialize(DX12Device* device) {
    Shutdown();

    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for root signature");
        return false;
    }

    NEXT_LOG_DEBUG("Creating root signature...");

    // Create empty root signature (for triangle demo, we don't need any parameters)
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 0;
    rootSignatureDesc.pParameters = nullptr;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // Serialize root signature
    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error
    );

    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("Root signature serialization failed: %s", (const char*)error->GetBufferPointer());
        } else {
            NEXT_LOG_ERROR("Root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    // Create root signature
    hr = device->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create root signature: 0x%X", hr);
        return false;
    }

    initialized_ = true;
    NEXT_LOG_DEBUG("Root signature created successfully");
    return true;
}

void DX12RootSignature::Shutdown() {
    rootSignature_.Reset();
    initialized_ = false;
}

} // namespace Next
