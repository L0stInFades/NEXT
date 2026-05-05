#pragma once

#include "next/renderer/dx12/device.h"
#include <d3d12.h>
#include <wrl/client.h>

namespace Next {

// DX12 Root Signature Wrapper
class DX12RootSignature {
public:
    DX12RootSignature();
    ~DX12RootSignature();

    // Initialization
    bool Initialize(DX12Device* device);
    void Shutdown();

    // Root Signature Access
    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }
    ID3D12RootSignature** GetAddressOf() { return rootSignature_.GetAddressOf(); }
    void SetRootSignature(ID3D12RootSignature* rootSig) {
        rootSignature_ = rootSig;
        initialized_ = rootSig != nullptr;
    }

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    bool initialized_;
};

} // namespace Next
