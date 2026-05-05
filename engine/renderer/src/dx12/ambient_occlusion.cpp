#include "next/renderer/dx12/ambient_occlusion.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;

namespace Next {

namespace {

struct GTAOConstantsGPU {
    float radiusPixels;
    float power;
    float sampleCount;
    float temporalStability;
    float invWidth;
    float invHeight;
    float depthScale;
    float padding0;
};

struct HBAOConstantsGPU {
    float radiusPixels;
    float bias;
    float stepCount;
    float power;
    float invWidth;
    float invHeight;
    float depthScale;
    float padding0;
};

struct VXAOConstantsGPU {
    float radiusPixels;
    float coneSampleCount;
    float coneDirectionCount;
    float power;
    float invWidth;
    float invHeight;
    float depthScale;
    float hardness;
};

void ClearAmbientOcclusionTexture(ID3D12GraphicsCommandList* commandList,
                                  ID3D12Resource* texture,
                                  D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    if (!commandList || !texture || rtv.ptr == 0) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    const float neutralAO[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    commandList->ClearRenderTargetView(rtv, neutralAO, 0, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

std::filesystem::path GetExecutableDirectory() {
    wchar_t path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(path).parent_path();
}

void PushUniquePath(std::vector<std::filesystem::path>& roots, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    for (const auto& existing : roots) {
        if (existing == path) {
            return;
        }
    }
    roots.push_back(path);
}

std::filesystem::path ResolveRuntimeAssetPath(const std::filesystem::path& relativePath) {
    if (relativePath.empty() || relativePath.is_absolute()) {
        return relativePath;
    }

    std::vector<std::filesystem::path> roots;
    std::error_code ec;
    PushUniquePath(roots, std::filesystem::current_path(ec));

    std::filesystem::path probe = GetExecutableDirectory();
    for (int i = 0; i < 6 && !probe.empty(); ++i) {
        PushUniquePath(roots, probe);
        const std::filesystem::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }

    for (const auto& root : roots) {
        const std::filesystem::path candidate = root / relativePath;
        if (std::filesystem::exists(candidate, ec)) {
            const std::filesystem::path absoluteCandidate = std::filesystem::absolute(candidate, ec);
            return ec ? candidate : absoluteCandidate;
        }
    }

    return relativePath;
}

std::string ResolveRuntimeAssetPathUtf8(const char* relativePath) {
    return ResolveRuntimeAssetPath(std::filesystem::path(relativePath)).u8string();
}

bool EqualsIgnoreCaseAscii(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }

    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = static_cast<char>(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = static_cast<char>(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }

    return *a == '\0' && *b == '\0';
}

bool IsTruthyEnv(const char* name) {
    const char* value = std::getenv(name);
    return value && *value &&
           !EqualsIgnoreCaseAscii(value, "0") &&
           !EqualsIgnoreCaseAscii(value, "false") &&
           !EqualsIgnoreCaseAscii(value, "off") &&
           !EqualsIgnoreCaseAscii(value, "no");
}

} // namespace

//=============================================================================
// GTAO Implementation
//=============================================================================

GTAO::GTAO()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , heapManager_(nullptr)
    , width_(0)
    , height_(0)
    , initialized_(false)
    , renderEvidenceLogged_(false)
    , filterEvidenceLogged_(false) {
}

GTAO::~GTAO() {
    Shutdown();
}

bool GTAO::Initialize(DX12Device* device,
                     DX12DescriptorHeap* srvHeap,
                     DX12DescriptorHeapManager* heapManager,
                     uint32_t width,
                     uint32_t height) {
    if (!device || !device->GetDevice() || !srvHeap || !srvHeap->GetHeap() ||
        !srvHeap->IsShaderVisible() || !heapManager || width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid device or heap for GTAO");
        return false;
    }

    Shutdown();

    device_ = device;
    srvHeap_ = srvHeap;
    heapManager_ = heapManager;
    width_ = width;
    height_ = height;
    renderEvidenceLogged_ = false;
    filterEvidenceLogged_ = false;

    // Create resources
    if (!CreateResources()) {
        NEXT_LOG_ERROR("Failed to create GTAO resources");
        Shutdown();
        return false;
    }

    // Create shaders
    if (!CreateShaders()) {
        NEXT_LOG_ERROR("Failed to create GTAO shaders");
        Shutdown();
        return false;
    }

    // Create root signature
    if (!CreateRootSignature()) {
        NEXT_LOG_ERROR("Failed to create GTAO root signature");
        Shutdown();
        return false;
    }

    // Create pipeline states
    if (!CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create GTAO pipeline states");
        Shutdown();
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("GTAO initialized: %ux%u", width, height);
    return true;
}

bool GTAO::CreateResources() {
    if (!rtvHeap_.Initialize(device_, 2)) {
        NEXT_LOG_ERROR("Failed to create GTAO RTV heap");
        return false;
    }

    depthSrvAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    if (depthSrvAllocation_.count < 2 || depthSrvAllocation_.gpuHandle.ptr == 0) {
        NEXT_LOG_ERROR("Failed to allocate GTAO depth/filter SRV descriptors");
        return false;
    }

    // Create AO texture in the GI format so it can be copied/combined without format conversion.
    D3D12_RESOURCE_DESC aoDesc = {};
    aoDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    aoDesc.Alignment = 0;
    aoDesc.Width = width_;
    aoDesc.Height = height_;
    aoDesc.DepthOrArraySize = 1;
    aoDesc.MipLevels = 1;
    aoDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    aoDesc.SampleDesc.Count = 1;
    aoDesc.SampleDesc.Quality = 0;
    aoDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    aoDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = aoDesc.Format;
    clearValue.Color[0] = 1.0f;
    clearValue.Color[1] = 1.0f;
    clearValue.Color[2] = 1.0f;
    clearValue.Color[3] = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &aoDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
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
        &clearValue,
        IID_PPV_ARGS(&aoTextureTemp_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GTAO temp texture");
        return false;
    }

    // Initialize constant buffer
    if (!constantBuffer_.Initialize(device_, sizeof(GTAOConstantsGPU))) {
        NEXT_LOG_ERROR("Failed to create GTAO constant buffer");
        return false;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = aoDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    const D3D12_CPU_DESCRIPTOR_HANDLE aoRtv = rtvHeap_.GetCPUDescriptorHandle(0);
    const D3D12_CPU_DESCRIPTOR_HANDLE tempRtv = rtvHeap_.GetCPUDescriptorHandle(1);
    if (aoRtv.ptr == 0 || tempRtv.ptr == 0) {
        NEXT_LOG_ERROR("Invalid GTAO RTV descriptor handles");
        return false;
    }
    device_->GetDevice()->CreateRenderTargetView(aoTexture_.Get(), &rtvDesc, aoRtv);
    device_->GetDevice()->CreateRenderTargetView(aoTextureTemp_.Get(), &rtvDesc, tempRtv);

    return true;
}

bool GTAO::CreateShaders() {
    const std::string vsPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/post_process.vs.hlsl");
    const std::string gtaoPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/gtao.hlsl");
    const std::string filterPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/gtao_filter.hlsl");

    if (!fullscreenVertexShader_.LoadFromFile(device_, vsPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile GTAO fullscreen vertex shader: %s", vsPath.c_str());
        return false;
    }

    if (!gtaoShader_.CompileFromFile(device_, gtaoPath.c_str(), "PSGTAO", "ps_5_1")) {
        NEXT_LOG_ERROR("Failed to compile GTAO shader: %s", gtaoPath.c_str());
        return false;
    }

    if (!filterShader_.CompileFromFile(device_, filterPath.c_str(), "PSFilter", "ps_5_1")) {
        NEXT_LOG_ERROR("Failed to compile GTAO filter shader: %s", filterPath.c_str());
        return false;
    }

    return true;
}

bool GTAO::CreateRootSignature() {
    D3D12_ROOT_PARAMETER rootParameters[2] = {};

    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE depthRange = {};
    depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depthRange.NumDescriptors = 1;
    depthRange.BaseShaderRegister = 0;
    depthRange.RegisterSpace = 0;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &depthRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 2;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = &sampler;
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
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.VS = fullscreenVertexShader_.GetBytecode();
    psoDesc.PS = gtaoShader_.GetPixelShader();

    // Rasterizer state
    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
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
    psoDesc.InputLayout = {};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    HRESULT hr = device_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&gtaoPSO_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GTAO pipeline state: 0x%X", hr);
        return false;
    }

    psoDesc.PS = filterShader_.GetPixelShader();
    hr = device_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&filterPSO_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GTAO filter pipeline state: 0x%X", hr);
        return false;
    }

    NEXT_LOG_INFO("GTAO depth-sampling pipeline state created");
    return true;
}

void GTAO::Render(ID3D12GraphicsCommandList* commandList,
                 ID3D12Resource* depthBuffer,
                 ID3D12Resource* normalBuffer,
                 ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    if (!commandList || !aoTexture_) {
        NEXT_LOG_ERROR("Cannot render GTAO: invalid command list or AO texture");
        return;
    }

    if (!depthBuffer || !gtaoPSO_ || !rootSignature_ ||
        !srvHeap_ || !srvHeap_->GetHeap() ||
        depthSrvAllocation_.gpuHandle.ptr == 0 ||
        constantBuffer_.GetGPUVirtualAddress() == 0) {
        ClearAmbientOcclusionTexture(commandList, aoTexture_.Get(), rtvHeap_.GetCPUDescriptorHandle(0));
        NEXT_LOG_WARNING("GTAO fell back to neutral AO because depth input or pipeline resources are unavailable");
        return;
    }

    if (!UpdateDepthShaderResource(depthBuffer) || !UpdateConstants()) {
        ClearAmbientOcclusionTexture(commandList, aoTexture_.Get(), rtvHeap_.GetCPUDescriptorHandle(0));
        NEXT_LOG_WARNING("GTAO fell back to neutral AO because inputs could not be updated");
        return;
    }

    if (params_.bilateralFilter) {
        if (!RenderGTAO(commandList, aoTextureTemp_.Get(), rtvHeap_.GetCPUDescriptorHandle(1)) ||
            !ApplySpatialFilter(commandList)) {
            return;
        }
    } else {
        if (!RenderGTAO(commandList, aoTexture_.Get(), rtvHeap_.GetCPUDescriptorHandle(0))) {
            return;
        }
    }

    if (!renderEvidenceLogged_) {
        NEXT_LOG_INFO("GTAO pass rendered");
        renderEvidenceLogged_ = true;
    }
}

bool GTAO::UpdateDepthShaderResource(ID3D12Resource* depthBuffer) {
    if (!device_ || !device_->GetDevice() || !depthBuffer || depthSrvAllocation_.cpuHandle.ptr == 0) {
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->GetDevice()->CreateShaderResourceView(depthBuffer, &srvDesc, depthSrvAllocation_.cpuHandle);
    return true;
}

bool GTAO::UpdateFilterShaderResource(ID3D12Resource* filterInput) {
    if (!device_ || !device_->GetDevice() || !filterInput || !srvHeap_ || !srvHeap_->GetHeap() ||
        depthSrvAllocation_.cpuHandle.ptr == 0 || depthSrvAllocation_.gpuHandle.ptr == 0 ||
        depthSrvAllocation_.count < 2) {
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE filterSrvCPU = depthSrvAllocation_.cpuHandle;
    filterSrvCPU.ptr += srvHeap_->GetDescriptorSize();

    const D3D12_RESOURCE_DESC inputDesc = filterInput->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = inputDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->GetDevice()->CreateShaderResourceView(filterInput, &srvDesc, filterSrvCPU);
    return true;
}

bool GTAO::UpdateConstants() {
    GTAOConstantsGPU constants = {};
    constants.radiusPixels = std::max(1.0f, params_.radius * 64.0f);
    constants.power = std::max(0.001f, params_.power);
    constants.sampleCount = static_cast<float>(std::clamp(params_.samples, 4, 16));
    constants.temporalStability = std::clamp(params_.temporalStability, 0.0f, 1.0f);
    constants.invWidth = width_ > 0 ? 1.0f / static_cast<float>(width_) : 0.0f;
    constants.invHeight = height_ > 0 ? 1.0f / static_cast<float>(height_) : 0.0f;
    constants.depthScale = 128.0f;

    return constantBuffer_.UpdateData(&constants, sizeof(constants));
}

bool GTAO::RenderGTAO(ID3D12GraphicsCommandList* commandList,
                      ID3D12Resource* target,
                      D3D12_CPU_DESCRIPTOR_HANDLE targetRTV) {
    if (!commandList || !gtaoPSO_) {
        NEXT_LOG_TRACE("GTAO neutral AO resolve already applied");
        return false;
    }

    if (!target || targetRTV.ptr == 0) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(width_);
    scissor.bottom = static_cast<LONG>(height_);

    commandList->OMSetRenderTargets(1, &targetRTV, TRUE, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap* heaps[] = {srvHeap_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(gtaoPSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(0, constantBuffer_.GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(1, depthSrvAllocation_.gpuHandle);
    commandList->DrawInstanced(3, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
    return true;
}

bool GTAO::ApplySpatialFilter(ID3D12GraphicsCommandList* commandList) {
    if (!commandList || !filterPSO_ || !aoTexture_ || !aoTextureTemp_) {
        return false;
    }

    if (!UpdateFilterShaderResource(aoTextureTemp_.Get()) || !UpdateConstants()) {
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_.GetCPUDescriptorHandle(0);
    if (rtv.ptr == 0) {
        return false;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE filterSrvGPU = depthSrvAllocation_.gpuHandle;
    filterSrvGPU.ptr += srvHeap_->GetDescriptorSize();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = aoTexture_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(width_);
    scissor.bottom = static_cast<LONG>(height_);

    commandList->OMSetRenderTargets(1, &rtv, TRUE, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap* heaps[] = {srvHeap_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(filterPSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(0, constantBuffer_.GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(1, filterSrvGPU);
    commandList->DrawInstanced(3, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);

    if (!filterEvidenceLogged_) {
        NEXT_LOG_INFO("GTAO spatial filter rendered");
        filterEvidenceLogged_ = true;
    }
    return true;
}

bool GTAO::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid GTAO resize dimensions: %ux%u", width, height);
        return false;
    }

    DX12Device* device = device_;
    DX12DescriptorHeap* srvHeap = srvHeap_;
    DX12DescriptorHeapManager* heapManager = heapManager_;
    Shutdown();
    return Initialize(device, srvHeap, heapManager, width, height);
}

void GTAO::Shutdown() {
    aoTexture_.Reset();
    aoTextureTemp_.Reset();
    rtvHeap_.Shutdown();
    rootSignature_.Reset();
    gtaoPSO_.Reset();
    filterPSO_.Reset();
    fullscreenVertexShader_.Shutdown();
    gtaoShader_.Shutdown();
    filterShader_.Shutdown();
    constantBuffer_.Shutdown();
    if (heapManager_ && depthSrvAllocation_.count != 0) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, depthSrvAllocation_);
    }
    depthSrvAllocation_ = DescriptorAllocation();
    device_ = nullptr;
    srvHeap_ = nullptr;
    heapManager_ = nullptr;
    width_ = 0;
    height_ = 0;
    initialized_ = false;
    renderEvidenceLogged_ = false;
    filterEvidenceLogged_ = false;
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
    , heapManager_(nullptr)
    , width_(0)
    , height_(0)
    , initialized_(false)
    , renderEvidenceLogged_(false)
    , blurEvidenceLogged_(false) {
}

HBAO::~HBAO() {
    Shutdown();
}

bool HBAO::Initialize(DX12Device* device,
                     DX12DescriptorHeap* srvHeap,
                     DX12DescriptorHeapManager* heapManager,
                     uint32_t width,
                     uint32_t height) {
    if (!device || !device->GetDevice() || !srvHeap || !srvHeap->GetHeap() ||
        !srvHeap->IsShaderVisible() || !heapManager || width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid device or heap for HBAO");
        return false;
    }

    Shutdown();

    device_ = device;
    srvHeap_ = srvHeap;
    heapManager_ = heapManager;
    width_ = width;
    height_ = height;
    renderEvidenceLogged_ = false;
    blurEvidenceLogged_ = false;

    // Create resources
    if (!CreateResources()) {
        NEXT_LOG_ERROR("Failed to create HBAO resources");
        Shutdown();
        return false;
    }

    // Create shaders
    if (!CreateShaders()) {
        NEXT_LOG_ERROR("Failed to create HBAO shaders");
        Shutdown();
        return false;
    }

    // Create root signature
    if (!CreateRootSignature()) {
        NEXT_LOG_ERROR("Failed to create HBAO root signature");
        Shutdown();
        return false;
    }

    // Create pipeline states
    if (!CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create HBAO pipeline states");
        Shutdown();
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("HBAO initialized: %ux%u", width, height);
    return true;
}

bool HBAO::CreateResources() {
    if (!rtvHeap_.Initialize(device_, 2)) {
        NEXT_LOG_ERROR("Failed to create HBAO RTV heap");
        return false;
    }

    depthSrvAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    if (depthSrvAllocation_.count < 2 || depthSrvAllocation_.gpuHandle.ptr == 0) {
        NEXT_LOG_ERROR("Failed to allocate HBAO depth/blur SRV descriptors");
        return false;
    }

    D3D12_RESOURCE_DESC aoDesc = {};
    aoDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    aoDesc.Alignment = 0;
    aoDesc.Width = width_;
    aoDesc.Height = height_;
    aoDesc.DepthOrArraySize = 1;
    aoDesc.MipLevels = 1;
    aoDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    aoDesc.SampleDesc.Count = 1;
    aoDesc.SampleDesc.Quality = 0;
    aoDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    aoDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = aoDesc.Format;
    clearValue.Color[0] = 1.0f;
    clearValue.Color[1] = 1.0f;
    clearValue.Color[2] = 1.0f;
    clearValue.Color[3] = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &aoDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
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
        &clearValue,
        IID_PPV_ARGS(&aoTextureTemp_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create HBAO temp texture");
        return false;
    }

    if (!constantBuffer_.Initialize(device_, sizeof(HBAOConstantsGPU))) {
        NEXT_LOG_ERROR("Failed to create HBAO constant buffer");
        return false;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = aoDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    const D3D12_CPU_DESCRIPTOR_HANDLE aoRtv = rtvHeap_.GetCPUDescriptorHandle(0);
    const D3D12_CPU_DESCRIPTOR_HANDLE tempRtv = rtvHeap_.GetCPUDescriptorHandle(1);
    if (aoRtv.ptr == 0 || tempRtv.ptr == 0) {
        NEXT_LOG_ERROR("Invalid HBAO RTV descriptor handles");
        return false;
    }
    device_->GetDevice()->CreateRenderTargetView(aoTexture_.Get(), &rtvDesc, aoRtv);
    device_->GetDevice()->CreateRenderTargetView(aoTextureTemp_.Get(), &rtvDesc, tempRtv);

    return true;
}

bool HBAO::CreateShaders() {
    const std::string vsPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/post_process.vs.hlsl");
    const std::string hbaoPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/hbao.hlsl");

    if (!fullscreenVertexShader_.LoadFromFile(device_, vsPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile HBAO fullscreen vertex shader: %s", vsPath.c_str());
        return false;
    }

    if (!hbaoShader_.CompileFromFile(device_, hbaoPath.c_str(), "PSHBAO", "ps_5_1")) {
        NEXT_LOG_ERROR("Failed to compile HBAO shader: %s", hbaoPath.c_str());
        return false;
    }

    if (!blurShader_.CompileFromFile(device_, hbaoPath.c_str(), "PSBlur", "ps_5_1")) {
        NEXT_LOG_ERROR("Failed to compile HBAO blur shader: %s", hbaoPath.c_str());
        return false;
    }

    return true;
}

bool HBAO::CreateRootSignature() {
    D3D12_ROOT_PARAMETER rootParameters[2] = {};

    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE depthRange = {};
    depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depthRange.NumDescriptors = 1;
    depthRange.BaseShaderRegister = 0;
    depthRange.RegisterSpace = 0;
    depthRange.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &depthRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 2;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = &sampler;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("HBAO root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("HBAO root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    hr = device_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create HBAO root signature: 0x%X", hr);
        return false;
    }

    return true;
}

bool HBAO::CreatePipelineStates() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.VS = fullscreenVertexShader_.GetBytecode();
    psoDesc.PS = hbaoShader_.GetPixelShader();

    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    HRESULT hr = device_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&hbaoPSO_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create HBAO pipeline state: 0x%X", hr);
        return false;
    }

    psoDesc.PS = blurShader_.GetPixelShader();
    hr = device_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&blurPSO_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create HBAO blur pipeline state: 0x%X", hr);
        return false;
    }

    return true;
}

void HBAO::Render(ID3D12GraphicsCommandList* commandList,
                 ID3D12Resource* depthBuffer,
                 ID3D12Resource* normalBuffer,
                 ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    if (!commandList || !aoTexture_) {
        NEXT_LOG_ERROR("Cannot render HBAO: invalid command list or AO texture");
        return;
    }

    if (!depthBuffer || !hbaoPSO_ || !rootSignature_ ||
        !srvHeap_ || !srvHeap_->GetHeap() ||
        depthSrvAllocation_.gpuHandle.ptr == 0 ||
        constantBuffer_.GetGPUVirtualAddress() == 0) {
        ClearAmbientOcclusionTexture(commandList, aoTexture_.Get(), rtvHeap_.GetCPUDescriptorHandle(0));
        NEXT_LOG_WARNING("HBAO fell back to neutral AO because depth input or pipeline resources are unavailable");
        return;
    }

    if (!UpdateDepthShaderResource(depthBuffer) || !UpdateConstants()) {
        ClearAmbientOcclusionTexture(commandList, aoTexture_.Get(), rtvHeap_.GetCPUDescriptorHandle(0));
        NEXT_LOG_WARNING("HBAO fell back to neutral AO because inputs could not be updated");
        return;
    }

    if (params_.blurEnabled) {
        if (!RenderHBAO(commandList, aoTextureTemp_.Get(), rtvHeap_.GetCPUDescriptorHandle(1)) ||
            !ApplyBlur(commandList)) {
            return;
        }
    } else {
        if (!RenderHBAO(commandList, aoTexture_.Get(), rtvHeap_.GetCPUDescriptorHandle(0))) {
            return;
        }
    }

    if (!renderEvidenceLogged_) {
        NEXT_LOG_INFO("HBAO pass rendered");
        renderEvidenceLogged_ = true;
    }
}

bool HBAO::UpdateDepthShaderResource(ID3D12Resource* depthBuffer) {
    if (!device_ || !device_->GetDevice() || !depthBuffer || depthSrvAllocation_.cpuHandle.ptr == 0) {
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->GetDevice()->CreateShaderResourceView(depthBuffer, &srvDesc, depthSrvAllocation_.cpuHandle);
    return true;
}

bool HBAO::UpdateBlurShaderResource(ID3D12Resource* blurInput) {
    if (!device_ || !device_->GetDevice() || !blurInput || !srvHeap_ || !srvHeap_->GetHeap() ||
        depthSrvAllocation_.cpuHandle.ptr == 0 || depthSrvAllocation_.gpuHandle.ptr == 0 ||
        depthSrvAllocation_.count < 2) {
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE blurSrvCPU = depthSrvAllocation_.cpuHandle;
    blurSrvCPU.ptr += srvHeap_->GetDescriptorSize();

    const D3D12_RESOURCE_DESC inputDesc = blurInput->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = inputDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->GetDevice()->CreateShaderResourceView(blurInput, &srvDesc, blurSrvCPU);
    return true;
}

bool HBAO::UpdateConstants() {
    HBAOConstantsGPU constants = {};
    constants.radiusPixels = std::max(1.0f, params_.radius * 96.0f);
    constants.bias = std::max(0.0f, params_.bias);
    constants.stepCount = static_cast<float>(std::clamp(params_.steps, 2, 8));
    constants.power = std::max(0.001f, params_.power);
    constants.invWidth = width_ > 0 ? 1.0f / static_cast<float>(width_) : 0.0f;
    constants.invHeight = height_ > 0 ? 1.0f / static_cast<float>(height_) : 0.0f;
    constants.depthScale = 128.0f;

    return constantBuffer_.UpdateData(&constants, sizeof(constants));
}

bool HBAO::RenderHBAO(ID3D12GraphicsCommandList* commandList,
                      ID3D12Resource* target,
                      D3D12_CPU_DESCRIPTOR_HANDLE targetRTV) {
    if (!commandList || !target || !hbaoPSO_) {
        return false;
    }

    if (targetRTV.ptr == 0) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(width_);
    scissor.bottom = static_cast<LONG>(height_);

    commandList->OMSetRenderTargets(1, &targetRTV, TRUE, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap* heaps[] = {srvHeap_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(hbaoPSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(0, constantBuffer_.GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(1, depthSrvAllocation_.gpuHandle);
    commandList->DrawInstanced(3, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
    return true;
}

bool HBAO::ApplyBlur(ID3D12GraphicsCommandList* commandList) {
    if (!commandList || !blurPSO_ || !aoTexture_ || !aoTextureTemp_) {
        return false;
    }

    if (!UpdateBlurShaderResource(aoTextureTemp_.Get()) || !UpdateConstants()) {
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_.GetCPUDescriptorHandle(0);
    if (rtv.ptr == 0) {
        return false;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE blurSrvGPU = depthSrvAllocation_.gpuHandle;
    blurSrvGPU.ptr += srvHeap_->GetDescriptorSize();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = aoTexture_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(width_);
    scissor.bottom = static_cast<LONG>(height_);

    commandList->OMSetRenderTargets(1, &rtv, TRUE, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap* heaps[] = {srvHeap_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(blurPSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(0, constantBuffer_.GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(1, blurSrvGPU);
    commandList->DrawInstanced(3, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);

    if (!blurEvidenceLogged_) {
        NEXT_LOG_INFO("HBAO blur pass rendered");
        blurEvidenceLogged_ = true;
    }
    return true;
}

bool HBAO::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid HBAO resize dimensions: %ux%u", width, height);
        return false;
    }

    DX12Device* device = device_;
    DX12DescriptorHeap* srvHeap = srvHeap_;
    DX12DescriptorHeapManager* heapManager = heapManager_;
    Shutdown();
    return Initialize(device, srvHeap, heapManager, width, height);
}

void HBAO::Shutdown() {
    aoTexture_.Reset();
    aoTextureTemp_.Reset();
    rtvHeap_.Shutdown();
    rootSignature_.Reset();
    hbaoPSO_.Reset();
    blurPSO_.Reset();
    fullscreenVertexShader_.Shutdown();
    hbaoShader_.Shutdown();
    blurShader_.Shutdown();
    constantBuffer_.Shutdown();
    if (heapManager_ && depthSrvAllocation_.count != 0) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, depthSrvAllocation_);
    }
    depthSrvAllocation_ = DescriptorAllocation();
    device_ = nullptr;
    srvHeap_ = nullptr;
    heapManager_ = nullptr;
    width_ = 0;
    height_ = 0;
    initialized_ = false;
    renderEvidenceLogged_ = false;
    blurEvidenceLogged_ = false;
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
    , heapManager_(nullptr)
    , width_(0)
    , height_(0)
    , initialized_(false)
    , renderEvidenceLogged_(false)
    , voxelizationUnavailableLogged_(false) {
}

VXAO::~VXAO() {
    Shutdown();
}

bool VXAO::Initialize(DX12Device* device,
                     DX12DescriptorHeap* srvHeap,
                     DX12DescriptorHeapManager* heapManager,
                     uint32_t width,
                     uint32_t height) {
    if (!device || !device->GetDevice() || !srvHeap || !srvHeap->GetHeap() ||
        !srvHeap->IsShaderVisible() || !heapManager || width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid device or heap for VXAO");
        return false;
    }

    Shutdown();

    device_ = device;
    srvHeap_ = srvHeap;
    heapManager_ = heapManager;
    width_ = width;
    height_ = height;
    renderEvidenceLogged_ = false;
    voxelizationUnavailableLogged_ = false;

    // Create resources (including 3D voxel texture)
    if (!CreateResources()) {
        NEXT_LOG_ERROR("Failed to create VXAO resources");
        Shutdown();
        return false;
    }

    // Create shaders
    if (!CreateShaders()) {
        NEXT_LOG_ERROR("Failed to create VXAO shaders");
        Shutdown();
        return false;
    }

    // Create root signatures
    if (!CreateRootSignature()) {
        NEXT_LOG_ERROR("Failed to create VXAO root signature");
        Shutdown();
        return false;
    }

    // Create pipeline states
    if (!CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create VXAO pipeline states");
        Shutdown();
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("VXAO initialized: %ux%u, voxel resolution: %d",
                  width, height, params_.voxelResolution);
    return true;
}

bool VXAO::CreateResources() {
    if (!rtvHeap_.Initialize(device_, 1)) {
        NEXT_LOG_ERROR("Failed to create VXAO RTV heap");
        return false;
    }

    depthSrvAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    if (depthSrvAllocation_.count == 0 || depthSrvAllocation_.gpuHandle.ptr == 0) {
        NEXT_LOG_ERROR("Failed to allocate VXAO depth SRV descriptor");
        return false;
    }

    D3D12_RESOURCE_DESC aoDesc = {};
    aoDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    aoDesc.Alignment = 0;
    aoDesc.Width = width_;
    aoDesc.Height = height_;
    aoDesc.DepthOrArraySize = 1;
    aoDesc.MipLevels = 1;
    aoDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    aoDesc.SampleDesc.Count = 1;
    aoDesc.SampleDesc.Quality = 0;
    aoDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    aoDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = aoDesc.Format;
    clearValue.Color[0] = 1.0f;
    clearValue.Color[1] = 1.0f;
    clearValue.Color[2] = 1.0f;
    clearValue.Color[3] = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &aoDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
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

    if (!constantBuffer_.Initialize(device_, sizeof(VXAOConstantsGPU))) {
        NEXT_LOG_ERROR("Failed to create VXAO constant buffer");
        return false;
    }

    if (!voxelizationBuffer_.Initialize(device_, sizeof(VXAOParameters))) {
        NEXT_LOG_ERROR("Failed to create VXAO voxelization buffer");
        return false;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = aoDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    const D3D12_CPU_DESCRIPTOR_HANDLE aoRtv = rtvHeap_.GetCPUDescriptorHandle(0);
    if (aoRtv.ptr == 0) {
        NEXT_LOG_ERROR("Invalid VXAO RTV descriptor handle");
        return false;
    }
    device_->GetDevice()->CreateRenderTargetView(aoTexture_.Get(), &rtvDesc, aoRtv);

    return true;
}

bool VXAO::CreateShaders() {
    const std::string vsPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/post_process.vs.hlsl");
    const std::string vxaoPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/vxao.hlsl");

    if (!fullscreenVertexShader_.LoadFromFile(device_, vsPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile VXAO fullscreen vertex shader: %s", vsPath.c_str());
        return false;
    }

    if (!vxaoShader_.CompileFromFile(device_, vxaoPath.c_str(), "PSVXAO", "ps_5_1")) {
        NEXT_LOG_ERROR("Failed to compile VXAO shader: %s", vxaoPath.c_str());
        return false;
    }

    return true;
}

bool VXAO::CreateRootSignature() {
    D3D12_ROOT_PARAMETER rootParameters[2] = {};

    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_DESCRIPTOR_RANGE depthRange = {};
    depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depthRange.NumDescriptors = 1;
    depthRange.BaseShaderRegister = 0;
    depthRange.RegisterSpace = 0;
    depthRange.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &depthRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 2;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = &sampler;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("VXAO root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("VXAO root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    hr = device_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXAO root signature: 0x%X", hr);
        return false;
    }

    return true;
}

bool VXAO::CreatePipelineStates() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.VS = fullscreenVertexShader_.GetBytecode();
    psoDesc.PS = vxaoShader_.GetPixelShader();

    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    HRESULT hr = device_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&vxaoPSO_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXAO pipeline state: 0x%X", hr);
        return false;
    }

    return true;
}

void VXAO::Render(ID3D12GraphicsCommandList* commandList,
                 ID3D12Resource* depthBuffer,
                 ID3D12Resource* normalBuffer,
                 ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    if (!commandList || !aoTexture_) {
        NEXT_LOG_ERROR("Cannot render VXAO: invalid command list or AO texture");
        return;
    }

    if (params_.enableVoxelization) {
        VoxelizeScene(commandList);
    }

    if (!depthBuffer || !vxaoPSO_ || !rootSignature_ ||
        !srvHeap_ || !srvHeap_->GetHeap() ||
        depthSrvAllocation_.gpuHandle.ptr == 0 ||
        constantBuffer_.GetGPUVirtualAddress() == 0) {
        ClearAmbientOcclusionTexture(commandList, aoTexture_.Get(), rtvHeap_.GetCPUDescriptorHandle(0));
        NEXT_LOG_WARNING("VXAO fell back to neutral AO because depth input or pipeline resources are unavailable");
        return;
    }

    if (!UpdateDepthShaderResource(depthBuffer) || !UpdateConstants()) {
        ClearAmbientOcclusionTexture(commandList, aoTexture_.Get(), rtvHeap_.GetCPUDescriptorHandle(0));
        NEXT_LOG_WARNING("VXAO fell back to neutral AO because inputs could not be updated");
        return;
    }

    if (!RenderVXAO(commandList)) {
        return;
    }

    if (!renderEvidenceLogged_) {
        NEXT_LOG_INFO("VXAO pass rendered");
        renderEvidenceLogged_ = true;
    }
}

void VXAO::VoxelizeScene(ID3D12GraphicsCommandList* commandList) {
    if (!commandList || !voxelizationPSO_) {
        if (!voxelizationUnavailableLogged_) {
            NEXT_LOG_WARNING("VXAO dynamic voxelization was requested, but no voxelization pipeline is configured; depth cone tracing remains active");
            voxelizationUnavailableLogged_ = true;
        }
        return;
    }
}

bool VXAO::UpdateDepthShaderResource(ID3D12Resource* depthBuffer) {
    if (!device_ || !device_->GetDevice() || !depthBuffer || depthSrvAllocation_.cpuHandle.ptr == 0) {
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->GetDevice()->CreateShaderResourceView(depthBuffer, &srvDesc, depthSrvAllocation_.cpuHandle);
    return true;
}

bool VXAO::UpdateConstants() {
    VXAOConstantsGPU constants = {};
    constants.radiusPixels = std::max(1.0f, params_.range * 128.0f);
    constants.coneSampleCount = static_cast<float>(std::clamp(params_.coneSamples, 2, 12));
    constants.coneDirectionCount = static_cast<float>(std::clamp(params_.coneDirections, 4, 8));
    constants.power = std::max(0.001f, params_.hardness);
    constants.invWidth = width_ > 0 ? 1.0f / static_cast<float>(width_) : 0.0f;
    constants.invHeight = height_ > 0 ? 1.0f / static_cast<float>(height_) : 0.0f;
    constants.depthScale = 128.0f;
    constants.hardness = std::max(0.001f, params_.hardness);

    return constantBuffer_.UpdateData(&constants, sizeof(constants));
}

bool VXAO::RenderVXAO(ID3D12GraphicsCommandList* commandList) {
    if (!commandList || !vxaoPSO_) {
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_.GetCPUDescriptorHandle(0);
    if (rtv.ptr == 0) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = aoTexture_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(width_);
    scissor.bottom = static_cast<LONG>(height_);

    commandList->OMSetRenderTargets(1, &rtv, TRUE, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap* heaps[] = {srvHeap_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(vxaoPSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(0, constantBuffer_.GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(1, depthSrvAllocation_.gpuHandle);
    commandList->DrawInstanced(3, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
    return true;
}

bool VXAO::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid VXAO resize dimensions: %ux%u", width, height);
        return false;
    }

    DX12Device* device = device_;
    DX12DescriptorHeap* srvHeap = srvHeap_;
    DX12DescriptorHeapManager* heapManager = heapManager_;
    Shutdown();
    return Initialize(device, srvHeap, heapManager, width, height);
}

void VXAO::Shutdown() {
    aoTexture_.Reset();
    voxelTexture_.Reset();
    voxelTextureTemp_.Reset();
    voxelUAV_.Reset();
    rtvHeap_.Shutdown();
    rootSignature_.Reset();
    voxelizationRootSignature_.Reset();
    vxaoPSO_.Reset();
    voxelizationPSO_.Reset();
    fullscreenVertexShader_.Shutdown();
    vxaoShader_.Shutdown();
    voxelizationShader_.Shutdown();
    constantBuffer_.Shutdown();
    voxelizationBuffer_.Shutdown();
    if (heapManager_ && depthSrvAllocation_.count != 0) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, depthSrvAllocation_);
    }
    depthSrvAllocation_ = DescriptorAllocation();
    device_ = nullptr;
    srvHeap_ = nullptr;
    heapManager_ = nullptr;
    width_ = 0;
    height_ = 0;
    initialized_ = false;
    renderEvidenceLogged_ = false;
    voxelizationUnavailableLogged_ = false;
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
    , heapManager_(nullptr)
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
                                        DX12DescriptorHeapManager* heapManager,
                                        uint32_t width, uint32_t height) {
    if (!device || !device->GetDevice() || !srvHeap || !srvHeap->GetHeap() ||
        !srvHeap->IsShaderVisible() || !heapManager || width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid device or heap for AO manager");
        return false;
    }

    Shutdown();

    device_ = device;
    srvHeap_ = srvHeap;
    heapManager_ = heapManager;
    width_ = width;
    height_ = height;

    // Create all AO technique instances
    gtao_ = new GTAO();
    hbao_ = new HBAO();
    vxao_ = new VXAO();

    // Initialize GTAO by default (best quality/performance balance)
    if (!gtao_->Initialize(device, srvHeap, heapManager, width, height)) {
        NEXT_LOG_ERROR("Failed to initialize GTAO");
        Shutdown();
        return false;
    }

    const bool requireFullDX12UPath = IsTruthyEnv("NEXT_REQUIRE_DX12U");

    // Initialize HBAO
    if (!hbao_->Initialize(device, srvHeap, heapManager, width, height)) {
        if (requireFullDX12UPath) {
            NEXT_LOG_ERROR("NEXT_REQUIRE_DX12U is set, but HBAO failed to initialize");
            Shutdown();
            return false;
        }
        NEXT_LOG_WARNING("Failed to initialize HBAO - will not be available");
    }

    // Initialize VXAO
    if (!vxao_->Initialize(device, srvHeap, heapManager, width, height)) {
        if (requireFullDX12UPath) {
            NEXT_LOG_ERROR("NEXT_REQUIRE_DX12U is set, but VXAO failed to initialize");
            Shutdown();
            return false;
        }
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

    if (width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid AO manager resize dimensions: %ux%u", width, height);
        return false;
    }

    bool ok = true;
    if (gtao_) ok = gtao_->Resize(width, height) && ok;
    if (hbao_) ok = hbao_->Resize(width, height) && ok;
    if (vxao_) ok = vxao_->Resize(width, height) && ok;

    if (!ok || (currentAO_ && !currentAO_->IsInitialized())) {
        NEXT_LOG_ERROR("AO manager resize left the active AO technique unavailable");
        currentAO_ = nullptr;
        currentType_ = AOType::None;
        return false;
    }

    width_ = width;
    height_ = height;

    return ok;
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
    device_ = nullptr;
    srvHeap_ = nullptr;
    heapManager_ = nullptr;
    width_ = 0;
    height_ = 0;
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
