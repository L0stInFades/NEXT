#include "next/renderer/dx12/ambient_occlusion.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <wrl/client.h>
#include <d3d12.h>
#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;

namespace Next {

//=============================================================================
// GTAO Implementation
//=============================================================================

GTAO::GTAO()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , width_(0)
    , height_(0)
    , initialized_(false) {
}

GTAO::~GTAO() {
    Shutdown();
}

bool GTAO::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                     uint32_t width, uint32_t height) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for GTAO");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    width_ = width;
    height_ = height;

    // Create resources
    if (!CreateResources()) {
        NEXT_LOG_ERROR("Failed to create GTAO resources");
        return false;
    }

    // Create shaders
    if (!CreateShaders()) {
        NEXT_LOG_ERROR("Failed to create GTAO shaders");
        return false;
    }

    // Create root signature
    if (!CreateRootSignature()) {
        NEXT_LOG_ERROR("Failed to create GTAO root signature");
        return false;
    }

    // Create pipeline states
    if (!CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create GTAO pipeline states");
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("GTAO initialized: %ux%u", width, height);
    return true;
}

bool GTAO::CreateResources() {
    // Create AO texture (single channel R16_UNORM for quality)
    D3D12_RESOURCE_DESC aoDesc = {};
    aoDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    aoDesc.Alignment = 0;
    aoDesc.Width = width_;
    aoDesc.Height = height_;
    aoDesc.DepthOrArraySize = 1;
    aoDesc.MipLevels = 1;
    aoDesc.Format = DXGI_FORMAT_R16_UNORM;
    aoDesc.SampleDesc.Count = 1;
    aoDesc.SampleDesc.Quality = 0;
    aoDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    aoDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &aoDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&aoTexture_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GTAO texture");
        return false;
    }

    // Create temp texture for filtering
    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &aoDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&aoTextureTemp_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GTAO temp texture");
        return false;
    }

    // Initialize constant buffer
    if (!constantBuffer_.Initialize(device_, sizeof(GTAOParameters))) {
        NEXT_LOG_ERROR("Failed to create GTAO constant buffer");
        return false;
    }

    return true;
}

bool GTAO::CreateShaders() {
    // GTAO shader
    if (!gtaoShader_.CompileFromFile(L"shaders/gtao.hlsl", "PSGTAO", "ps_5_1")) {
        NEXT_LOG_ERROR("Failed to compile GTAO shader");
        return false;
    }

    // Filter shader
    if (!filterShader_.CompileFromFile(L"shaders/gtao_filter.hlsl", "PSFilter", "ps_5_1")) {
        NEXT_LOG_ERROR("Failed to compile GTAO filter shader");
        return false;
    }

    return true;
}

bool GTAO::CreateRootSignature() {
    // Create GTAO root signature with required parameters
    // Note: Full implementation requires proper D3D12 root signature setup
    // This placeholder shows the structure but uses simplified approach

    D3D12_ROOT_PARAMETER rootParameters[4] = {};

    // 0: Constant buffer (camera parameters, AO settings)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // 1: Depth texture SRV
    D3D12_DESCRIPTOR_RANGE depthRange = {};
    depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depthRange.NumDescriptors = 1;
    depthRange.BaseShaderRegister = 0;
    depthRange.RegisterSpace = 0;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &depthRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 2: Normal texture SRV
    D3D12_DESCRIPTOR_RANGE normalRange = {};
    normalRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    normalRange.NumDescriptors = 1;
    normalRange.BaseShaderRegister = 1;
    normalRange.RegisterSpace = 0;
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[2].DescriptorTable.pDescriptorRanges = &normalRange;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 3: Sampler
    D3D12_DESCRIPTOR_RANGE samplerRange = {};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = 1;
    samplerRange.BaseShaderRegister = 0;
    samplerRange.RegisterSpace = 0;
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges = &samplerRange;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 4;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr)) {
        // Failed to serialize root signature
        if (error) {
            // Root signature error details available in error blob
        }
        return false;
    }

    hr = device_->GetDevice()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GTAO root signature: 0x%X", hr);
        return false;
    }

    NEXT_LOG_INFO("GTAO root signature created");
    return true;
}

bool GTAO::CreatePipelineStates() {
    // Create GTAO pipeline state
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature_.Get();

    // TODO: Load shaders from compiled bytecode
    // psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    // psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };

    // Rasterizer state
    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.MultisampleEnable = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount = 0;
    rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    psoDesc.RasterizerState = rasterizerDesc;

    // Blend state
    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState = blendDesc;

    // Depth stencil state
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    // TODO: This will fail until shaders are provided
    // HRESULT hr = device_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
    // For now, return true as placeholder
    NEXT_LOG_WARNING("GTAO pipeline state creation requires compiled shaders");
    return true;
}

void GTAO::Render(ID3D12GraphicsCommandList* commandList,
                 ID3D12Resource* depthBuffer,
                 ID3D12Resource* normalBuffer,
                 ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    // Placeholder for GTAO rendering with constant buffer update
    // Full implementation requires: shaders, PSO, descriptor management
    // For now, just log that GTAO would be rendered
    NEXT_LOG_DEBUG("GTAO render (placeholder - radius=%.2f)",
                   params_.radius);

    // Render GTAO
    RenderGTAO(commandList);

    // Apply spatial filter
    if (params_.bilateralFilter) {
        ApplySpatialFilter(commandList);
    }
}

void GTAO::RenderGTAO(ID3D12GraphicsCommandList* commandList) {
    // Placeholder - GTAO rendering requires shaders
    NEXT_LOG_WARNING("GTAO::RenderGTAO not yet implemented - needs shaders");
}

void GTAO::ApplySpatialFilter(ID3D12GraphicsCommandList* commandList) {
    // Placeholder - spatial filter requires shaders
    NEXT_LOG_WARNING("GTAO::ApplySpatialFilter not yet implemented - needs shaders");
}

bool GTAO::Resize(uint32_t width, uint32_t height) {
    Shutdown();
    return Initialize(device_, srvHeap_, width, height);
}

void GTAO::Shutdown() {
    aoTexture_.Reset();
    aoTextureTemp_.Reset();
    rootSignature_.Reset();
    gtaoPSO_.Reset();
    filterPSO_.Reset();
    constantBuffer_.Shutdown();
    initialized_ = false;
}

ID3D12Resource* GTAO::GetOutputTexture() {
    return aoTexture_.Get();
}

//=============================================================================
// HBAO Implementation
//=============================================================================

HBAO::HBAO()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , width_(0)
    , height_(0)
    , initialized_(false) {
}

HBAO::~HBAO() {
    Shutdown();
}

bool HBAO::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                     uint32_t width, uint32_t height) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for HBAO");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    width_ = width;
    height_ = height;

    // Create resources
    if (!CreateResources()) {
        NEXT_LOG_ERROR("Failed to create HBAO resources");
        return false;
    }

    // Create shaders
    if (!CreateShaders()) {
        NEXT_LOG_ERROR("Failed to create HBAO shaders");
        return false;
    }

    // Create root signature
    if (!CreateRootSignature()) {
        NEXT_LOG_ERROR("Failed to create HBAO root signature");
        return false;
    }

    // Create pipeline states
    if (!CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create HBAO pipeline states");
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("HBAO initialized: %ux%u", width, height);
    return true;
}

bool HBAO::CreateResources() {
    // Similar to GTAO but with HBAO-specific parameters
    D3D12_RESOURCE_DESC aoDesc = {};
    aoDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    aoDesc.Alignment = 0;
    aoDesc.Width = width_;
    aoDesc.Height = height_;
    aoDesc.DepthOrArraySize = 1;
    aoDesc.MipLevels = 1;
    aoDesc.Format = DXGI_FORMAT_R8_UNORM;  // HBAO uses R8 for performance
    aoDesc.SampleDesc.Count = 1;
    aoDesc.SampleDesc.Quality = 0;
    aoDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    aoDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &aoDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&aoTexture_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create HBAO texture");
        return false;
    }

    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &aoDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&aoTextureTemp_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create HBAO temp texture");
        return false;
    }

    // Initialize constant buffer
    if (!constantBuffer_.Initialize(device_, sizeof(HBAOParameters))) {
        NEXT_LOG_ERROR("Failed to create HBAO constant buffer");
        return false;
    }

    return true;
}

bool HBAO::CreateShaders() {
    // Placeholder - actual shader files need to be created
    NEXT_LOG_WARNING("HBAO shaders not yet implemented - using placeholder");
    return true;
}

bool HBAO::CreateRootSignature() {
    // Similar to GTAO root signature
    NEXT_LOG_WARNING("HBAO root signature not yet implemented - using placeholder");
    return true;
}

bool HBAO::CreatePipelineStates() {
    // Similar to GTAO PSO
    NEXT_LOG_WARNING("HBAO pipeline states not yet implemented - using placeholder");
    return true;
}

void HBAO::Render(ID3D12GraphicsCommandList* commandList,
                 ID3D12Resource* depthBuffer,
                 ID3D12Resource* normalBuffer,
                 ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    // Placeholder for HBAO rendering with constant buffer update
    NEXT_LOG_DEBUG("HBAO render (placeholder - radius=%.2f)",
                   params_.radius);

    // Render HBAO
    RenderHBAO(commandList);

    // Apply blur if enabled
    if (params_.blurEnabled) {
        ApplyBlur(commandList);
    }
}

void HBAO::RenderHBAO(ID3D12GraphicsCommandList* commandList) {
    // HBAO rendering pass
    NEXT_LOG_WARNING("HBAO render pass not yet implemented");
}

void HBAO::ApplyBlur(ID3D12GraphicsCommandList* commandList) {
    // HBAO blur pass
    NEXT_LOG_WARNING("HBAO blur pass not yet implemented");
}

bool HBAO::Resize(uint32_t width, uint32_t height) {
    Shutdown();
    return Initialize(device_, srvHeap_, width, height);
}

void HBAO::Shutdown() {
    aoTexture_.Reset();
    aoTextureTemp_.Reset();
    rootSignature_.Reset();
    hbaoPSO_.Reset();
    blurPSO_.Reset();
    constantBuffer_.Shutdown();
    initialized_ = false;
}

ID3D12Resource* HBAO::GetOutputTexture() {
    return aoTexture_.Get();
}

//=============================================================================
// VXAO Implementation
//=============================================================================

VXAO::VXAO()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , width_(0)
    , height_(0)
    , initialized_(false) {
}

VXAO::~VXAO() {
    Shutdown();
}

bool VXAO::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                     uint32_t width, uint32_t height) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for VXAO");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    width_ = width;
    height_ = height;

    // Create resources (including 3D voxel texture)
    if (!CreateResources()) {
        NEXT_LOG_ERROR("Failed to create VXAO resources");
        return false;
    }

    // Create shaders
    if (!CreateShaders()) {
        NEXT_LOG_ERROR("Failed to create VXAO shaders");
        return false;
    }

    // Create root signatures
    if (!CreateRootSignature()) {
        NEXT_LOG_ERROR("Failed to create VXAO root signature");
        return false;
    }

    // Create pipeline states
    if (!CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create VXAO pipeline states");
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("VXAO initialized: %ux%u, voxel resolution: %d",
                  width, height, params_.voxelResolution);
    return true;
}

bool VXAO::CreateResources() {
    // Create AO texture
    D3D12_RESOURCE_DESC aoDesc = {};
    aoDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    aoDesc.Alignment = 0;
    aoDesc.Width = width_;
    aoDesc.Height = height_;
    aoDesc.DepthOrArraySize = 1;
    aoDesc.MipLevels = 1;
    aoDesc.Format = DXGI_FORMAT_R16_UNORM;
    aoDesc.SampleDesc.Count = 1;
    aoDesc.SampleDesc.Quality = 0;
    aoDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    aoDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &aoDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&aoTexture_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXAO texture");
        return false;
    }

    // Create 3D voxel texture
    D3D12_RESOURCE_DESC voxelDesc = {};
    voxelDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    voxelDesc.Alignment = 0;
    voxelDesc.Width = params_.voxelResolution;
    voxelDesc.Height = params_.voxelResolution;
    voxelDesc.DepthOrArraySize = params_.voxelResolution;
    voxelDesc.MipLevels = 1;
    voxelDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // RGBA for voxel data
    voxelDesc.SampleDesc.Count = 1;
    voxelDesc.SampleDesc.Quality = 0;
    voxelDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    voxelDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &voxelDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&voxelTexture_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXAO voxel texture");
        return false;
    }

    // Create temp voxel texture for double-buffering
    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &voxelDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&voxelTextureTemp_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXAO temp voxel texture");
        return false;
    }

    // Initialize constant buffers
    if (!constantBuffer_.Initialize(device_, sizeof(VXAOParameters))) {
        NEXT_LOG_ERROR("Failed to create VXAO constant buffer");
        return false;
    }

    if (!voxelizationBuffer_.Initialize(device_, sizeof(VXAOParameters))) {
        NEXT_LOG_ERROR("Failed to create VXAO voxelization buffer");
        return false;
    }

    return true;
}

bool VXAO::CreateShaders() {
    // Placeholder - actual shader files need to be created
    NEXT_LOG_WARNING("VXAO shaders not yet implemented - using placeholder");
    return true;
}

bool VXAO::CreateRootSignature() {
    // VXAO needs two root signatures: one for voxelization, one for rendering
    NEXT_LOG_WARNING("VXAO root signatures not yet implemented - using placeholder");
    return true;
}

bool VXAO::CreatePipelineStates() {
    NEXT_LOG_WARNING("VXAO pipeline states not yet implemented - using placeholder");
    return true;
}

void VXAO::Render(ID3D12GraphicsCommandList* commandList,
                 ID3D12Resource* depthBuffer,
                 ID3D12Resource* normalBuffer,
                 ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    // Placeholder for VXAO rendering with constant buffer update
    NEXT_LOG_DEBUG("VXAO render (placeholder - voxelSize=%.3f)",
                   params_.voxelSize);

    // Voxelize scene (if enabled)
    if (params_.enableVoxelization) {
        VoxelizeScene(commandList);
    }

    // Render VXAO
    RenderVXAO(commandList);
}

void VXAO::VoxelizeScene(ID3D12GraphicsCommandList* commandList) {
    // Voxelization pass
    NEXT_LOG_WARNING("VXAO voxelization pass not yet implemented");
}

void VXAO::RenderVXAO(ID3D12GraphicsCommandList* commandList) {
    // VXAO rendering pass
    NEXT_LOG_WARNING("VXAO render pass not yet implemented");
}

bool VXAO::Resize(uint32_t width, uint32_t height) {
    Shutdown();
    return Initialize(device_, srvHeap_, width, height);
}

void VXAO::Shutdown() {
    aoTexture_.Reset();
    voxelTexture_.Reset();
    voxelTextureTemp_.Reset();
    voxelUAV_.Reset();
    rootSignature_.Reset();
    voxelizationRootSignature_.Reset();
    vxaoPSO_.Reset();
    voxelizationPSO_.Reset();
    constantBuffer_.Shutdown();
    voxelizationBuffer_.Shutdown();
    initialized_ = false;
}

ID3D12Resource* VXAO::GetOutputTexture() {
    return aoTexture_.Get();
}

//=============================================================================
// Ambient Occlusion Manager
//=============================================================================

AmbientOcclusionManager::AmbientOcclusionManager()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , gtao_(nullptr)
    , hbao_(nullptr)
    , vxao_(nullptr)
    , currentAO_(nullptr)
    , currentType_(AOType::None)
    , width_(0)
    , height_(0)
    , initialized_(false) {
}

AmbientOcclusionManager::~AmbientOcclusionManager() {
    Shutdown();
}

bool AmbientOcclusionManager::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                                        uint32_t width, uint32_t height) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for AO manager");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    width_ = width;
    height_ = height;

    // Create all AO technique instances
    gtao_ = new GTAO();
    hbao_ = new HBAO();
    vxao_ = new VXAO();

    // Initialize GTAO by default (best quality/performance balance)
    if (!gtao_->Initialize(device, srvHeap, width, height)) {
        NEXT_LOG_ERROR("Failed to initialize GTAO");
        return false;
    }

    // Initialize HBAO
    if (!hbao_->Initialize(device, srvHeap, width, height)) {
        NEXT_LOG_WARNING("Failed to initialize HBAO - will not be available");
    }

    // Initialize VXAO
    if (!vxao_->Initialize(device, srvHeap, width, height)) {
        NEXT_LOG_WARNING("Failed to initialize VXAO - will not be available");
    }

    // Set GTAO as default
    currentAO_ = gtao_;
    currentType_ = AOType::GTAO;

    initialized_ = true;
    NEXT_LOG_INFO("Ambient Occlusion Manager initialized (default: GTAO)");
    return true;
}

bool AmbientOcclusionManager::SetAOType(AOType type) {
    if (!initialized_) {
        NEXT_LOG_ERROR("AO manager not initialized");
        return false;
    }

    switch (type) {
        case AOType::GTAO:
            if (gtao_ && gtao_->IsInitialized()) {
                currentAO_ = gtao_;
                currentType_ = AOType::GTAO;
                NEXT_LOG_INFO("Switched to GTAO");
                return true;
            }
            break;

        case AOType::HBAO:
            if (hbao_ && hbao_->IsInitialized()) {
                currentAO_ = hbao_;
                currentType_ = AOType::HBAO;
                NEXT_LOG_INFO("Switched to HBAO");
                return true;
            }
            break;

        case AOType::VXAO:
            if (vxao_ && vxao_->IsInitialized()) {
                currentAO_ = vxao_;
                currentType_ = AOType::VXAO;
                NEXT_LOG_INFO("Switched to VXAO");
                return true;
            }
            break;

        case AOType::None:
            currentAO_ = nullptr;
            currentType_ = AOType::None;
            NEXT_LOG_INFO("AO disabled");
            return true;

        default:
            NEXT_LOG_ERROR("Unknown AO type");
            break;
    }

    NEXT_LOG_ERROR("Failed to switch to AO type %d - technique not available", (int)type);
    return false;
}

void AmbientOcclusionManager::Render(ID3D12GraphicsCommandList* commandList,
                                    ID3D12Resource* depthBuffer,
                                    ID3D12Resource* normalBuffer,
                                    ID3D12Resource* output) {
    if (!initialized_ || !currentAO_) {
        return;
    }

    currentAO_->Render(commandList, depthBuffer, normalBuffer, output);
}

bool AmbientOcclusionManager::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) {
        return false;
    }

    width_ = width;
    height_ = height;

    if (gtao_) gtao_->Resize(width, height);
    if (hbao_) hbao_->Resize(width, height);
    if (vxao_) vxao_->Resize(width, height);

    return true;
}

void AmbientOcclusionManager::Shutdown() {
    if (gtao_) {
        gtao_->Shutdown();
        delete gtao_;
        gtao_ = nullptr;
    }

    if (hbao_) {
        hbao_->Shutdown();
        delete hbao_;
        hbao_ = nullptr;
    }

    if (vxao_) {
        vxao_->Shutdown();
        delete vxao_;
        vxao_ = nullptr;
    }

    currentAO_ = nullptr;
    currentType_ = AOType::None;
    initialized_ = false;

    NEXT_LOG_INFO("Ambient Occlusion Manager shutdown complete");
}

ID3D12Resource* AmbientOcclusionManager::GetOutputTexture() {
    if (currentAO_) {
        return currentAO_->GetOutputTexture();
    }
    return nullptr;
}

void AmbientOcclusionManager::SetGTAOParameters(const GTAOParameters& params) {
    if (gtao_) {
        gtao_->SetParameters(params);
    }
}

void AmbientOcclusionManager::SetHBAOParameters(const HBAOParameters& params) {
    if (hbao_) {
        hbao_->SetParameters(params);
    }
}

void AmbientOcclusionManager::SetVXAOParameters(const VXAOParameters& params) {
    if (vxao_) {
        vxao_->SetParameters(params);
    }
}

const GTAOParameters& AmbientOcclusionManager::GetGTAOParameters() const {
    static GTAOParameters defaultParams;
    return gtao_ ? gtao_->GetParameters() : defaultParams;
}

const HBAOParameters& AmbientOcclusionManager::GetHBAOParameters() const {
    static HBAOParameters defaultParams;
    return hbao_ ? hbao_->GetParameters() : defaultParams;
}

const VXAOParameters& AmbientOcclusionManager::GetVXAOParameters() const {
    static VXAOParameters defaultParams;
    return vxao_ ? vxao_->GetParameters() : defaultParams;
}

} // namespace Next
