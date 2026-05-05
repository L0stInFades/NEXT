#include "next/renderer/dx12/mesh_shader_pass.h"
#include "next/foundation/logger.h"
#include <d3dcompiler.h>

namespace Next {

namespace {

template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename DataT>
struct alignas(void*) PipelineStateSubobject {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
    DataT data = {};
};

struct MeshPipelineStateStream {
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> rootSignature;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE> amplificationShader;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE> meshShader;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> pixelShader;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> blend;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> sampleMask;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> rasterizer;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> depthStencil;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> primitiveTopology;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> renderTargetFormats;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> depthStencilFormat;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> sampleDesc;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK, UINT> nodeMask;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO, D3D12_CACHED_PIPELINE_STATE> cachedPso;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, D3D12_PIPELINE_STATE_FLAGS> flags;
};

struct MeshOnlyPipelineStateStream {
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> rootSignature;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE> meshShader;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> pixelShader;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> blend;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> sampleMask;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> rasterizer;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> depthStencil;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> primitiveTopology;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> renderTargetFormats;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> depthStencilFormat;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> sampleDesc;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK, UINT> nodeMask;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO, D3D12_CACHED_PIPELINE_STATE> cachedPso;
    PipelineStateSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, D3D12_PIPELINE_STATE_FLAGS> flags;
};

D3D12_BLEND_DESC DefaultBlendDesc() {
    D3D12_BLEND_DESC desc = {};
    desc.AlphaToCoverageEnable = FALSE;
    desc.IndependentBlendEnable = FALSE;

    D3D12_RENDER_TARGET_BLEND_DESC target = {};
    target.BlendEnable = FALSE;
    target.LogicOpEnable = FALSE;
    target.SrcBlend = D3D12_BLEND_ONE;
    target.DestBlend = D3D12_BLEND_ZERO;
    target.BlendOp = D3D12_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D12_BLEND_ONE;
    target.DestBlendAlpha = D3D12_BLEND_ZERO;
    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    target.LogicOp = D3D12_LOGIC_OP_NOOP;
    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
        desc.RenderTarget[i] = target;
    }
    return desc;
}

D3D12_RASTERIZER_DESC DefaultRasterizerDesc() {
    D3D12_RASTERIZER_DESC desc = {};
    desc.FillMode = D3D12_FILL_MODE_SOLID;
    desc.CullMode = D3D12_CULL_MODE_BACK;
    desc.FrontCounterClockwise = FALSE;
    desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.DepthClipEnable = TRUE;
    desc.MultisampleEnable = FALSE;
    desc.AntialiasedLineEnable = FALSE;
    desc.ForcedSampleCount = 0;
    desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return desc;
}

D3D12_DEPTH_STENCIL_DESC DefaultDepthStencilDesc() {
    D3D12_DEPTH_STENCIL_DESC desc = {};
    desc.DepthEnable = TRUE;
    desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.StencilEnable = FALSE;
    desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    desc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    desc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    desc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    desc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.BackFace = desc.FrontFace;
    return desc;
}

D3D12_RT_FORMAT_ARRAY DefaultRenderTargetFormats() {
    D3D12_RT_FORMAT_ARRAY formats = {};
    formats.NumRenderTargets = 1;
    formats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    return formats;
}

} // namespace

MeshShaderPass::MeshShaderPass()
    : device_(nullptr)
    , amplificationShader_(nullptr)
    , meshShader_(nullptr)
    , pixelShader_(nullptr)
    , rootSignature_(nullptr)
    , initialized_(false)
    , dispatchEvidenceLogged_(false) {
}

MeshShaderPass::~MeshShaderPass() {
    Shutdown();
}

bool MeshShaderPass::Initialize(DX12Device* device) {
    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for mesh shader pass");
        return false;
    }

    Shutdown();

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

    if (!device_ || !device_->GetDevice() || !meshShaderPath || !pixelShaderPath) {
        NEXT_LOG_ERROR("Invalid mesh shader load request");
        return false;
    }

    pipelineState_.Reset();
    ownedRootSignature_.Reset();
    rootSignature_ = nullptr;

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

    auto clearLoadedShaders = [this]() {
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
    };

    // Load amplification shader (optional, can be null)
    if (amplificationShaderPath) {
        amplificationShader_ = new DX12Shader();
        if (!amplificationShader_->InitializeFromFile(device_, amplificationShaderPath, "main", "as_6_5")) {
            NEXT_LOG_ERROR("Failed to load amplification shader: %s", amplificationShaderPath);
            clearLoadedShaders();
            return false;
        }
        NEXT_LOG_INFO("Loaded amplification shader: %s", amplificationShaderPath);
    }

    // Load mesh shader (required)
    meshShader_ = new DX12Shader();
    if (!meshShader_->InitializeFromFile(device_, meshShaderPath, "main", "ms_6_5")) {
        NEXT_LOG_ERROR("Failed to load mesh shader: %s", meshShaderPath);
        clearLoadedShaders();
        return false;
    }
    NEXT_LOG_INFO("Loaded mesh shader: %s", meshShaderPath);

    // Load pixel shader (required)
    pixelShader_ = new DX12Shader();
    if (!pixelShader_->InitializeFromFile(device_, pixelShaderPath, "main", "ps_6_5")) {
        NEXT_LOG_ERROR("Failed to load pixel shader: %s", pixelShaderPath);
        clearLoadedShaders();
        return false;
    }
    NEXT_LOG_INFO("Loaded pixel shader: %s", pixelShaderPath);

    return true;
}

bool MeshShaderPass::CreateDefaultRootSignature() {
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 0;
    rootSignatureDesc.pParameters = nullptr;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error);
    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("Mesh shader root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("Mesh shader root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    hr = device_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&ownedRootSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create mesh shader root signature: 0x%X", hr);
        return false;
    }

    rootSignature_ = ownedRootSignature_.Get();
    return true;
}

bool MeshShaderPass::CreatePipelineState(ID3D12RootSignature* rootSignature) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot create pipeline state: mesh shader pass is not initialized");
        return false;
    }

    if (!device_ || !device_->GetDevice() || !meshShader_ || !pixelShader_) {
        NEXT_LOG_ERROR("Cannot create pipeline state: shaders not loaded");
        return false;
    }

    if (!meshShader_->GetBytecode().pShaderBytecode || meshShader_->GetBytecode().BytecodeLength == 0 ||
        !pixelShader_->GetBytecode().pShaderBytecode || pixelShader_->GetBytecode().BytecodeLength == 0) {
        NEXT_LOG_ERROR("Cannot create pipeline state: mesh or pixel shader bytecode is invalid");
        return false;
    }

    if (amplificationShader_ &&
        (!amplificationShader_->GetBytecode().pShaderBytecode ||
         amplificationShader_->GetBytecode().BytecodeLength == 0)) {
        NEXT_LOG_ERROR("Cannot create pipeline state: amplification shader bytecode is invalid");
        return false;
    }

    if (!device_->SupportsMeshShaders()) {
        NEXT_LOG_ERROR("Cannot create mesh shader pipeline state: device does not support mesh shaders");
        return false;
    }

    pipelineState_.Reset();
    ownedRootSignature_.Reset();
    rootSignature_ = rootSignature;
    dispatchEvidenceLogged_ = false;
    if (!rootSignature_ && !CreateDefaultRootSignature()) {
        return false;
    }

    if (!rootSignature_) {
        NEXT_LOG_ERROR("Cannot create pipeline state: root signature is invalid");
        return false;
    }

    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
    auto fillCommonStream = [this](auto& stream) {
        stream.rootSignature.data = rootSignature_;
        stream.meshShader.data = meshShader_->GetBytecode();
        stream.pixelShader.data = pixelShader_->GetBytecode();
        stream.blend.data = DefaultBlendDesc();
        stream.sampleMask.data = 0xffffffffu;
        stream.rasterizer.data = DefaultRasterizerDesc();
        stream.depthStencil.data = DefaultDepthStencilDesc();
        stream.primitiveTopology.data = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        stream.renderTargetFormats.data = DefaultRenderTargetFormats();
        stream.depthStencilFormat.data = DXGI_FORMAT_D32_FLOAT;
        stream.sampleDesc.data.Count = 1;
        stream.sampleDesc.data.Quality = 0;
        stream.nodeMask.data = 0;
        stream.cachedPso.data = {};
        stream.flags.data = D3D12_PIPELINE_STATE_FLAG_NONE;
    };

    MeshPipelineStateStream amplifiedStream = {};
    MeshOnlyPipelineStateStream meshOnlyStream = {};
    if (amplificationShader_) {
        fillCommonStream(amplifiedStream);
        amplifiedStream.amplificationShader.data = amplificationShader_->GetBytecode();
        streamDesc.SizeInBytes = sizeof(amplifiedStream);
        streamDesc.pPipelineStateSubobjectStream = &amplifiedStream;
    } else {
        fillCommonStream(meshOnlyStream);
        streamDesc.SizeInBytes = sizeof(meshOnlyStream);
        streamDesc.pPipelineStateSubobjectStream = &meshOnlyStream;
    }

    HRESULT hr = device_->GetDevice()->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&pipelineState_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create mesh shader pipeline state: 0x%X", hr);
        pipelineState_.Reset();
        return false;
    }

    NEXT_LOG_INFO("Mesh shader pipeline state created");
    return true;
}

void MeshShaderPass::Render(ID3D12GraphicsCommandList* commandList, uint32_t instanceCount) {
    if (!initialized_ || !commandList) {
        NEXT_LOG_ERROR("Cannot render: not initialized or invalid command list");
        return;
    }

    if (!pipelineState_ || !rootSignature_) {
        NEXT_LOG_ERROR("Cannot render: pipeline state not created");
        return;
    }

    if (instanceCount == 0) {
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList6> meshCommandList;
    HRESULT hr = commandList->QueryInterface(IID_PPV_ARGS(&meshCommandList));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Cannot render mesh shader pass: command list does not expose ID3D12GraphicsCommandList6");
        return;
    }

    constexpr UINT kMaxDispatchDimension = 65535;
    constexpr uint32_t kMaxDispatchGroups = 1u << 22;
    uint32_t dispatchGroups = instanceCount;
    if (dispatchGroups > kMaxDispatchGroups) {
        NEXT_LOG_WARNING("Clamping mesh dispatch from %u to %u groups", dispatchGroups, kMaxDispatchGroups);
        dispatchGroups = kMaxDispatchGroups;
    }

    const UINT threadGroupCountX =
        dispatchGroups > kMaxDispatchDimension ? kMaxDispatchDimension : dispatchGroups;
    const UINT threadGroupCountY =
        (dispatchGroups + threadGroupCountX - 1) / threadGroupCountX;

    commandList->SetGraphicsRootSignature(rootSignature_);
    meshCommandList->SetPipelineState(pipelineState_.Get());
    meshCommandList->DispatchMesh(threadGroupCountX, threadGroupCountY, 1);

    if (!dispatchEvidenceLogged_) {
        NEXT_LOG_INFO("Mesh shader pass dispatched");
        dispatchEvidenceLogged_ = true;
    }

    NEXT_LOG_DEBUG("Mesh shader dispatch: %u groups (%u x %u x 1)",
                   dispatchGroups,
                   threadGroupCountX,
                   threadGroupCountY);
}

void MeshShaderPass::Shutdown() {
    pipelineState_.Reset();
    ownedRootSignature_.Reset();
    rootSignature_ = nullptr;

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
    dispatchEvidenceLogged_ = false;

    NEXT_LOG_INFO("Mesh shader pass shutdown complete");
}

} // namespace Next
