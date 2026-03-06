#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/shader.h"
#include "next/renderer/dx12/root_signature.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

namespace Next {

// Input Element Description Helper
struct InputElementDesc {
    const char* semanticName;
    UINT semanticIndex;
    DXGI_FORMAT format;
    UINT inputSlot;
    UINT alignedByteOffset;
    D3D12_INPUT_CLASSIFICATION inputSlotClass;
    UINT instanceDataStepRate;

    D3D12_INPUT_ELEMENT_DESC ToD3D12() const {
        return {
            semanticName,
            semanticIndex,
            format,
            inputSlot,
            alignedByteOffset,
            inputSlotClass,
            instanceDataStepRate
        };
    }
};

// DX12 Pipeline State Object Wrapper
class DX12PipelineState {
public:
    DX12PipelineState();
    ~DX12PipelineState();

    // Initialization
    bool Initialize(
        DX12Device* device,
        DX12RootSignature* rootSignature,
        DX12Shader* vertexShader,
        DX12Shader* pixelShader,
        const std::vector<InputElementDesc>& inputLayout,
        DXGI_FORMAT renderTargetFormat,
        D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

    void Shutdown();

    // PSO Access
    ID3D12PipelineState* GetPSO() const { return pso_.Get(); }
    D3D12_PRIMITIVE_TOPOLOGY GetPrimitiveTopology() const { return primitiveTopology_; }

private:
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
    D3D12_PRIMITIVE_TOPOLOGY primitiveTopology_;
    bool initialized_;
};

} // namespace Next
