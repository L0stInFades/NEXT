#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/buffer.h"
#include "next/renderer/dx12/shader.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

namespace Next {

// Forward declarations
class DX12Device;
class DX12DescriptorHeap;

// Mesh Shader Pass (DX12U Feature Level 12.2)
// Design principles:
// - Sustainable Experimental: Easy to experiment with different LOD strategies
// - Advanced: GPU-driven culling and LOD (similar to UE5 Nanite, RAGE procedural geometry)
// - Refactor Friendly: Clear separation between amplification and mesh shaders
class MeshShaderPass {
public:
    MeshShaderPass();
    ~MeshShaderPass();

    // Initialize mesh shader pipeline
    bool Initialize(DX12Device* device);

    // Create amplification and mesh shaders
    bool LoadShaders(const char* amplificationShaderPath, const char* meshShaderPath,
                     const char* pixelShaderPath);

    // Create pipeline state. If no root signature is supplied, the pass creates an empty one.
    bool CreatePipelineState(ID3D12RootSignature* rootSignature = nullptr);

    // Render with mesh shaders
    void Render(ID3D12GraphicsCommandList* commandList, uint32_t instanceCount);

    // Cleanup
    void Shutdown();

    // Get pipeline state
    ID3D12PipelineState* GetPipelineState() const { return pipelineState_.Get(); }

    bool IsInitialized() const { return initialized_; }

private:
    bool CreateDefaultRootSignature();

    DX12Device* device_;

    // Shaders
    DX12Shader* amplificationShader_;
    DX12Shader* meshShader_;
    DX12Shader* pixelShader_;

    // Pipeline state
    ID3D12RootSignature* rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> ownedRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    bool initialized_;
    bool dispatchEvidenceLogged_;
};

} // namespace Next
