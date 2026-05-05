#include "next/renderer/dx12/pipeline_state.h"
#include "next/foundation/logger.h"

namespace Next {

DX12PipelineState::DX12PipelineState()
    : primitiveTopology_(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST), initialized_(false) {
}

DX12PipelineState::~DX12PipelineState() {
    Shutdown();
}

bool DX12PipelineState::Initialize(
    DX12Device* device,
    DX12RootSignature* rootSignature,
    DX12Shader* vertexShader,
    DX12Shader* pixelShader,
    const std::vector<InputElementDesc>& inputLayout,
    DXGI_FORMAT renderTargetFormat,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopology,
    D3D12_FILL_MODE fillMode,
    bool depthEnable,
    DXGI_FORMAT depthStencilFormat) {

    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for PSO");
        return false;
    }

    if (!rootSignature || !vertexShader || !pixelShader) {
        NEXT_LOG_ERROR("Invalid root signature or shaders");
        return false;
    }

    Shutdown();

    NEXT_LOG_INFO("Creating Pipeline State Object...");

    // Convert input layout
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
    for (const auto& elem : inputLayout) {
        inputElements.push_back(elem.ToD3D12());
    }

    // Debug: Validate shader bytecode
    D3D12_SHADER_BYTECODE vsBytecode = vertexShader->GetBytecode();
    D3D12_SHADER_BYTECODE psBytecode = pixelShader->GetBytecode();

    NEXT_LOG_DEBUG("Vertex Shader Bytecode: %p (%zu bytes)", vsBytecode.pShaderBytecode, vsBytecode.BytecodeLength);
    NEXT_LOG_DEBUG("Pixel Shader Bytecode: %p (%zu bytes)", psBytecode.pShaderBytecode, psBytecode.BytecodeLength);

    if (!vsBytecode.pShaderBytecode || vsBytecode.BytecodeLength == 0) {
        NEXT_LOG_ERROR("Vertex shader bytecode is invalid!");
        return false;
    }

    if (!psBytecode.pShaderBytecode || psBytecode.BytecodeLength == 0) {
        NEXT_LOG_ERROR("Pixel shader bytecode is invalid!");
        return false;
    }

    // Debug: Print input layout details
    NEXT_LOG_DEBUG("Input Layout:");
    for (size_t i = 0; i < inputElements.size(); i++) {
        NEXT_LOG_DEBUG("  [%zu] SemanticName: %s, Format: %d, Offset: %u, Slot: %u",
                     i, inputElements[i].SemanticName, inputElements[i].Format,
                     inputElements[i].AlignedByteOffset, inputElements[i].InputSlot);
    }

    // Debug: Check Root Signature compatibility
    ID3D12RootSignature* rs = rootSignature->GetRootSignature();
    NEXT_LOG_DEBUG("Root Signature: 0x%p", rs);
    if (!rs) {
        NEXT_LOG_ERROR("Root signature is invalid");
        return false;
    }

    // Create pipeline state stream description
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    // Blend state
    D3D12_BLEND_DESC& blendDesc = psoDesc.BlendState;
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
        blendDesc.RenderTarget[i].BlendEnable = FALSE;
        blendDesc.RenderTarget[i].LogicOpEnable = FALSE;
        blendDesc.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
        blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    // Rasterizer state
    D3D12_RASTERIZER_DESC& rasterDesc = psoDesc.RasterizerState;
    rasterDesc.FillMode = fillMode;
    rasterDesc.CullMode = D3D12_CULL_MODE_BACK;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterDesc.DepthClipEnable = TRUE;
    rasterDesc.MultisampleEnable = FALSE;
    rasterDesc.AntialiasedLineEnable = FALSE;
    rasterDesc.ForcedSampleCount = 0;
    rasterDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Depth stencil state
    D3D12_DEPTH_STENCIL_DESC& depthDesc = psoDesc.DepthStencilState;
    depthDesc.DepthEnable = depthEnable ? TRUE : FALSE;
    depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    depthDesc.StencilEnable = FALSE;
    depthDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depthDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    depthDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depthDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depthDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    depthDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depthDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depthDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    depthDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    psoDesc.pRootSignature = rootSignature->GetRootSignature();
    psoDesc.VS = vertexShader->GetBytecode();
    psoDesc.PS = pixelShader->GetBytecode();
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.InputLayout = { inputElements.data(), (UINT)inputElements.size() };
    psoDesc.PrimitiveTopologyType = primitiveTopology;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = renderTargetFormat;
    psoDesc.DSVFormat = depthEnable ? depthStencilFormat : DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;

    // Create PSO
    HRESULT hr = device->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create PSO: 0x%X", hr);
        return false;
    }

    // Map primitive topology type to D3D12_PRIMITIVE_TOPOLOGY
    switch (primitiveTopology) {
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
            primitiveTopology_ = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            break;
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
            primitiveTopology_ = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            break;
        case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
            primitiveTopology_ = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            break;
        default:
            primitiveTopology_ = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            break;
    }

    initialized_ = true;
    NEXT_LOG_INFO("Pipeline State Object created successfully");
    return true;
}

void DX12PipelineState::Shutdown() {
    pso_.Reset();
    primitiveTopology_ = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    initialized_ = false;
}

} // namespace Next
