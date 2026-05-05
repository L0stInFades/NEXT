#include "next/renderer/dx12/post_processing.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <vector>
#include <windows.h>

namespace Next {

namespace {

struct PostProcessConstantsGPU {
    float bloomIntensity;
    float bloomThreshold;
    float bloomSoftKnee;
    float bloomRadius;
    float minLuminance;
    float maxLuminance;
    float preExposure;
    float exposureBias;
    float contrast;
    float saturation;
    float gamma;
    float temperature;
    float tint;
    float vibrance;
    float adaptationSpeedUp;
    float adaptationSpeedDown;
    float timeSeconds;
    float globalIlluminationIntensity;
    float globalIlluminationAvailable;
    float invFrameWidth;
    float invFrameHeight;
    float passMode;
    float bloomIterations;
    float padding0;
};

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

PostProcessing::PostProcessing()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , intermediateRtvDescriptorSize_(0)
    , postSrvDescriptorSize_(0)
    , bloomState_(D3D12_RESOURCE_STATE_RENDER_TARGET)
    , gradedState_(D3D12_RESOURCE_STATE_RENDER_TARGET)
    , width_(0)
    , height_(0)
    , bloomWidth_(0)
    , bloomHeight_(0)
    , initialized_(false) {
    inputSrvCPU_.ptr = 0;
    inputSrvGPU_.ptr = 0;
    globalIlluminationSrvCPU_.ptr = 0;
    globalIlluminationSrvGPU_.ptr = 0;
    bloomRTV_.ptr = 0;
    gradedRTV_.ptr = 0;
    sceneAndGiSrvCPU_.ptr = 0;
    sceneAndGiSrvGPU_.ptr = 0;
    gradedAndNullSrvCPU_.ptr = 0;
    gradedAndNullSrvGPU_.ptr = 0;
    gradedAndBloomSrvCPU_.ptr = 0;
    gradedAndBloomSrvGPU_.ptr = 0;
}

PostProcessing::~PostProcessing() {
    Shutdown();
}

bool PostProcessing::Initialize(DX12Device* device,
                                DX12DescriptorHeap* srvHeap,
                                D3D12_CPU_DESCRIPTOR_HANDLE inputSrvCPU,
                                D3D12_GPU_DESCRIPTOR_HANDLE inputSrvGPU,
                                uint32_t width,
                                uint32_t height,
                                DXGI_FORMAT outputFormat) {
    if (!device || !device->GetDevice() || !srvHeap || inputSrvCPU.ptr == 0 || inputSrvGPU.ptr == 0 ||
        width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid parameters for post-processing initialization");
        return false;
    }

    if (!srvHeap->GetHeap() || !srvHeap->IsShaderVisible() || srvHeap->GetDescriptorSize() == 0) {
        NEXT_LOG_ERROR("Invalid shader-visible SRV heap for post-processing");
        return false;
    }

    Shutdown();

    NEXT_LOG_INFO("Initializing post-processing: %ux%u", width, height);

    device_ = device;
    srvHeap_ = srvHeap;
    inputSrvCPU_ = inputSrvCPU;
    inputSrvGPU_ = inputSrvGPU;
    const UINT descriptorSize = srvHeap_->GetDescriptorSize();
    globalIlluminationSrvCPU_ = inputSrvCPU_;
    globalIlluminationSrvCPU_.ptr += descriptorSize;
    globalIlluminationSrvGPU_ = inputSrvGPU_;
    globalIlluminationSrvGPU_.ptr += descriptorSize;
    width_ = width;
    height_ = height;

    bloom_ = BloomParameters();
    eyeAdaptation_ = EyeAdaptationParameters();
    colorGrading_ = ColorGradingParameters();

    if (!CreateIntermediateResources()) {
        NEXT_LOG_ERROR("Failed to create post-processing intermediate resources");
        Shutdown();
        return false;
    }

    if (!CreatePipelineResources(outputFormat)) {
        NEXT_LOG_ERROR("Failed to create post-processing pipeline resources");
        Shutdown();
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("Post-processing initialized successfully");
    return true;
}

bool PostProcessing::CreateIntermediateResources() {
    if (!CreateBloomResources()) {
        NEXT_LOG_ERROR("Failed to create bloom resources");
        return false;
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = desc.Format;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue,
        IID_PPV_ARGS(&gradedBuffer_));

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create color grading buffer: 0x%X", hr);
        return false;
    }

    gradedState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;

    if (!CreateIntermediateDescriptors()) {
        NEXT_LOG_ERROR("Failed to create post-processing intermediate descriptors");
        return false;
    }

    NEXT_LOG_INFO("Post-processing intermediate resources created");
    return true;
}

bool PostProcessing::CreateBloomResources() {
    bloomWidth_ = std::max(1u, width_ / 2);
    bloomHeight_ = std::max(1u, height_ / 2);

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = bloomWidth_;
    desc.Height = bloomHeight_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = desc.Format;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue,
        IID_PPV_ARGS(&bloomBuffer_));

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create bloom buffer: 0x%X", hr);
        return false;
    }

    bloomState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;

    NEXT_LOG_INFO("Bloom buffer created: %ux%u", bloomWidth_, bloomHeight_);
    return true;
}

bool PostProcessing::CreateIntermediateDescriptors() {
    if (!device_ || !device_->GetDevice() || !gradedBuffer_ || !bloomBuffer_) {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 2;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;

    HRESULT hr = device_->GetDevice()->CreateDescriptorHeap(
        &rtvHeapDesc,
        IID_PPV_ARGS(&intermediateRtvHeap_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create post-processing RTV heap: 0x%X", hr);
        return false;
    }

    intermediateRtvDescriptorSize_ =
        device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    gradedRTV_ = intermediateRtvHeap_->GetCPUDescriptorHandleForHeapStart();
    bloomRTV_ = gradedRTV_;
    bloomRTV_.ptr += intermediateRtvDescriptorSize_;

    device_->GetDevice()->CreateRenderTargetView(gradedBuffer_.Get(), nullptr, gradedRTV_);
    device_->GetDevice()->CreateRenderTargetView(bloomBuffer_.Get(), nullptr, bloomRTV_);

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = 6;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NodeMask = 0;

    hr = device_->GetDevice()->CreateDescriptorHeap(
        &srvHeapDesc,
        IID_PPV_ARGS(&postSrvHeap_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create post-processing SRV heap: 0x%X", hr);
        return false;
    }

    postSrvDescriptorSize_ =
        device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    sceneAndGiSrvCPU_ = postSrvHeap_->GetCPUDescriptorHandleForHeapStart();
    sceneAndGiSrvGPU_ = postSrvHeap_->GetGPUDescriptorHandleForHeapStart();

    gradedAndNullSrvCPU_ = sceneAndGiSrvCPU_;
    gradedAndNullSrvCPU_.ptr += postSrvDescriptorSize_ * 2;
    gradedAndNullSrvGPU_ = sceneAndGiSrvGPU_;
    gradedAndNullSrvGPU_.ptr += postSrvDescriptorSize_ * 2;

    gradedAndBloomSrvCPU_ = sceneAndGiSrvCPU_;
    gradedAndBloomSrvCPU_.ptr += postSrvDescriptorSize_ * 4;
    gradedAndBloomSrvGPU_ = sceneAndGiSrvGPU_;
    gradedAndBloomSrvGPU_.ptr += postSrvDescriptorSize_ * 4;

    return true;
}

bool PostProcessing::CreatePipelineResources(DXGI_FORMAT outputFormat) {
    const std::string vsPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/post_process.vs.hlsl");
    const std::string psPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/post_process.ps.hlsl");

    if (!vertexShader_.LoadFromFile(device_, vsPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile post-processing vertex shader: %s", vsPath.c_str());
        return false;
    }

    if (!pixelShader_.LoadFromFile(device_, psPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile post-processing pixel shader: %s", psPath.c_str());
        return false;
    }

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 2;
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
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
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
            NEXT_LOG_ERROR("Post-processing root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("Post-processing root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    hr = device_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature));

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create post-processing root signature: 0x%X", hr);
        return false;
    }
    rootSignature_.SetRootSignature(rootSignature.Get());

    std::vector<InputElementDesc> inputLayout;
    if (!intermediatePipelineState_.Initialize(
            device_,
            &rootSignature_,
            &vertexShader_,
            &pixelShader_,
            inputLayout,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)) {
        NEXT_LOG_ERROR("Failed to create post-processing intermediate pipeline state");
        return false;
    }

    if (!pipelineState_.Initialize(
            device_,
            &rootSignature_,
            &vertexShader_,
            &pixelShader_,
            inputLayout,
            outputFormat,
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)) {
        NEXT_LOG_ERROR("Failed to create post-processing pipeline state");
        return false;
    }

    if (!sceneConstantsBuffer_.Initialize(device_, sizeof(PostProcessConstantsGPU)) ||
        !bloomConstantsBuffer_.Initialize(device_, sizeof(PostProcessConstantsGPU)) ||
        !finalConstantsBuffer_.Initialize(device_, sizeof(PostProcessConstantsGPU))) {
        NEXT_LOG_ERROR("Failed to create post-processing constants buffers");
        return false;
    }

    return true;
}

bool PostProcessing::UpdateInputShaderResources(ID3D12Resource* inputFrame,
                                                ID3D12Resource* globalIlluminationFrame,
                                                D3D12_CPU_DESCRIPTOR_HANDLE descriptorStart) {
    if (!device_ || !device_->GetDevice() || !inputFrame ||
        descriptorStart.ptr == 0 || postSrvDescriptorSize_ == 0) {
        return false;
    }

    D3D12_RESOURCE_DESC inputDesc = inputFrame->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = inputDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = inputDesc.MipLevels;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    device_->GetDevice()->CreateShaderResourceView(inputFrame, &srvDesc, descriptorStart);

    D3D12_CPU_DESCRIPTOR_HANDLE secondDescriptor = descriptorStart;
    secondDescriptor.ptr += postSrvDescriptorSize_;

    D3D12_SHADER_RESOURCE_VIEW_DESC giSrvDesc = {};
    giSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    giSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    giSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    giSrvDesc.Texture2D.MostDetailedMip = 0;
    giSrvDesc.Texture2D.MipLevels = 1;
    giSrvDesc.Texture2D.PlaneSlice = 0;
    giSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    if (globalIlluminationFrame) {
        const D3D12_RESOURCE_DESC giDesc = globalIlluminationFrame->GetDesc();
        giSrvDesc.Format = giDesc.Format;
        giSrvDesc.Texture2D.MipLevels = giDesc.MipLevels;
    }

    device_->GetDevice()->CreateShaderResourceView(
        globalIlluminationFrame,
        &giSrvDesc,
        secondDescriptor);
    return true;
}

bool PostProcessing::UpdateConstants(DX12ConstantBuffer& constantsBuffer,
                                     bool globalIlluminationAvailable,
                                     float passMode) {
    PostProcessConstantsGPU constants = {};
    constants.bloomIntensity = bloom_.intensity;
    constants.bloomThreshold = bloom_.threshold;
    constants.bloomSoftKnee = bloom_.softKnee;
    constants.bloomRadius = bloom_.radius;
    constants.minLuminance = std::max(0.001f, eyeAdaptation_.minLuminance);
    constants.maxLuminance = std::max(constants.minLuminance, eyeAdaptation_.maxLuminance);
    constants.preExposure = eyeAdaptation_.preExposure;
    constants.exposureBias = eyeAdaptation_.exposureBias;
    constants.contrast = colorGrading_.contrast;
    constants.saturation = colorGrading_.saturation;
    constants.gamma = colorGrading_.gamma;
    constants.temperature = colorGrading_.temperature;
    constants.tint = colorGrading_.tint;
    constants.vibrance = colorGrading_.vibrance;
    constants.adaptationSpeedUp = std::max(0.0f, eyeAdaptation_.speedUp);
    constants.adaptationSpeedDown = std::max(0.0f, eyeAdaptation_.speedDown);
    constants.globalIlluminationIntensity = std::max(0.0f, globalIlluminationIntensity_);
    constants.globalIlluminationAvailable = globalIlluminationAvailable ? 1.0f : 0.0f;
    constants.invFrameWidth = width_ > 0 ? 1.0f / static_cast<float>(width_) : 1.0f;
    constants.invFrameHeight = height_ > 0 ? 1.0f / static_cast<float>(height_) : 1.0f;
    constants.passMode = passMode;
    constants.bloomIterations = static_cast<float>(std::max(1u, bloom_.iterations));

    return constantsBuffer.UpdateData(&constants, sizeof(constants));
}

void PostProcessing::TransitionIntermediateResource(ID3D12GraphicsCommandList* commandList,
                                                    ID3D12Resource* resource,
                                                    D3D12_RESOURCE_STATES& currentState,
                                                    D3D12_RESOURCE_STATES targetState) {
    if (!commandList || !resource || currentState == targetState) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = currentState;
    barrier.Transition.StateAfter = targetState;
    commandList->ResourceBarrier(1, &barrier);
    currentState = targetState;
}

void PostProcessing::RenderFullscreenPass(ID3D12GraphicsCommandList* commandList,
                                          DX12PipelineState& passPipelineState,
                                          D3D12_CPU_DESCRIPTOR_HANDLE outputRTV,
                                          D3D12_GPU_DESCRIPTOR_HANDLE inputSrvTable,
                                          D3D12_GPU_VIRTUAL_ADDRESS constantsAddress,
                                          uint32_t passWidth,
                                          uint32_t passHeight) {
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(passWidth);
    viewport.Height = static_cast<float>(passHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(passWidth);
    scissor.bottom = static_cast<LONG>(passHeight);

    commandList->OMSetRenderTargets(1, &outputRTV, TRUE, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    commandList->SetGraphicsRootSignature(rootSignature_.GetRootSignature());
    commandList->SetPipelineState(passPipelineState.GetPSO());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootDescriptorTable(0, inputSrvTable);
    commandList->SetGraphicsRootConstantBufferView(1, constantsAddress);
    commandList->DrawInstanced(3, 1, 0, 0);
}

void PostProcessing::Process(ID3D12GraphicsCommandList* commandList,
                             ID3D12Resource* inputFrame,
                             ID3D12Resource* outputFrame,
                             D3D12_CPU_DESCRIPTOR_HANDLE outputRTV,
                             ID3D12Resource* globalIlluminationFrame) {
    if (!initialized_ || !commandList || !inputFrame || !outputFrame ||
        outputRTV.ptr == 0 || inputSrvGPU_.ptr == 0) {
        NEXT_LOG_ERROR("Cannot process post-processing: invalid state or resources");
        return;
    }

    if (!rootSignature_.GetRootSignature() || !pipelineState_.GetPSO() ||
        !intermediatePipelineState_.GetPSO() || !postSrvHeap_ ||
        !gradedBuffer_ || !bloomBuffer_ ||
        sceneConstantsBuffer_.GetGPUVirtualAddress() == 0 ||
        bloomConstantsBuffer_.GetGPUVirtualAddress() == 0 ||
        finalConstantsBuffer_.GetGPUVirtualAddress() == 0) {
        NEXT_LOG_ERROR("Cannot process post-processing: pipeline resources are invalid");
        return;
    }

    if (inputFrame == outputFrame) {
        NEXT_LOG_ERROR("Post-processing input and output must be different resources");
        return;
    }

    ID3D12DescriptorHeap* descriptorHeaps[] = { postSrvHeap_.Get() };
    commandList->SetDescriptorHeaps(1, descriptorHeaps);

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    TransitionIntermediateResource(commandList,
                                   gradedBuffer_.Get(),
                                   gradedState_,
                                   D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ClearRenderTargetView(gradedRTV_, clearColor, 0, nullptr);
    if (!UpdateInputShaderResources(inputFrame, globalIlluminationFrame, sceneAndGiSrvCPU_) ||
        !UpdateConstants(sceneConstantsBuffer_, globalIlluminationFrame != nullptr, 0.0f)) {
        NEXT_LOG_ERROR("Failed to update post-processing scene inputs");
        return;
    }
    RenderFullscreenPass(commandList,
                         intermediatePipelineState_,
                         gradedRTV_,
                         sceneAndGiSrvGPU_,
                         sceneConstantsBuffer_.GetGPUVirtualAddress(),
                         width_,
                         height_);

    TransitionIntermediateResource(commandList,
                                   gradedBuffer_.Get(),
                                   gradedState_,
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionIntermediateResource(commandList,
                                   bloomBuffer_.Get(),
                                   bloomState_,
                                   D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ClearRenderTargetView(bloomRTV_, clearColor, 0, nullptr);
    if (!UpdateInputShaderResources(gradedBuffer_.Get(), nullptr, gradedAndNullSrvCPU_) ||
        !UpdateConstants(bloomConstantsBuffer_, false, 1.0f)) {
        NEXT_LOG_ERROR("Failed to update post-processing bloom inputs");
        return;
    }
    RenderFullscreenPass(commandList,
                         intermediatePipelineState_,
                         bloomRTV_,
                         gradedAndNullSrvGPU_,
                         bloomConstantsBuffer_.GetGPUVirtualAddress(),
                         bloomWidth_,
                         bloomHeight_);

    TransitionIntermediateResource(commandList,
                                   bloomBuffer_.Get(),
                                   bloomState_,
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (!UpdateInputShaderResources(gradedBuffer_.Get(), bloomBuffer_.Get(), gradedAndBloomSrvCPU_) ||
        !UpdateConstants(finalConstantsBuffer_, false, 2.0f)) {
        NEXT_LOG_ERROR("Failed to update post-processing final inputs");
        return;
    }
    RenderFullscreenPass(commandList,
                         pipelineState_,
                         outputRTV,
                         gradedAndBloomSrvGPU_,
                         finalConstantsBuffer_.GetGPUVirtualAddress(),
                         width_,
                         height_);
}

bool PostProcessing::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot resize uninitialized post-processing");
        return false;
    }

    if (width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid post-processing resize dimensions: %ux%u", width, height);
        return false;
    }

    NEXT_LOG_INFO("Resizing post-processing: %ux%u -> %ux%u", width_, height_, width, height);

    bloomBuffer_.Reset();
    gradedBuffer_.Reset();
    intermediateRtvHeap_.Reset();
    postSrvHeap_.Reset();
    bloomRTV_.ptr = 0;
    gradedRTV_.ptr = 0;
    sceneAndGiSrvCPU_.ptr = 0;
    sceneAndGiSrvGPU_.ptr = 0;
    gradedAndNullSrvCPU_.ptr = 0;
    gradedAndNullSrvGPU_.ptr = 0;
    gradedAndBloomSrvCPU_.ptr = 0;
    gradedAndBloomSrvGPU_.ptr = 0;
    intermediateRtvDescriptorSize_ = 0;
    postSrvDescriptorSize_ = 0;
    bloomWidth_ = 0;
    bloomHeight_ = 0;
    width_ = width;
    height_ = height;

    if (!CreateIntermediateResources()) {
        NEXT_LOG_ERROR("Failed to recreate post-processing resources during resize");
        Shutdown();
        return false;
    }

    return true;
}

void PostProcessing::Shutdown() {
    finalConstantsBuffer_.Shutdown();
    bloomConstantsBuffer_.Shutdown();
    sceneConstantsBuffer_.Shutdown();
    pipelineState_.Shutdown();
    intermediatePipelineState_.Shutdown();
    pixelShader_.Shutdown();
    vertexShader_.Shutdown();
    rootSignature_.Shutdown();
    bloomBuffer_.Reset();
    gradedBuffer_.Reset();
    intermediateRtvHeap_.Reset();
    postSrvHeap_.Reset();

    device_ = nullptr;
    srvHeap_ = nullptr;
    inputSrvCPU_.ptr = 0;
    inputSrvGPU_.ptr = 0;
    globalIlluminationSrvCPU_.ptr = 0;
    globalIlluminationSrvGPU_.ptr = 0;
    bloomRTV_.ptr = 0;
    gradedRTV_.ptr = 0;
    sceneAndGiSrvCPU_.ptr = 0;
    sceneAndGiSrvGPU_.ptr = 0;
    gradedAndNullSrvCPU_.ptr = 0;
    gradedAndNullSrvGPU_.ptr = 0;
    gradedAndBloomSrvCPU_.ptr = 0;
    gradedAndBloomSrvGPU_.ptr = 0;
    intermediateRtvDescriptorSize_ = 0;
    postSrvDescriptorSize_ = 0;
    bloomState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    gradedState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    width_ = 0;
    height_ = 0;
    bloomWidth_ = 0;
    bloomHeight_ = 0;
    initialized_ = false;

    NEXT_LOG_INFO("Post-processing shutdown complete");
}

} // namespace Next
