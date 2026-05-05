#include "next/renderer/dx12/global_illumination.h"
#include "next/renderer/dx12/shader_compiler.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>
#include <windows.h>
#include <d3dcompiler.h>

namespace Next {

namespace {

constexpr const wchar_t* kRTGIRayGenShader = L"RayGen";

struct GICombineConstantsGPU {
    float giIntensity;
    float indirectIntensity;
    float aoStrength;
    float probeStrength;
    float screenSpaceStrength;
    float padding0;
    float padding1;
    float padding2;
};

uint64_t AlignUp(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t FloatToBits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
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

bool CreateBufferResource(DX12Device* device,
                          uint64_t size,
                          D3D12_HEAP_TYPE heapType,
                          D3D12_RESOURCE_FLAGS flags,
                          D3D12_RESOURCE_STATES initialState,
                          Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
                          const char* debugName) {
    if (!device || !device->GetDevice() || size == 0) {
        return false;
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = heapType;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create %s buffer: 0x%X", debugName, hr);
        return false;
    }

    return true;
}

uint16_t CalculateMipLevels(int resolution) {
    uint16_t levels = 1;
    while (resolution > 1) {
        resolution >>= 1;
        ++levels;
    }
    return levels;
}

void ClearTexture2D(ID3D12GraphicsCommandList* commandList,
                    ID3D12Resource* resource,
                    D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                    const float color[4]) {
    if (!commandList || !resource || rtv.ptr == 0) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    commandList->ClearRenderTargetView(rtv, color, 0, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

bool CopyTexture2D(ID3D12GraphicsCommandList* commandList,
                   ID3D12Resource* source,
                   ID3D12Resource* destination) {
    if (!commandList || !source || !destination || source == destination) {
        return false;
    }

    const D3D12_RESOURCE_DESC sourceDesc = source->GetDesc();
    const D3D12_RESOURCE_DESC destinationDesc = destination->GetDesc();
    if (sourceDesc.Dimension != destinationDesc.Dimension ||
        sourceDesc.Width != destinationDesc.Width ||
        sourceDesc.Height != destinationDesc.Height ||
        sourceDesc.Format != destinationDesc.Format) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = source;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = destination;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(2, barriers);

    commandList->CopyResource(destination, source);

    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(2, barriers);

    return true;
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

} // namespace

//=============================================================================
// Global Illumination Implementation
//=============================================================================

GlobalIllumination::GlobalIllumination()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , heapManager_(nullptr)
    , width_(0)
    , height_(0)
    , frameCount_(0)
    , initialized_(false) {
}

GlobalIllumination::~GlobalIllumination() {
    Shutdown();
}

bool GlobalIllumination::Initialize(DX12Device* device,
                                   DX12DescriptorHeap* srvHeap,
                                   DX12DescriptorHeapManager* heapManager,
                                   uint32_t width,
                                   uint32_t height) {
    if (!device || !device->GetDevice() || !srvHeap || !srvHeap->GetHeap() ||
        !srvHeap->IsShaderVisible() || !heapManager || width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid device or heap for GI system");
        return false;
    }

    Shutdown();

    device_ = device;
    srvHeap_ = srvHeap;
    heapManager_ = heapManager;
    width_ = width;
    height_ = height;
    frameCount_ = 0;

    // Initialize subsystems
    if (!aoManager_.Initialize(device, srvHeap, heapManager, width, height)) {
        NEXT_LOG_ERROR("Failed to initialize AO manager");
        Shutdown();
        return false;
    }

    if (!probeManager_.Initialize(device, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize probe manager");
        Shutdown();
        return false;
    }

    if (!screenSpaceGI_.Initialize(device, srvHeap, width, height)) {
        if (IsTruthyEnv("NEXT_REQUIRE_DX12U")) {
            NEXT_LOG_ERROR("NEXT_REQUIRE_DX12U is set, but screen space GI failed to initialize");
            Shutdown();
            return false;
        }
        NEXT_LOG_WARNING("Failed to initialize screen space GI - will not be available");
    }

    // Create resources
    if (!CreateResources()) {
        NEXT_LOG_ERROR("Failed to create GI resources");
        Shutdown();
        return false;
    }

    // Create shaders
    if (!CreateShaders()) {
        NEXT_LOG_ERROR("Failed to create GI shaders");
        Shutdown();
        return false;
    }

    // Create root signature
    if (!CreateRootSignature()) {
        NEXT_LOG_ERROR("Failed to create GI root signature");
        Shutdown();
        return false;
    }

    // Create pipeline states
    if (!CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create GI pipeline states");
        Shutdown();
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("Global Illumination system initialized: %ux%u", width, height);
    return true;
}

bool GlobalIllumination::CreateResources() {
    if (!rtvHeap_.Initialize(device_, 6)) {
        NEXT_LOG_ERROR("Failed to create GI RTV heap");
        return false;
    }

    if (combineSrvAllocation_.count < 3) {
        if (combineSrvAllocation_.count != 0 && heapManager_) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, combineSrvAllocation_);
            combineSrvAllocation_ = DescriptorAllocation();
        }

        combineSrvAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);
        if (combineSrvAllocation_.count < 3 || combineSrvAllocation_.gpuHandle.ptr == 0) {
            NEXT_LOG_ERROR("Failed to allocate GI combine SRV descriptors");
            return false;
        }
    }

    // Create GI textures in a UAV-capable HDR format for compute and DXR resolve paths.
    D3D12_RESOURCE_DESC giDesc = {};
    giDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    giDesc.Alignment = 0;
    giDesc.Width = width_;
    giDesc.Height = height_;
    giDesc.DepthOrArraySize = 1;
    giDesc.MipLevels = 1;
    giDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    giDesc.SampleDesc.Count = 1;
    giDesc.SampleDesc.Quality = 0;
    giDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    giDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = giDesc.Format;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    // AO output
    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &giDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&aoOutput_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create AO output texture");
        return false;
    }

    // Probe output
    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &giDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&probeOutput_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create probe output texture");
        return false;
    }

    // Screen space GI output
    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &giDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&ssGIOutput_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create SS GI output texture");
        return false;
    }

    // Final GI output
    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &giDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&giOutput_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GI output texture");
        return false;
    }

    // Temporal history
    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &giDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&giHistory_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GI history texture");
        return false;
    }

    // History temp (ping-pong)
    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &giDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&giHistoryTemp_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GI history temp texture");
        return false;
    }

    // Create settings buffer
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = sizeof(GICombineConstantsGPU);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CreationNodeMask = 1;
    uploadHeap.VisibleNodeMask = 1;

    hr = device_->GetDevice()->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&settingsBuffer_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GI settings buffer");
        return false;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = giDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[6] = {};
    for (UINT i = 0; i < 6; ++i) {
        rtvs[i] = rtvHeap_.GetCPUDescriptorHandle(i);
        if (rtvs[i].ptr == 0) {
            NEXT_LOG_ERROR("Invalid GI RTV descriptor handle at index %u", i);
            return false;
        }
    }
    device_->GetDevice()->CreateRenderTargetView(aoOutput_.Get(), &rtvDesc, rtvs[0]);
    device_->GetDevice()->CreateRenderTargetView(probeOutput_.Get(), &rtvDesc, rtvs[1]);
    device_->GetDevice()->CreateRenderTargetView(ssGIOutput_.Get(), &rtvDesc, rtvs[2]);
    device_->GetDevice()->CreateRenderTargetView(giOutput_.Get(), &rtvDesc, rtvs[3]);
    device_->GetDevice()->CreateRenderTargetView(giHistory_.Get(), &rtvDesc, rtvs[4]);
    device_->GetDevice()->CreateRenderTargetView(giHistoryTemp_.Get(), &rtvDesc, rtvs[5]);

    return true;
}

bool GlobalIllumination::CreateShaders() {
    const std::string vsPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/post_process.vs.hlsl");
    const std::string psPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/gi_combine.ps.hlsl");

    if (!combineVertexShader_.LoadFromFile(device_, vsPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile GI combine vertex shader: %s", vsPath.c_str());
        return false;
    }

    if (!combineShader_.LoadFromFile(device_, psPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile GI combine pixel shader: %s", psPath.c_str());
        return false;
    }

    return true;
}

bool GlobalIllumination::CreateRootSignature() {
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 3;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[0].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].Descriptor.ShaderRegister = 0;
    rootParameters[1].Descriptor.RegisterSpace = 0;
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

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error);
    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("GI combine root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("GI combine root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    hr = device_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&combineRootSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GI combine root signature: 0x%X", hr);
        return false;
    }

    return true;
}

bool GlobalIllumination::CreatePipelineStates() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = combineRootSignature_.Get();
    psoDesc.VS = combineVertexShader_.GetBytecode();
    psoDesc.PS = combineShader_.GetPixelShader();

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

    HRESULT hr = device_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&combinePSO_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GI combine pipeline state: 0x%X", hr);
        return false;
    }

    return true;
}

void GlobalIllumination::Update(ID3D12GraphicsCommandList* commandList,
                               ID3D12Resource* depthBuffer,
                               ID3D12Resource* normalBuffer,
                               ID3D12Resource* colorBuffer) {
    if (!initialized_) {
        return;
    }

    // Skip updates based on frequency setting
    if (settings_.updateFrequency > 1 && (frameCount_ % settings_.updateFrequency) != 0) {
        return;
    }

    // Update subsystems
    RenderAmbientOcclusion(commandList, depthBuffer, normalBuffer);
    RenderLightProbes(commandList);
    RenderScreenSpaceGI(commandList);

    frameCount_++;
}

void GlobalIllumination::Render(ID3D12GraphicsCommandList* commandList,
                               ID3D12Resource* output) {
    if (!initialized_ || !commandList || !output) {
        return;
    }

    // Combine all GI techniques
    CombineGI(commandList);
}

void GlobalIllumination::RenderAmbientOcclusion(ID3D12GraphicsCommandList* commandList,
                                                ID3D12Resource* depthBuffer,
                                                ID3D12Resource* normalBuffer) {
    // Render AO based on current type
    aoManager_.Render(commandList, depthBuffer, normalBuffer, aoOutput_.Get());

    ID3D12Resource* aoTexture = aoManager_.GetOutputTexture();
    if (aoTexture && aoTexture != aoOutput_.Get()) {
        if (!CopyTexture2D(commandList, aoTexture, aoOutput_.Get())) {
            const float neutralAO[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            ClearTexture2D(commandList, aoOutput_.Get(), rtvHeap_.GetCPUDescriptorHandle(0), neutralAO);
        }
    }
}

void GlobalIllumination::RenderLightProbes(ID3D12GraphicsCommandList* commandList) {
    const float probeClear[4] = {0.02f, 0.025f, 0.035f, 1.0f};
    ClearTexture2D(commandList, probeOutput_.Get(), rtvHeap_.GetCPUDescriptorHandle(1), probeClear);
}

void GlobalIllumination::RenderScreenSpaceGI(ID3D12GraphicsCommandList* commandList) {
    // Render screen-space GI
    if (screenSpaceGI_.IsInitialized()) {
        screenSpaceGI_.Render(commandList, ssGIOutput_.Get());
    }
}

void GlobalIllumination::CombineGI(ID3D12GraphicsCommandList* commandList) {
    if (!commandList || !settingsBuffer_ || !giOutput_ || !combineRootSignature_ || !combinePSO_ ||
        combineSrvAllocation_.gpuHandle.ptr == 0) {
        return;
    }

    const UINT descriptorSize = srvHeap_ ? srvHeap_->GetDescriptorSize() : 0;
    if (descriptorSize == 0) {
        return;
    }

    ID3D12Resource* resources[3] = {
        aoOutput_.Get(),
        probeOutput_.Get(),
        ssGIOutput_.Get()
    };

    for (UINT i = 0; i < 3; ++i) {
        if (!resources[i]) {
            return;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE handle = combineSrvAllocation_.cpuHandle;
        handle.ptr += static_cast<SIZE_T>(descriptorSize) * i;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        const D3D12_RESOURCE_DESC resourceDesc = resources[i]->GetDesc();
        srvDesc.Format = resourceDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = resourceDesc.MipLevels;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        device_->GetDevice()->CreateShaderResourceView(resources[i], &srvDesc, handle);
    }

    void* pData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    HRESULT hr = settingsBuffer_->Map(0, &readRange, &pData);
    if (SUCCEEDED(hr)) {
        GICombineConstantsGPU constants = {};
        constants.giIntensity = std::max(0.0f, settings_.giIntensity);
        constants.indirectIntensity = std::max(0.0f, settings_.indirectIntensity);
        constants.aoStrength = std::clamp(settings_.aoInfluence, 0.0f, 1.0f);
        constants.probeStrength = std::max(0.0f, settings_.probeInfluence);
        constants.screenSpaceStrength = settings_.primaryTechnique == GITechnique::ScreenSpaceGI ||
                                                settings_.primaryTechnique == GITechnique::Hybrid
                                            ? 1.0f
                                            : 0.0f;
        memcpy(pData, &constants, sizeof(constants));
        settingsBuffer_->Unmap(0, nullptr);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_.GetCPUDescriptorHandle(3);
    if (rtv.ptr == 0) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = giOutput_.Get();
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
    commandList->SetGraphicsRootSignature(combineRootSignature_.Get());
    commandList->SetPipelineState(combinePSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootDescriptorTable(0, combineSrvAllocation_.gpuHandle);
    commandList->SetGraphicsRootConstantBufferView(1, settingsBuffer_->GetGPUVirtualAddress());
    commandList->DrawInstanced(3, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

bool GlobalIllumination::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) {
        return false;
    }

    if (width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid GI resize dimensions: %ux%u", width, height);
        return false;
    }

    const bool aoOk = aoManager_.Resize(width, height);
    const bool ssOk = !screenSpaceGI_.IsInitialized() || screenSpaceGI_.Resize(width, height);

    aoOutput_.Reset();
    probeOutput_.Reset();
    ssGIOutput_.Reset();
    giOutput_.Reset();
    giHistory_.Reset();
    giHistoryTemp_.Reset();
    settingsBuffer_.Reset();
    rtvHeap_.Shutdown();

    width_ = width;
    height_ = height;
    if (!aoOk || !ssOk || !CreateResources()) {
        NEXT_LOG_ERROR("Failed to resize GI resources or subsystems");
        Shutdown();
        return false;
    }

    return true;
}

void GlobalIllumination::Shutdown() {
    aoOutput_.Reset();
    probeOutput_.Reset();
    ssGIOutput_.Reset();
    giOutput_.Reset();
    giHistory_.Reset();
    giHistoryTemp_.Reset();
    settingsBuffer_.Reset();
    rtvHeap_.Shutdown();
    combineRootSignature_.Reset();
    combinePSO_.Reset();
    combineVertexShader_.Shutdown();
    combineShader_.Shutdown();
    if (heapManager_ && combineSrvAllocation_.count != 0) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, combineSrvAllocation_);
    }
    combineSrvAllocation_ = DescriptorAllocation();

    // Shutdown subsystems
    aoManager_.Shutdown();
    probeManager_.Shutdown();
    screenSpaceGI_.Shutdown();

    device_ = nullptr;
    srvHeap_ = nullptr;
    heapManager_ = nullptr;
    width_ = 0;
    height_ = 0;
    frameCount_ = 0;
    initialized_ = false;
    NEXT_LOG_INFO("Global Illumination system shutdown complete");
}

//=============================================================================
// VXGI Implementation
//=============================================================================

VXGI::VXGI()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , heapManager_(nullptr)
    , worldMin_(0.0f, 0.0f, 0.0f)
    , worldMax_(10.0f, 10.0f, 10.0f)
    , voxelResolution_(128)
    , initialized_(false)
    , voxelizationEvidenceLogged_(false)
    , coneTraceEvidenceLogged_(false) {
}

VXGI::~VXGI() {
    Shutdown();
}

bool VXGI::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                     DX12DescriptorHeapManager* heapManager,
                     const Vec3& worldMin, const Vec3& worldMax,
                     int voxelResolution) {
    if (!device || !srvHeap || !heapManager || voxelResolution <= 0) {
        NEXT_LOG_ERROR("Invalid device or heap for VXGI");
        return false;
    }
    if (worldMax.x <= worldMin.x || worldMax.y <= worldMin.y || worldMax.z <= worldMin.z) {
        NEXT_LOG_ERROR("Invalid VXGI world bounds");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    heapManager_ = heapManager;
    worldMin_ = worldMin;
    worldMax_ = worldMax;
    voxelResolution_ = voxelResolution;
    voxelizationEvidenceLogged_ = false;
    coneTraceEvidenceLogged_ = false;

    // Create voxel resources
    if (!CreateVoxelResources()) {
        NEXT_LOG_ERROR("Failed to create VXGI voxel resources");
        return false;
    }

    // Create shaders
    if (!CreateShaders()) {
        NEXT_LOG_ERROR("Failed to create VXGI shaders");
        Shutdown();
        return false;
    }

    // Create pipeline states
    if (!CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create VXGI pipeline states");
        Shutdown();
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("VXGI initialized: %d^3 voxels, bounds: [%.1f,%.1f,%.1f] to [%.1f,%.1f,%.1f]",
                  voxelResolution, worldMin.x, worldMin.y, worldMin.z,
                  worldMax.x, worldMax.y, worldMax.z);
    return true;
}

bool VXGI::CreateVoxelResources() {
    // Create 3D voxel textures
    D3D12_RESOURCE_DESC voxelDesc = {};
    voxelDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    voxelDesc.Alignment = 0;
    voxelDesc.Width = voxelResolution_;
    voxelDesc.Height = voxelResolution_;
    voxelDesc.DepthOrArraySize = voxelResolution_;
    voxelDesc.MipLevels = 1;
    voxelDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    voxelDesc.SampleDesc.Count = 1;
    voxelDesc.SampleDesc.Quality = 0;
    voxelDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    voxelDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    // Voxel albedo
    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &voxelDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&voxelAlbedo_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXGI voxel albedo texture");
        return false;
    }

    // Voxel normal
    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &voxelDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&voxelNormal_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXGI voxel normal texture");
        return false;
    }

    // Voxel emission
    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &voxelDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&voxelEmission_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXGI voxel emission texture");
        return false;
    }

    voxelDesc.MipLevels = CalculateMipLevels(voxelResolution_);
    voxelDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &voxelDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&voxelAlbedoMip_)
    );
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXGI mipmapped albedo texture");
        return false;
    }

    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &voxelDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&voxelNormalMip_)
    );
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXGI mipmapped normal texture");
        return false;
    }

    if (voxelUavAllocation_.count < 3) {
        if (voxelUavAllocation_.count != 0) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, voxelUavAllocation_);
            voxelUavAllocation_ = DescriptorAllocation();
        }

        voxelUavAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);
        if (voxelUavAllocation_.count < 3 || voxelUavAllocation_.gpuHandle.ptr == 0) {
            NEXT_LOG_ERROR("Failed to allocate VXGI voxel UAV descriptors");
            return false;
        }
    }

    if (voxelSrvAllocation_.count < 3) {
        if (voxelSrvAllocation_.count != 0) {
            heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, voxelSrvAllocation_);
            voxelSrvAllocation_ = DescriptorAllocation();
        }

        voxelSrvAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);
        if (voxelSrvAllocation_.count < 3 || voxelSrvAllocation_.gpuHandle.ptr == 0) {
            NEXT_LOG_ERROR("Failed to allocate VXGI voxel SRV descriptors");
            return false;
        }
    }

    return true;
}

bool VXGI::CreateShaders() {
    const std::string csPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/vxgi_voxelize.cs.hlsl");
    const std::string vsPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/post_process.vs.hlsl");
    const std::string psPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/vxgi_cone_trace.ps.hlsl");

    if (!voxelizationShader_.LoadFromFile(device_, csPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile VXGI voxelization compute shader: %s", csPath.c_str());
        return false;
    }

    if (!fullscreenVertexShader_.LoadFromFile(device_, vsPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile VXGI fullscreen vertex shader: %s", vsPath.c_str());
        return false;
    }

    if (!coneTraceShader_.CompileFromFile(device_, psPath.c_str(), "main", "ps_5_1")) {
        NEXT_LOG_ERROR("Failed to compile VXGI cone trace shader: %s", psPath.c_str());
        return false;
    }

    return true;
}

bool VXGI::CreatePipelineStates() {
    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    D3D12_DESCRIPTOR_RANGE voxelUavRange = {};
    voxelUavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    voxelUavRange.NumDescriptors = 3;
    voxelUavRange.BaseShaderRegister = 0;
    voxelUavRange.RegisterSpace = 0;
    voxelUavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER voxelRootParameter = {};
    voxelRootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    voxelRootParameter.DescriptorTable.NumDescriptorRanges = 1;
    voxelRootParameter.DescriptorTable.pDescriptorRanges = &voxelUavRange;
    voxelRootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC voxelRootSignatureDesc = {};
    voxelRootSignatureDesc.NumParameters = 1;
    voxelRootSignatureDesc.pParameters = &voxelRootParameter;
    voxelRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    HRESULT hr = D3D12SerializeRootSignature(
        &voxelRootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error);
    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("VXGI voxelization root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("VXGI voxelization root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    hr = device_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&voxelizationRootSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXGI voxelization root signature: 0x%X", hr);
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
    computeDesc.pRootSignature = voxelizationRootSignature_.Get();
    computeDesc.CS = voxelizationShader_.GetComputeShader();
    hr = device_->GetDevice()->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&voxelizationPSO_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXGI voxelization pipeline state: 0x%X", hr);
        return false;
    }

    signature.Reset();
    error.Reset();

    D3D12_DESCRIPTOR_RANGE coneTraceSrvRange = {};
    coneTraceSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    coneTraceSrvRange.NumDescriptors = 3;
    coneTraceSrvRange.BaseShaderRegister = 0;
    coneTraceSrvRange.RegisterSpace = 0;
    coneTraceSrvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER coneTraceRootParameter = {};
    coneTraceRootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    coneTraceRootParameter.DescriptorTable.NumDescriptorRanges = 1;
    coneTraceRootParameter.DescriptorTable.pDescriptorRanges = &coneTraceSrvRange;
    coneTraceRootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC coneTraceSampler = {};
    coneTraceSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    coneTraceSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    coneTraceSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    coneTraceSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    coneTraceSampler.MipLODBias = 0.0f;
    coneTraceSampler.MaxAnisotropy = 1;
    coneTraceSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    coneTraceSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    coneTraceSampler.MinLOD = 0.0f;
    coneTraceSampler.MaxLOD = D3D12_FLOAT32_MAX;
    coneTraceSampler.ShaderRegister = 0;
    coneTraceSampler.RegisterSpace = 0;
    coneTraceSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 1;
    rootSignatureDesc.pParameters = &coneTraceRootParameter;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = &coneTraceSampler;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error);
    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("VXGI cone trace root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("VXGI cone trace root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    hr = device_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&coneTraceRootSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXGI cone trace root signature: 0x%X", hr);
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = coneTraceRootSignature_.Get();
    psoDesc.VS = fullscreenVertexShader_.GetBytecode();
    psoDesc.PS = coneTraceShader_.GetPixelShader();

    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
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

    hr = device_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&coneTracePSO_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create VXGI cone trace pipeline state: 0x%X", hr);
        return false;
    }

    if (!coneTraceRTVHeap_.Initialize(device_, 1)) {
        NEXT_LOG_ERROR("Failed to create VXGI cone trace RTV heap");
        return false;
    }

    return true;
}

void VXGI::Voxelize(ID3D12GraphicsCommandList* commandList,
                   ID3D12Resource* depthBuffer,
                   ID3D12Resource* normalBuffer,
                   ID3D12Resource* colorBuffer) {
    if (!initialized_) {
        return;
    }

    if (!commandList || !voxelizationPSO_) {
        NEXT_LOG_TRACE("VXGI analytic cone trace is active; dynamic voxelization is not bound");
        return;
    }

    if (!voxelAlbedo_ || !voxelNormal_ || !voxelEmission_ ||
        !voxelizationRootSignature_ || voxelUavAllocation_.count < 3 ||
        voxelUavAllocation_.gpuHandle.ptr == 0 || !srvHeap_ || !srvHeap_->GetHeap()) {
        NEXT_LOG_WARNING("VXGI voxelization skipped because resources are unavailable");
        return;
    }

    const UINT descriptorSize = srvHeap_->GetDescriptorSize();
    ID3D12Resource* voxelResources[3] = {
        voxelAlbedo_.Get(),
        voxelNormal_.Get(),
        voxelEmission_.Get()
    };

    for (UINT i = 0; i < 3; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = voxelUavAllocation_.cpuHandle;
        handle.ptr += static_cast<SIZE_T>(descriptorSize) * i;

        const D3D12_RESOURCE_DESC resourceDesc = voxelResources[i]->GetDesc();
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = resourceDesc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.MipSlice = 0;
        uavDesc.Texture3D.FirstWSlice = 0;
        uavDesc.Texture3D.WSize = resourceDesc.DepthOrArraySize;
        device_->GetDevice()->CreateUnorderedAccessView(voxelResources[i], nullptr, &uavDesc, handle);
    }

    ID3D12DescriptorHeap* heaps[] = {srvHeap_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetComputeRootSignature(voxelizationRootSignature_.Get());
    commandList->SetPipelineState(voxelizationPSO_.Get());
    commandList->SetComputeRootDescriptorTable(0, voxelUavAllocation_.gpuHandle);

    const UINT groups = static_cast<UINT>((voxelResolution_ + 3) / 4);
    commandList->Dispatch(groups, groups, groups);

    D3D12_RESOURCE_BARRIER barriers[3] = {};
    for (UINT i = 0; i < 3; ++i) {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[i].UAV.pResource = voxelResources[i];
    }
    commandList->ResourceBarrier(3, barriers);

    if (!voxelizationEvidenceLogged_) {
        NEXT_LOG_INFO("VXGI dynamic voxelization dispatched: %u x %u x %u thread groups", groups, groups, groups);
        voxelizationEvidenceLogged_ = true;
    }
}

void VXGI::ConeTrace(ID3D12GraphicsCommandList* commandList,
                    ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    if (!commandList || !output || !coneTracePSO_ || !coneTraceRootSignature_ ||
        voxelSrvAllocation_.count < 3 || voxelSrvAllocation_.gpuHandle.ptr == 0 ||
        !srvHeap_ || !srvHeap_->GetHeap()) {
        return;
    }

    const D3D12_RESOURCE_DESC outputDesc = output->GetDesc();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = coneTraceRTVHeap_.GetCPUDescriptorHandle(0);
    if (rtv.ptr == 0) {
        return;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = outputDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    device_->GetDevice()->CreateRenderTargetView(output, &rtvDesc, rtv);

    const UINT descriptorSize = srvHeap_->GetDescriptorSize();
    ID3D12Resource* voxelResources[3] = {
        voxelAlbedo_.Get(),
        voxelNormal_.Get(),
        voxelEmission_.Get()
    };

    for (UINT i = 0; i < 3; ++i) {
        if (!voxelResources[i]) {
            return;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE handle = voxelSrvAllocation_.cpuHandle;
        handle.ptr += static_cast<SIZE_T>(descriptorSize) * i;

        const D3D12_RESOURCE_DESC resourceDesc = voxelResources[i]->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = resourceDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture3D.MostDetailedMip = 0;
        srvDesc.Texture3D.MipLevels = resourceDesc.MipLevels;
        srvDesc.Texture3D.ResourceMinLODClamp = 0.0f;
        device_->GetDevice()->CreateShaderResourceView(voxelResources[i], &srvDesc, handle);
    }

    ID3D12DescriptorHeap* heaps[] = {srvHeap_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = output;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(outputDesc.Width);
    viewport.Height = static_cast<float>(outputDesc.Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(outputDesc.Width);
    scissor.bottom = static_cast<LONG>(outputDesc.Height);

    commandList->OMSetRenderTargets(1, &rtv, TRUE, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    commandList->SetGraphicsRootSignature(coneTraceRootSignature_.Get());
    commandList->SetPipelineState(coneTracePSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootDescriptorTable(0, voxelSrvAllocation_.gpuHandle);
    commandList->DrawInstanced(3, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);

    if (!coneTraceEvidenceLogged_) {
        NEXT_LOG_INFO("VXGI cone trace rendered");
        coneTraceEvidenceLogged_ = true;
    }
}

void VXGI::Shutdown() {
    if (voxelUavAllocation_.count != 0 && heapManager_) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, voxelUavAllocation_);
    }
    voxelUavAllocation_ = DescriptorAllocation();
    if (voxelSrvAllocation_.count != 0 && heapManager_) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, voxelSrvAllocation_);
    }
    voxelSrvAllocation_ = DescriptorAllocation();
    voxelAlbedo_.Reset();
    voxelNormal_.Reset();
    voxelEmission_.Reset();
    voxelAlbedoMip_.Reset();
    voxelNormalMip_.Reset();
    voxelizationRootSignature_.Reset();
    voxelizationPSO_.Reset();
    coneTraceRootSignature_.Reset();
    coneTracePSO_.Reset();
    coneTraceRTVHeap_.Shutdown();
    fullscreenVertexShader_.Shutdown();
    voxelizationShader_.Shutdown();
    coneTraceShader_.Shutdown();
    heapManager_ = nullptr;
    srvHeap_ = nullptr;
    device_ = nullptr;
    initialized_ = false;
    voxelizationEvidenceLogged_ = false;
    coneTraceEvidenceLogged_ = false;
}

//=============================================================================
// DDGI Implementation
//=============================================================================

DDGI::DDGI()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , initialized_(false) {
}

DDGI::~DDGI() {
    Shutdown();
}

bool DDGI::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                     const DDGISettings& settings) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for DDGI");
        return false;
    }
    if (settings.probeCountX <= 0 || settings.probeCountY <= 0 || settings.probeCountZ <= 0 ||
        settings.probeResolution <= 0 || settings.raysPerProbe <= 0) {
        NEXT_LOG_ERROR("Invalid DDGI settings");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    settings_ = settings;

    // Create probe volume
    if (!CreateProbeVolume()) {
        NEXT_LOG_ERROR("Failed to create DDGI probe volume");
        return false;
    }

    if (!CreateShaders() || !CreateRootSignatures() || !CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create DDGI render pipeline");
        Shutdown();
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("DDGI initialized: %dx%dx%d probes, %d rays per probe",
                  settings_.probeCountX, settings_.probeCountY, settings_.probeCountZ,
                  settings_.raysPerProbe);
    return true;
}

bool DDGI::CreateProbeVolume() {
    const uint64_t probeCount =
        static_cast<uint64_t>(settings_.probeCountX) *
        static_cast<uint64_t>(settings_.probeCountY) *
        static_cast<uint64_t>(settings_.probeCountZ);
    const uint64_t shBytes = probeCount * 9u * sizeof(Vec3);
    const uint64_t depthBytes =
        probeCount *
        static_cast<uint64_t>(settings_.probeResolution) *
        static_cast<uint64_t>(settings_.probeResolution) *
        sizeof(float);
    const uint64_t offsetBytes = probeCount * sizeof(Vec3);

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    auto createBuffer = [&](uint64_t bytes,
                            Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
                            const char* name) -> bool {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = std::max<uint64_t>(bytes, 1);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        HRESULT hr = device_->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&resource));
        if (FAILED(hr)) {
            NEXT_LOG_ERROR("Failed to create DDGI %s buffer: 0x%X", name, hr);
            return false;
        }
        return true;
    };

    return createBuffer(shBytes, probeSH_, "SH") &&
           createBuffer(depthBytes, probeDepth_, "depth") &&
           createBuffer(offsetBytes, probeOffsets_, "offset") &&
           createBuffer(shBytes, probeHistory_, "history");
}

bool DDGI::CreateShaders() {
    const std::string vsPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/post_process.vs.hlsl");
    const std::string psPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/ddgi_render.ps.hlsl");

    if (!fullscreenVertexShader_.LoadFromFile(device_, vsPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile DDGI fullscreen vertex shader: %s", vsPath.c_str());
        return false;
    }

    if (!renderShader_.CompileFromFile(device_, psPath.c_str(), "main", "ps_5_1")) {
        NEXT_LOG_ERROR("Failed to compile DDGI render shader: %s", psPath.c_str());
        return false;
    }

    return true;
}

bool DDGI::CreateRootSignatures() {
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error);
    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("DDGI root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("DDGI root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    hr = device_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&renderRootSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create DDGI render root signature: 0x%X", hr);
        return false;
    }

    return true;
}

bool DDGI::CreatePipelineStates() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = renderRootSignature_.Get();
    psoDesc.VS = fullscreenVertexShader_.GetBytecode();
    psoDesc.PS = renderShader_.GetPixelShader();

    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
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

    HRESULT hr = device_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&renderPSO_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create DDGI render pipeline state: 0x%X", hr);
        return false;
    }

    if (!renderRTVHeap_.Initialize(device_, 1)) {
        NEXT_LOG_ERROR("Failed to create DDGI render RTV heap");
        return false;
    }

    return true;
}

void DDGI::UpdateProbes(ID3D12GraphicsCommandList* commandList,
                       ID3D12Resource* depthBuffer,
                       ID3D12Resource* normalBuffer) {
    if (!initialized_) {
        return;
    }

    if (!commandList || !updatePSO_) {
        NEXT_LOG_TRACE("DDGI using analytic probe field for this frame");
        return;
    }
}

void DDGI::Render(ID3D12GraphicsCommandList* commandList,
                 ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    if (!commandList || !output || !renderPSO_ || !renderRootSignature_) {
        return;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderRTVHeap_.GetCPUDescriptorHandle(0);
    if (rtv.ptr == 0) {
        return;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    device_->GetDevice()->CreateRenderTargetView(output, &rtvDesc, rtv);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = output;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    const D3D12_RESOURCE_DESC outputDesc = output->GetDesc();
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(outputDesc.Width);
    viewport.Height = static_cast<float>(outputDesc.Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(outputDesc.Width);
    scissor.bottom = static_cast<LONG>(outputDesc.Height);

    commandList->OMSetRenderTargets(1, &rtv, TRUE, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    commandList->SetGraphicsRootSignature(renderRootSignature_.Get());
    commandList->SetPipelineState(renderPSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

void DDGI::Shutdown() {
    probeSH_.Reset();
    probeDepth_.Reset();
    probeOffsets_.Reset();
    probeHistory_.Reset();
    updateRootSignature_.Reset();
    updatePSO_.Reset();
    renderRootSignature_.Reset();
    renderPSO_.Reset();
    renderRTVHeap_.Shutdown();
    fullscreenVertexShader_.Shutdown();
    updateShader_.Shutdown();
    renderShader_.Shutdown();
    initialized_ = false;
}

//=============================================================================
// Ray Traced GI Implementation
//=============================================================================

RayTracedGI::RayTracedGI()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , heapManager_(nullptr)
    , sceneVertexBuffer_(nullptr)
    , sceneIndexBuffer_(nullptr)
    , sceneVertexCount_(0)
    , sceneVertexStride_(0)
    , sceneIndexCount_(0)
    , sceneIndexFormat_(DXGI_FORMAT_UNKNOWN)
    , blasCapacityBytes_(0)
    , blasScratchCapacityBytes_(0)
    , tlasCapacityBytes_(0)
    , tlasScratchCapacityBytes_(0)
    , cameraPosition_(0.0f, 0.0f, -4.0f)
    , cameraForward_(0.0f, 0.0f, 1.0f)
    , cameraRight_(1.0f, 0.0f, 0.0f)
    , cameraUp_(0.0f, 1.0f, 0.0f)
    , cameraTanHalfFovY_(0.5522848f)
    , cameraAspect_(1.35f)
    , dxrAvailable_(false)
    , initialized_(false)
    , accelerationStructuresBuilt_(false)
    , frameIndex_(0) {
}

RayTracedGI::~RayTracedGI() {
    Shutdown();
}

bool RayTracedGI::Initialize(DX12Device* device,
                             DX12DescriptorHeap* srvHeap,
                             DX12DescriptorHeapManager* heapManager,
                             const RayTracedGISettings& settings) {
    if (!device || !srvHeap || !heapManager) {
        NEXT_LOG_ERROR("Invalid device or heap for RTGI");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    heapManager_ = heapManager;
    settings_ = settings;
    frameIndex_ = 0;

    // Check DXR support
    if (!CheckDXRSupport()) {
        NEXT_LOG_WARNING("DXR not available on this device - RTGI disabled");
        return false;
    }

    dxrAvailable_ = true;

    if (!CreateOutputDescriptor() ||
        !CreateGlobalRootSignature() ||
        !CreateRayTracingPipeline() ||
        !CreateShaderTable()) {
        NEXT_LOG_WARNING("DXR available, but RTGI pipeline initialization failed; RTGI disabled");
        Shutdown();
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("RTGI initialized with DXR DispatchRays pipeline");
    return true;
}

bool RayTracedGI::CheckDXRSupport() {
    // Check for DXR support
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 features5 = {};
    HRESULT hr = device_->GetDevice()->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS5,
        &features5,
        sizeof(features5)
    );

    if (SUCCEEDED(hr) && features5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
        NEXT_LOG_INFO("DXR is available (tier: %d)", features5.RaytracingTier);
        return true;
    }

    return false;
}

bool RayTracedGI::CreateOutputDescriptor() {
    outputUavAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    if (outputUavAllocation_.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate RTGI output UAV descriptor");
        return false;
    }

    return true;
}

bool RayTracedGI::CreateGlobalRootSignature() {
    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParameters[3] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[0].DescriptorTable.pDescriptorRanges = &uavRange;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].Constants.ShaderRegister = 0;
    rootParameters[1].Constants.RegisterSpace = 0;
    rootParameters[1].Constants.Num32BitValues = 20;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[2].Descriptor.ShaderRegister = 0;
    rootParameters[2].Descriptor.RegisterSpace = 0;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 3;
    rootSignatureDesc.pParameters = rootParameters;
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
            NEXT_LOG_ERROR("RTGI root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("RTGI root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    hr = device_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&globalRootSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create RTGI global root signature: 0x%X", hr);
        return false;
    }

    return true;
}

bool RayTracedGI::CreateRayTracingPipeline() {
    ShaderCompiler compiler;
    if (!compiler.Initialize()) {
        NEXT_LOG_ERROR("Failed to initialize shader compiler for RTGI");
        return false;
    }

    ShaderCompileConfig config;
    config.sourceFile = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/rtgi.raytracing.hlsl");
    config.targetProfile = "lib_6_3";
    config.entryPoint.clear();
    config.optimisationLevel0 = false;
    config.optimisationLevel3 = true;

    std::vector<uint8_t> dxilLibrary;
    if (!compiler.CompileShader(config, dxilLibrary)) {
        NEXT_LOG_ERROR("Failed to compile RTGI ray tracing shader library");
        return false;
    }

    D3D12_EXPORT_DESC rayGenExport = {};
    rayGenExport.Name = kRTGIRayGenShader;
    rayGenExport.ExportToRename = nullptr;
    rayGenExport.Flags = D3D12_EXPORT_FLAG_NONE;

    D3D12_EXPORT_DESC missExport = {};
    missExport.Name = L"Miss";
    missExport.ExportToRename = nullptr;
    missExport.Flags = D3D12_EXPORT_FLAG_NONE;

    D3D12_EXPORT_DESC closestHitExport = {};
    closestHitExport.Name = L"ClosestHit";
    closestHitExport.ExportToRename = nullptr;
    closestHitExport.Flags = D3D12_EXPORT_FLAG_NONE;

    D3D12_EXPORT_DESC exports[] = {rayGenExport, missExport, closestHitExport};

    D3D12_DXIL_LIBRARY_DESC dxilLibraryDesc = {};
    dxilLibraryDesc.DXILLibrary.pShaderBytecode = dxilLibrary.data();
    dxilLibraryDesc.DXILLibrary.BytecodeLength = dxilLibrary.size();
    dxilLibraryDesc.NumExports = static_cast<UINT>(sizeof(exports) / sizeof(exports[0]));
    dxilLibraryDesc.pExports = exports;

    D3D12_HIT_GROUP_DESC hitGroupDesc = {};
    hitGroupDesc.HitGroupExport = L"PrimaryHitGroup";
    hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    shaderConfig.MaxPayloadSizeInBytes = sizeof(float) * 4;
    shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2;

    LPCWSTR shaderConfigExports[] = {kRTGIRayGenShader, L"Miss", L"PrimaryHitGroup"};
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderConfigAssociation = {};
    shaderConfigAssociation.NumExports = static_cast<UINT>(sizeof(shaderConfigExports) / sizeof(shaderConfigExports[0]));
    shaderConfigAssociation.pExports = shaderConfigExports;

    D3D12_GLOBAL_ROOT_SIGNATURE globalRootSignature = {};
    globalRootSignature.pGlobalRootSignature = globalRootSignature_.Get();

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = 1;

    D3D12_STATE_SUBOBJECT subobjects[6] = {};
    subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobjects[0].pDesc = &dxilLibraryDesc;
    subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[1].pDesc = &hitGroupDesc;
    subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobjects[2].pDesc = &shaderConfig;
    shaderConfigAssociation.pSubobjectToAssociate = &subobjects[2];
    subobjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    subobjects[3].pDesc = &shaderConfigAssociation;
    subobjects[4].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobjects[4].pDesc = &globalRootSignature;
    subobjects[5].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobjects[5].pDesc = &pipelineConfig;

    D3D12_STATE_OBJECT_DESC stateObjectDesc = {};
    stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateObjectDesc.NumSubobjects = static_cast<UINT>(sizeof(subobjects) / sizeof(subobjects[0]));
    stateObjectDesc.pSubobjects = subobjects;

    HRESULT hr = device_->GetDevice()->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&rtpso_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create RTGI ray tracing state object: 0x%X", hr);
        return false;
    }

    hr = rtpso_.As(&rtpsoProperties_);
    if (FAILED(hr) || !rtpsoProperties_) {
        NEXT_LOG_ERROR("Failed to query RTGI state object properties: 0x%X", hr);
        return false;
    }

    return true;
}

bool RayTracedGI::CreateShaderTable() {
    if (!rtpsoProperties_) {
        return false;
    }

    const void* rayGenIdentifier = rtpsoProperties_->GetShaderIdentifier(kRTGIRayGenShader);
    const void* missIdentifier = rtpsoProperties_->GetShaderIdentifier(L"Miss");
    const void* hitGroupIdentifier = rtpsoProperties_->GetShaderIdentifier(L"PrimaryHitGroup");
    if (!rayGenIdentifier || !missIdentifier || !hitGroupIdentifier) {
        NEXT_LOG_ERROR("RTGI shader table identifier lookup failed");
        return false;
    }

    const uint64_t recordStride = AlignUp(
        D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
        D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    const uint64_t tableSize = recordStride * 3;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = tableSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&shaderTable_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create RTGI shader table: 0x%X", hr);
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange = {0, 0};
    hr = shaderTable_->Map(0, &readRange, &mapped);
    if (FAILED(hr) || !mapped) {
        NEXT_LOG_ERROR("Failed to map RTGI shader table: 0x%X", hr);
        return false;
    }

    std::memset(mapped, 0, static_cast<size_t>(tableSize));
    std::memcpy(mapped, rayGenIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(static_cast<uint8_t*>(mapped) + recordStride, missIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(static_cast<uint8_t*>(mapped) + recordStride * 2, hitGroupIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    shaderTable_->Unmap(0, nullptr);

    return true;
}

bool RayTracedGI::EnsureBootstrapSceneGeometry() {
    if (bootstrapVertexBuffer_ && bootstrapIndexBuffer_) {
        return true;
    }

    struct BootstrapVertex {
        float x;
        float y;
        float z;
    };

    const std::array<BootstrapVertex, 8> vertices = {{
        {-4.0f, -1.0f, 0.0f},
        { 4.0f, -1.0f, 0.0f},
        { 4.0f, -1.0f, 8.0f},
        {-4.0f, -1.0f, 8.0f},
        {-4.0f,  3.0f, 0.0f},
        { 4.0f,  3.0f, 0.0f},
        { 4.0f,  3.0f, 8.0f},
        {-4.0f,  3.0f, 8.0f},
    }};

    const std::array<uint32_t, 30> indices = {{
        0, 2, 1, 0, 3, 2,
        3, 6, 2, 3, 7, 6,
        0, 7, 3, 0, 4, 7,
        1, 2, 6, 1, 6, 5,
        4, 5, 6, 4, 6, 7,
    }};

    auto uploadData = [](ID3D12Resource* resource, const void* data, size_t size, const char* label) -> bool {
        void* mapped = nullptr;
        D3D12_RANGE readRange = {0, 0};
        HRESULT hr = resource->Map(0, &readRange, &mapped);
        if (FAILED(hr) || !mapped) {
            NEXT_LOG_ERROR("Failed to map RTGI %s upload buffer: 0x%X", label, hr);
            return false;
        }

        std::memcpy(mapped, data, size);
        resource->Unmap(0, nullptr);
        return true;
    };

    if (!CreateBufferResource(
            device_,
            sizeof(BootstrapVertex) * vertices.size(),
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            bootstrapVertexBuffer_,
            "RTGI bootstrap vertex") ||
        !CreateBufferResource(
            device_,
            sizeof(uint32_t) * indices.size(),
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            bootstrapIndexBuffer_,
            "RTGI bootstrap index")) {
        bootstrapVertexBuffer_.Reset();
        bootstrapIndexBuffer_.Reset();
        return false;
    }

    if (!uploadData(bootstrapVertexBuffer_.Get(), vertices.data(), sizeof(BootstrapVertex) * vertices.size(), "vertex") ||
        !uploadData(bootstrapIndexBuffer_.Get(), indices.data(), sizeof(uint32_t) * indices.size(), "index")) {
        bootstrapVertexBuffer_.Reset();
        bootstrapIndexBuffer_.Reset();
        return false;
    }

    NEXT_LOG_INFO("RTGI bootstrap scene geometry created for DXR smoke coverage");
    return true;
}

void RayTracedGI::SetSceneGeometry(ID3D12Resource* vertexBuffer,
                                   uint32_t vertexCount,
                                   uint32_t vertexStride,
                                   ID3D12Resource* indexBuffer,
                                   uint32_t indexCount,
                                   DXGI_FORMAT indexFormat,
                                   bool forceRebuild) {
    const bool sameGeometry =
        sceneVertexBuffer_ == vertexBuffer &&
        sceneIndexBuffer_ == indexBuffer &&
        sceneVertexCount_ == vertexCount &&
        sceneVertexStride_ == vertexStride &&
        sceneIndexCount_ == indexCount &&
        sceneIndexFormat_ == indexFormat;
    if (sameGeometry && !forceRebuild) {
        return;
    }

    sceneVertexBuffer_ = vertexBuffer;
    sceneIndexBuffer_ = indexBuffer;
    sceneVertexCount_ = vertexCount;
    sceneVertexStride_ = vertexStride;
    sceneIndexCount_ = indexCount;
    sceneIndexFormat_ = indexFormat;
    accelerationStructuresBuilt_ = false;
}

void RayTracedGI::SetCamera(const Vec3& position,
                            const Vec3& target,
                            const Vec3& up,
                            float verticalFovRadians,
                            float aspectRatio) {
    Vec3 forward = (target - position).Normalize();
    if (forward.Length() <= 0.0001f) {
        forward = Vec3(0.0f, 0.0f, 1.0f);
    }

    Vec3 upCandidate = up.Normalize();
    if (upCandidate.Length() <= 0.0001f) {
        upCandidate = Vec3(0.0f, 1.0f, 0.0f);
    }

    Vec3 right = upCandidate.Cross(forward).Normalize();
    if (right.Length() <= 0.0001f) {
        const Vec3 fallbackUp = std::fabs(forward.y) > 0.95f ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(0.0f, 1.0f, 0.0f);
        right = fallbackUp.Cross(forward).Normalize();
    }

    Vec3 correctedUp = forward.Cross(right).Normalize();
    if (correctedUp.Length() <= 0.0001f) {
        correctedUp = Vec3(0.0f, 1.0f, 0.0f);
    }

    cameraPosition_ = position;
    cameraForward_ = forward;
    cameraRight_ = right;
    cameraUp_ = correctedUp;
    cameraTanHalfFovY_ = std::max(0.001f, std::tan(std::max(0.001f, verticalFovRadians) * 0.5f));
    cameraAspect_ = std::max(0.001f, aspectRatio);
}

bool RayTracedGI::BuildAccelerationStructures(ID3D12GraphicsCommandList* commandList) {
    if (accelerationStructuresBuilt_) {
        return true;
    }

    ID3D12Resource* buildVertexBuffer = sceneVertexBuffer_;
    ID3D12Resource* buildIndexBuffer = sceneIndexBuffer_;
    uint32_t buildVertexCount = sceneVertexCount_;
    uint32_t buildVertexStride = sceneVertexStride_;
    uint32_t buildIndexCount = sceneIndexCount_;
    DXGI_FORMAT buildIndexFormat = sceneIndexFormat_;

    if (!buildVertexBuffer || !buildIndexBuffer || buildVertexCount == 0 ||
        buildVertexStride == 0 || buildIndexCount == 0) {
        if (!EnsureBootstrapSceneGeometry()) {
            NEXT_LOG_ERROR("RTGI could not create bootstrap scene geometry for acceleration structure build");
            return false;
        }

        buildVertexBuffer = bootstrapVertexBuffer_.Get();
        buildIndexBuffer = bootstrapIndexBuffer_.Get();
        buildVertexCount = 8;
        buildVertexStride = sizeof(float) * 3;
        buildIndexCount = 30;
        buildIndexFormat = DXGI_FORMAT_R32_UINT;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> rayCommandList;
    HRESULT hr = commandList->QueryInterface(IID_PPV_ARGS(&rayCommandList));
    if (FAILED(hr) || !rayCommandList) {
        NEXT_LOG_DEBUG("RTGI acceleration structure build skipped; command list does not expose DXR build API");
        return false;
    }

    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
    geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometryDesc.Triangles.VertexBuffer.StartAddress = buildVertexBuffer->GetGPUVirtualAddress();
    geometryDesc.Triangles.VertexBuffer.StrideInBytes = buildVertexStride;
    geometryDesc.Triangles.VertexCount = buildVertexCount;
    geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometryDesc.Triangles.IndexBuffer = buildIndexBuffer->GetGPUVirtualAddress();
    geometryDesc.Triangles.IndexCount = buildIndexCount;
    geometryDesc.Triangles.IndexFormat = buildIndexFormat;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs = {};
    blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blasInputs.NumDescs = 1;
    blasInputs.pGeometryDescs = &geometryDesc;
    blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasInfo = {};
    device_->GetDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &blasInfo);
    if (blasInfo.ResultDataMaxSizeInBytes == 0) {
        NEXT_LOG_ERROR("RTGI BLAS prebuild info was empty");
        return false;
    }

    const uint64_t blasSize = AlignUp(
        blasInfo.ResultDataMaxSizeInBytes,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    const uint64_t blasScratchSize = AlignUp(
        blasInfo.ScratchDataSizeInBytes,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

    auto ensureBuffer = [&](Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
                            uint64_t& capacityBytes,
                            uint64_t requiredBytes,
                            D3D12_HEAP_TYPE heapType,
                            D3D12_RESOURCE_FLAGS flags,
                            D3D12_RESOURCE_STATES initialState,
                            const char* debugName) -> bool {
        if (resource && capacityBytes >= requiredBytes) {
            return true;
        }
        resource.Reset();
        capacityBytes = 0;
        if (!CreateBufferResource(device_, requiredBytes, heapType, flags, initialState, resource, debugName)) {
            return false;
        }
        capacityBytes = requiredBytes;
        return true;
    };

    if (!ensureBuffer(
            blas_,
            blasCapacityBytes_,
            blasSize,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            "RTGI BLAS") ||
        !ensureBuffer(
            blasScratch_,
            blasScratchCapacityBytes_,
            blasScratchSize,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            "RTGI BLAS scratch")) {
        return false;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasBuildDesc = {};
    blasBuildDesc.Inputs = blasInputs;
    blasBuildDesc.DestAccelerationStructureData = blas_->GetGPUVirtualAddress();
    blasBuildDesc.ScratchAccelerationStructureData = blasScratch_->GetGPUVirtualAddress();
    rayCommandList->BuildRaytracingAccelerationStructure(&blasBuildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER blasBarrier = {};
    blasBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    blasBarrier.UAV.pResource = blas_.Get();
    commandList->ResourceBarrier(1, &blasBarrier);

    D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
    instanceDesc.Transform[0][0] = 1.0f;
    instanceDesc.Transform[1][1] = 1.0f;
    instanceDesc.Transform[2][2] = 1.0f;
    instanceDesc.InstanceMask = 0xff;
    instanceDesc.AccelerationStructure = blas_->GetGPUVirtualAddress();

    if (!instanceDescUpload_) {
        if (!CreateBufferResource(
                device_,
                sizeof(instanceDesc),
                D3D12_HEAP_TYPE_UPLOAD,
                D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                instanceDescUpload_,
                "RTGI TLAS instance upload")) {
            return false;
        }
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange = {0, 0};
    hr = instanceDescUpload_->Map(0, &readRange, &mapped);
    if (FAILED(hr) || !mapped) {
        NEXT_LOG_ERROR("Failed to map RTGI TLAS instance upload: 0x%X", hr);
        return false;
    }
    std::memcpy(mapped, &instanceDesc, sizeof(instanceDesc));
    instanceDescUpload_->Unmap(0, nullptr);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs = {};
    tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlasInputs.NumDescs = 1;
    tlasInputs.InstanceDescs = instanceDescUpload_->GetGPUVirtualAddress();
    tlasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasInfo = {};
    device_->GetDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs, &tlasInfo);
    if (tlasInfo.ResultDataMaxSizeInBytes == 0) {
        NEXT_LOG_ERROR("RTGI TLAS prebuild info was empty");
        return false;
    }

    const uint64_t tlasSize = AlignUp(
        tlasInfo.ResultDataMaxSizeInBytes,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    const uint64_t tlasScratchSize = AlignUp(
        tlasInfo.ScratchDataSizeInBytes,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

    if (!ensureBuffer(
            tlas_,
            tlasCapacityBytes_,
            tlasSize,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            "RTGI TLAS") ||
        !ensureBuffer(
            tlasScratch_,
            tlasScratchCapacityBytes_,
            tlasScratchSize,
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            "RTGI TLAS scratch")) {
        return false;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasBuildDesc = {};
    tlasBuildDesc.Inputs = tlasInputs;
    tlasBuildDesc.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress();
    tlasBuildDesc.ScratchAccelerationStructureData = tlasScratch_->GetGPUVirtualAddress();
    rayCommandList->BuildRaytracingAccelerationStructure(&tlasBuildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER tlasBarrier = {};
    tlasBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    tlasBarrier.UAV.pResource = tlas_.Get();
    commandList->ResourceBarrier(1, &tlasBarrier);

    accelerationStructuresBuilt_ = true;
    NEXT_LOG_INFO("RTGI acceleration structures built: %u vertices, %u indices", buildVertexCount, buildIndexCount);
    return true;
}

void RayTracedGI::Render(ID3D12GraphicsCommandList* commandList,
                        ID3D12Resource* depthBuffer,
                        ID3D12Resource* normalBuffer,
                        ID3D12Resource* output) {
    if (!initialized_ || !dxrAvailable_) {
        return;
    }

    if (!commandList || !output || !rtpso_ || !shaderTable_ || outputUavAllocation_.count == 0) {
        NEXT_LOG_DEBUG("RTGI render skipped; no ray tracing state object is bound");
        return;
    }

    if (!BuildAccelerationStructures(commandList)) {
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> rayCommandList;
    HRESULT hr = commandList->QueryInterface(IID_PPV_ARGS(&rayCommandList));
    if (FAILED(hr) || !rayCommandList) {
        NEXT_LOG_DEBUG("RTGI render skipped; command list does not support DispatchRays");
        return;
    }

    const D3D12_RESOURCE_DESC outputDesc = output->GetDesc();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = outputDesc.Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    uavDesc.Texture2D.PlaneSlice = 0;
    device_->GetDevice()->CreateUnorderedAccessView(
        output,
        nullptr,
        &uavDesc,
        outputUavAllocation_.cpuHandle);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = output;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    ID3D12DescriptorHeap* heaps[] = {srvHeap_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);
    rayCommandList->SetPipelineState1(rtpso_.Get());
    rayCommandList->SetComputeRootSignature(globalRootSignature_.Get());
    rayCommandList->SetComputeRootDescriptorTable(0, outputUavAllocation_.gpuHandle);

    const std::array<uint32_t, 20> constants = {
        frameIndex_++,
        static_cast<uint32_t>(settings_.raysPerPixel),
        FloatToBits(settings_.rayLength),
        FloatToBits(settings_.temporalWeight),
        FloatToBits(cameraPosition_.x),
        FloatToBits(cameraPosition_.y),
        FloatToBits(cameraPosition_.z),
        FloatToBits(cameraTanHalfFovY_),
        FloatToBits(cameraForward_.x),
        FloatToBits(cameraForward_.y),
        FloatToBits(cameraForward_.z),
        FloatToBits(cameraAspect_),
        FloatToBits(cameraRight_.x),
        FloatToBits(cameraRight_.y),
        FloatToBits(cameraRight_.z),
        0,
        FloatToBits(cameraUp_.x),
        FloatToBits(cameraUp_.y),
        FloatToBits(cameraUp_.z),
        0,
    };
    rayCommandList->SetComputeRoot32BitConstants(
        1,
        static_cast<UINT>(constants.size()),
        constants.data(),
        0);
    rayCommandList->SetComputeRootShaderResourceView(2, tlas_->GetGPUVirtualAddress());

    const uint64_t recordStride = AlignUp(
        D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
        D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    const D3D12_GPU_VIRTUAL_ADDRESS shaderTableAddress = shaderTable_->GetGPUVirtualAddress();

    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.RayGenerationShaderRecord.StartAddress = shaderTableAddress;
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = recordStride;
    dispatchDesc.MissShaderTable.StartAddress = shaderTableAddress + recordStride;
    dispatchDesc.MissShaderTable.SizeInBytes = recordStride;
    dispatchDesc.MissShaderTable.StrideInBytes = recordStride;
    dispatchDesc.HitGroupTable.StartAddress = shaderTableAddress + recordStride * 2;
    dispatchDesc.HitGroupTable.SizeInBytes = recordStride;
    dispatchDesc.HitGroupTable.StrideInBytes = recordStride;
    dispatchDesc.Width = static_cast<UINT>(outputDesc.Width);
    dispatchDesc.Height = outputDesc.Height;
    dispatchDesc.Depth = 1;
    rayCommandList->DispatchRays(&dispatchDesc);

    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = output;
    commandList->ResourceBarrier(1, &uavBarrier);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

void RayTracedGI::Shutdown() {
    raytracingOutput_.Reset();
    denoisedOutput_.Reset();
    temporalHistory_.Reset();
    shaderTable_.Reset();
    blas_.Reset();
    tlas_.Reset();
    blasScratch_.Reset();
    tlasScratch_.Reset();
    instanceDescUpload_.Reset();
    bootstrapVertexBuffer_.Reset();
    bootstrapIndexBuffer_.Reset();
    blasCapacityBytes_ = 0;
    blasScratchCapacityBytes_ = 0;
    tlasCapacityBytes_ = 0;
    tlasScratchCapacityBytes_ = 0;
    rtpso_.Reset();
    rtpsoProperties_.Reset();
    globalRootSignature_.Reset();
    if (heapManager_ && outputUavAllocation_.count != 0) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, outputUavAllocation_);
    }
    outputUavAllocation_ = DescriptorAllocation();
    dxrAvailable_ = false;
    initialized_ = false;
    accelerationStructuresBuilt_ = false;
    sceneVertexBuffer_ = nullptr;
    sceneIndexBuffer_ = nullptr;
    sceneVertexCount_ = 0;
    sceneVertexStride_ = 0;
    sceneIndexCount_ = 0;
    sceneIndexFormat_ = DXGI_FORMAT_UNKNOWN;
    heapManager_ = nullptr;
    srvHeap_ = nullptr;
    device_ = nullptr;
    frameIndex_ = 0;
}

//=============================================================================
// GI Manager Implementation
//=============================================================================

GIManager::GIManager()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , heapManager_(nullptr)
    , currentTechnique_(GITechnique::Hybrid)
    , width_(0)
    , height_(0)
    , initialized_(false) {
}

GIManager::~GIManager() {
    Shutdown();
}

bool GIManager::Initialize(DX12Device* device,
                           DX12DescriptorHeap* srvHeap,
                           DX12DescriptorHeapManager* heapManager,
                           uint32_t width,
                           uint32_t height) {
    if (!device || !srvHeap || !heapManager || width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid device or heap for GI manager");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    heapManager_ = heapManager;
    width_ = width;
    height_ = height;
    settings_.primaryTechnique = currentTechnique_;

    // Initialize main GI system
    if (!gi_.Initialize(device, srvHeap, heapManager, width, height)) {
        NEXT_LOG_ERROR("Failed to initialize GI system");
        Shutdown();
        return false;
    }
    gi_.SetSettings(settings_);

    const bool requireFullDX12UPath = IsTruthyEnv("NEXT_REQUIRE_DX12U");

    // Initialize VXGI
    Vec3 worldMin(0.0f, 0.0f, 0.0f);
    Vec3 worldMax(20.0f, 10.0f, 20.0f);
    const bool vxgiOk = vxgi_.Initialize(device, srvHeap, heapManager, worldMin, worldMax, 128);
    if (!vxgiOk) {
        if (requireFullDX12UPath) {
            NEXT_LOG_ERROR("NEXT_REQUIRE_DX12U is set, but VXGI failed to initialize");
            Shutdown();
            return false;
        }
        NEXT_LOG_WARNING("Failed to initialize VXGI - will not be available");
    }

    // Initialize DDGI
    DDGISettings ddgiSettings;
    const bool ddgiOk = ddgi_.Initialize(device, srvHeap, ddgiSettings);
    if (!ddgiOk) {
        if (requireFullDX12UPath) {
            NEXT_LOG_ERROR("NEXT_REQUIRE_DX12U is set, but DDGI failed to initialize");
            Shutdown();
            return false;
        }
        NEXT_LOG_WARNING("Failed to initialize DDGI - will not be available");
    }

    // Initialize RTGI
    RayTracedGISettings rtiSettings;
    const bool rtgiOk = rti_.Initialize(device, srvHeap, heapManager, rtiSettings);
    if (!rtgiOk) {
        if (requireFullDX12UPath) {
            NEXT_LOG_ERROR("NEXT_REQUIRE_DX12U is set, but RTGI failed to initialize");
            Shutdown();
            return false;
        }
        NEXT_LOG_WARNING("Failed to initialize RTGI - will not be available");
    }

    initialized_ = true;
    NEXT_LOG_INFO("GI Manager initialized (technique: Hybrid)");
    return true;
}

void GIManager::SetGITechnique(GITechnique technique) {
    currentTechnique_ = technique;
    settings_.primaryTechnique = technique;
    gi_.SetSettings(settings_);
    NEXT_LOG_INFO("GI technique changed to: %d", (int)technique);
}

void GIManager::Update(ID3D12GraphicsCommandList* commandList,
                      ID3D12Resource* depthBuffer,
                      ID3D12Resource* normalBuffer,
                      ID3D12Resource* colorBuffer) {
    if (!initialized_) {
        return;
    }

    switch (currentTechnique_) {
        case GITechnique::LightProbes:
        case GITechnique::ScreenSpaceGI:
        case GITechnique::Hybrid:
            gi_.Update(commandList, depthBuffer, normalBuffer, colorBuffer);
            if (currentTechnique_ == GITechnique::Hybrid && ddgi_.IsInitialized()) {
                ddgi_.UpdateProbes(commandList, depthBuffer, normalBuffer);
            }
            break;

        case GITechnique::VoxelGI:
            gi_.Update(commandList, depthBuffer, normalBuffer, colorBuffer);
            if (vxgi_.IsInitialized()) {
                vxgi_.Voxelize(commandList, depthBuffer, normalBuffer, colorBuffer);
            }
            break;

        default:
            break;
    }
}

void GIManager::Render(ID3D12GraphicsCommandList* commandList,
                      ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    switch (currentTechnique_) {
        case GITechnique::LightProbes:
        case GITechnique::ScreenSpaceGI:
        case GITechnique::Hybrid:
            gi_.Render(commandList, output);
            if (currentTechnique_ == GITechnique::Hybrid) {
                ID3D12Resource* giOutput = gi_.GetGIOutput();
                if (giOutput && ddgi_.IsInitialized()) {
                    ddgi_.Render(commandList, giOutput);
                }
                if (giOutput && vxgi_.IsInitialized()) {
                    vxgi_.ConeTrace(commandList, giOutput);
                }
                if (giOutput && rti_.IsInitialized()) {
                    rti_.Render(commandList, nullptr, nullptr, giOutput);
                }
            }
            break;

        case GITechnique::VoxelGI:
            gi_.Render(commandList, output);
            if (vxgi_.IsInitialized()) {
                vxgi_.ConeTrace(commandList, gi_.GetGIOutput());
            }
            break;

        case GITechnique::None:
        default:
            break;
    }
}

bool GIManager::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) {
        return false;
    }

    if (width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid GI manager resize dimensions: %ux%u", width, height);
        return false;
    }

    width_ = width;
    height_ = height;

    return gi_.Resize(width, height);
}

void GIManager::SetSettings(const GISettings& settings) {
    settings_ = settings;
    gi_.SetSettings(settings);
}

void GIManager::SetRayTracingSceneGeometry(ID3D12Resource* vertexBuffer,
                                           uint32_t vertexCount,
                                           uint32_t vertexStride,
                                           ID3D12Resource* indexBuffer,
                                           uint32_t indexCount,
                                           DXGI_FORMAT indexFormat,
                                           bool forceRebuild) {
    rti_.SetSceneGeometry(vertexBuffer, vertexCount, vertexStride, indexBuffer, indexCount, indexFormat, forceRebuild);
}

void GIManager::SetRayTracingCamera(const Vec3& position,
                                    const Vec3& target,
                                    const Vec3& up,
                                    float verticalFovRadians,
                                    float aspectRatio) {
    rti_.SetCamera(position, target, up, verticalFovRadians, aspectRatio);
}

void GIManager::Shutdown() {
    gi_.Shutdown();
    vxgi_.Shutdown();
    ddgi_.Shutdown();
    rti_.Shutdown();
    heapManager_ = nullptr;
    srvHeap_ = nullptr;
    device_ = nullptr;
    initialized_ = false;
    NEXT_LOG_INFO("GI Manager shutdown complete");
}

} // namespace Next
