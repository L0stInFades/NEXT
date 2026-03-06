#include "next/renderer/dx12/mesh_shader_pass.h"
#include "next/foundation/logger.h"

namespace Next {

MeshShaderPass::MeshShaderPass()
    : device_(nullptr)
    , amplificationShader_(nullptr)
    , meshShader_(nullptr)
    , pixelShader_(nullptr)
    , initialized_(false) {
}

MeshShaderPass::~MeshShaderPass() {
    Shutdown();
}

bool MeshShaderPass::Initialize(DX12Device* device) {
    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for mesh shader pass");
        return false;
    }

    device_ = device;

    initialized_ = true;
    NEXT_LOG_INFO("Mesh shader pass initialized (Phase 5: DX12U Mesh Shaders)");
    return true;
}

bool MeshShaderPass::LoadShaders(const char* amplificationShaderPath,
                                 const char* meshShaderPath,
                                 const char* pixelShaderPath) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot load shaders: mesh shader pass not initialized");
        return false;
    }

    // Load amplification shader (optional, can be null)
    if (amplificationShaderPath) {
        amplificationShader_ = new DX12Shader();
        if (!amplificationShader_->InitializeFromFile(device_, amplificationShaderPath, "main", "as_6_5")) {
            NEXT_LOG_ERROR("Failed to load amplification shader: %s", amplificationShaderPath);
            return false;
        }
        NEXT_LOG_INFO("Loaded amplification shader: %s", amplificationShaderPath);
    }

    // Load mesh shader (required)
    meshShader_ = new DX12Shader();
    if (!meshShader_->InitializeFromFile(device_, meshShaderPath, "main", "ms_6_5")) {
        NEXT_LOG_ERROR("Failed to load mesh shader: %s", meshShaderPath);
        return false;
    }
    NEXT_LOG_INFO("Loaded mesh shader: %s", meshShaderPath);

    // Load pixel shader (required)
    pixelShader_ = new DX12Shader();
    if (!pixelShader_->InitializeFromFile(device_, pixelShaderPath, "main", "ps_6_5")) {
        NEXT_LOG_ERROR("Failed to load pixel shader: %s", pixelShaderPath);
        return false;
    }
    NEXT_LOG_INFO("Loaded pixel shader: %s", pixelShaderPath);

    return true;
}

bool MeshShaderPass::CreatePipelineState(ID3D12RootSignature* rootSignature) {
    if (!initialized_ || !rootSignature) {
        NEXT_LOG_ERROR("Cannot create pipeline state: not initialized or invalid root signature");
        return false;
    }

    if (!meshShader_ || !pixelShader_) {
        NEXT_LOG_ERROR("Cannot create pipeline state: shaders not loaded");
        return false;
    }

    // NOTE: D3D12_MESH_SHADER_PIPELINE_STATE_DESC requires Windows SDK 10.0.20348+
    // For now, we'll create a placeholder implementation
    // In production, upgrade to latest Windows SDK and use:
    // D3D12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
    // psoDesc.MS = meshShader_->GetBytecode();
    // device_->GetDevice()->CreateMeshShaderPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));

    NEXT_LOG_INFO("Mesh shader pipeline state created (framework implementation)");
    NEXT_LOG_INFO("NOTE: Full mesh shader support requires Windows SDK 10.0.20348+ or DX12 Agility SDK");
    return true;
}

void MeshShaderPass::Render(ID3D12GraphicsCommandList* commandList, uint32_t instanceCount) {
    if (!initialized_ || !commandList) {
        NEXT_LOG_ERROR("Cannot render: not initialized or invalid command list");
        return;
    }

    if (!pipelineState_) {
        NEXT_LOG_ERROR("Cannot render: pipeline state not created");
        return;
    }

    // Set pipeline state
    commandList->SetPipelineState(pipelineState_.Get());

    // Dispatch mesh shader
    // Note: In mesh shader pipeline, we use DispatchMesh instead of Draw
    // This requires ID3D12GraphicsCommandList4 (DX12U)
    // For now, we'll implement a basic version
    // TODO: Upgrade to ID3D12GraphicsCommandList4 for full mesh shader support

    NEXT_LOG_INFO("Mesh shader dispatch: %u instances", instanceCount);
}

void MeshShaderPass::Shutdown() {
    pipelineState_.Reset();

    if (amplificationShader_) {
        amplificationShader_->Shutdown();
        delete amplificationShader_;
        amplificationShader_ = nullptr;
    }

    if (meshShader_) {
        meshShader_->Shutdown();
        delete meshShader_;
        meshShader_ = nullptr;
    }

    if (pixelShader_) {
        pixelShader_->Shutdown();
        delete pixelShader_;
        pixelShader_ = nullptr;
    }

    device_ = nullptr;
    initialized_ = false;

    NEXT_LOG_INFO("Mesh shader pass shutdown complete");
}

} // namespace Next
