#include "next/renderer/dx12/taa.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <cmath>
#include <d3dcompiler.h>
#include <filesystem>
#include <system_error>
#include <vector>
#include <windows.h>

namespace Next {

namespace {

struct TAAConstantsGPU {
    float blendFactor;
    float sharpening;
    float antiGhosting;
    float velocityScale;
    float rectificationBias;
    float historyValid;
    float velocityAvailable;
    float texelSizeX;
    float texelSizeY;
    float padding0;
    float padding1;
    float padding2;
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

D3D12_CPU_DESCRIPTOR_HANDLE OffsetCPU(D3D12_CPU_DESCRIPTOR_HANDLE base, UINT descriptorSize, UINT index) {
    base.ptr += static_cast<SIZE_T>(descriptorSize) * index;
    return base;
}

D3D12_GPU_DESCRIPTOR_HANDLE OffsetGPU(D3D12_GPU_DESCRIPTOR_HANDLE base, UINT descriptorSize, UINT index) {
    base.ptr += static_cast<UINT64>(descriptorSize) * index;
    return base;
}

} // namespace

TemporalAA::TemporalAA()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , width_(0)
    , height_(0)
    , outputFormat_(DXGI_FORMAT_R8G8B8A8_UNORM)
    , historyIndex_(0)
    , historyValid_(false)
    , initialized_(false) {
    srvCPU_.ptr = 0;
    srvGPU_.ptr = 0;
    velocitySRV_.ptr = 0;
    velocityUAV_.ptr = 0;
    for (int i = 0; i < 2; ++i) {
        historySRV_[i].ptr = 0;
        historyUAV_[i].ptr = 0;
    }
}

TemporalAA::~TemporalAA() {
    Shutdown();
}

bool TemporalAA::Initialize(DX12Device* device,
                            DX12DescriptorHeap* srvHeap,
                            D3D12_CPU_DESCRIPTOR_HANDLE srvCPU,
                            D3D12_GPU_DESCRIPTOR_HANDLE srvGPU,
                            uint32_t width,
                            uint32_t height,
                            DXGI_FORMAT outputFormat) {
    if (!device || !device->GetDevice() || !srvHeap || !srvHeap->GetHeap() || !srvHeap->IsShaderVisible() ||
        srvCPU.ptr == 0 || srvGPU.ptr == 0 || width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid device or SRV heap for TAA");
        return false;
    }

    Shutdown();

    NEXT_LOG_INFO("Initializing TAA: %ux%u", width, height);

    device_ = device;
    srvHeap_ = srvHeap;
    srvCPU_ = srvCPU;
    srvGPU_ = srvGPU;
    width_ = width;
    height_ = height;
    outputFormat_ = outputFormat;
    historyIndex_ = 0;
    historyValid_ = false;

    // Create history resources
    if (!CreateHistoryResources()) {
        NEXT_LOG_ERROR("Failed to create history resources");
        Shutdown();
        return false;
    }

    // Create velocity buffer
    if (!CreateVelocityBuffer()) {
        NEXT_LOG_ERROR("Failed to create velocity buffer");
        Shutdown();
        return false;
    }

    if (!CreatePipelineResources(outputFormat_)) {
        NEXT_LOG_ERROR("Failed to create TAA pipeline resources");
        Shutdown();
        return false;
    }

    // Initialize default parameters (UE5-style)
    params_ = TAAParameters();

    initialized_ = true;
    NEXT_LOG_INFO("TAA initialized successfully (Phase 5: UE5-style TAA)");
    return true;
}

bool TemporalAA::CreateHistoryResources() {
    // Create two history buffers (double-buffered)
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = outputFormat_;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    // Create two history buffers
    for (int i = 0; i < 2; i++) {
        HRESULT hr = device_->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr,
            IID_PPV_ARGS(&historyBuffer_[i])
        );

        if (FAILED(hr)) {
            NEXT_LOG_ERROR("Failed to create history buffer %d: 0x%X", i, hr);
            return false;
        }

        historySRV_[i] = OffsetGPU(srvGPU_, srvHeap_->GetDescriptorSize(), 1);
        historyUAV_[i] = {};
    }

    NEXT_LOG_INFO("History buffers created: 2x %ux%u", width_, height_);
    return true;
}

bool TemporalAA::CreatePipelineResources(DXGI_FORMAT outputFormat) {
    const std::string vsPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/post_process.vs.hlsl");
    const std::string psPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/taa_resolve.ps.hlsl");

    if (!vertexShader_.LoadFromFile(device_, vsPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile TAA vertex shader: %s", vsPath.c_str());
        return false;
    }

    if (!pixelShader_.LoadFromFile(device_, psPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile TAA pixel shader: %s", psPath.c_str());
        return false;
    }

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
            NEXT_LOG_ERROR("TAA root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("TAA root signature serialization failed: 0x%X", hr);
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
        NEXT_LOG_ERROR("Failed to create TAA root signature: 0x%X", hr);
        return false;
    }
    rootSignature_.SetRootSignature(rootSignature.Get());

    std::vector<InputElementDesc> inputLayout;
    if (!pipelineState_.Initialize(
            device_,
            &rootSignature_,
            &vertexShader_,
            &pixelShader_,
            inputLayout,
            outputFormat,
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)) {
        NEXT_LOG_ERROR("Failed to create TAA pipeline state");
        return false;
    }

    if (!constantsBuffer_.Initialize(device_, sizeof(TAAConstantsGPU))) {
        NEXT_LOG_ERROR("Failed to create TAA constants buffer");
        return false;
    }

    return true;
}

bool TemporalAA::CreateVelocityBuffer() {
    // Velocity buffer stores motion vectors (2D)
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16_FLOAT;  // Motion vectors (2D)
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&velocityBuffer_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create velocity buffer: 0x%X", hr);
        return false;
    }

    NEXT_LOG_INFO("Velocity buffer created: %ux%u", width_, height_);
    return true;
}

void TemporalAA::UpdateHistory(ID3D12GraphicsCommandList* commandList,
                               ID3D12Resource* currentFrame,
                               ID3D12Resource* motionVectors) {
    if (!initialized_ || !commandList) {
        NEXT_LOG_ERROR("Cannot update history: not initialized or invalid command list");
        return;
    }

    const uint32_t writeIndex = (historyIndex_ + 1) % 2;
    if (!currentFrame || !historyBuffer_[writeIndex]) {
        NEXT_LOG_ERROR("Cannot update history: current frame is null");
        return;
    }

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = currentFrame;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = historyBuffer_[writeIndex].Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(2, barriers);

    commandList->CopyResource(historyBuffer_[writeIndex].Get(), currentFrame);

    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(2, barriers);

    // Store motion vectors if provided
    if (motionVectors && velocityBuffer_) {
        D3D12_RESOURCE_BARRIER velBarrier = {};
        velBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        velBarrier.Transition.pResource = velocityBuffer_.Get();
        velBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        velBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        velBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &velBarrier);
        commandList->CopyResource(velocityBuffer_.Get(), motionVectors);
        velBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        velBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        commandList->ResourceBarrier(1, &velBarrier);
    }

    historyIndex_ = writeIndex;
    historyValid_ = true;

    NEXT_LOG_DEBUG("History %u updated", historyIndex_);
}

bool TemporalAA::UpdateShaderResources(ID3D12Resource* currentFrame,
                                       ID3D12Resource* motionVectors) {
    if (!device_ || !device_->GetDevice() || !srvHeap_ || !currentFrame ||
        !historyBuffer_[historyIndex_] || srvCPU_.ptr == 0 || srvGPU_.ptr == 0) {
        return false;
    }

    const UINT descriptorSize = srvHeap_->GetDescriptorSize();
    if (descriptorSize == 0) {
        return false;
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE currentSrv = OffsetCPU(srvCPU_, descriptorSize, 0);
    const D3D12_CPU_DESCRIPTOR_HANDLE historySrv = OffsetCPU(srvCPU_, descriptorSize, 1);
    const D3D12_CPU_DESCRIPTOR_HANDLE velocitySrv = OffsetCPU(srvCPU_, descriptorSize, 2);
    historySRV_[historyIndex_] = OffsetGPU(srvGPU_, descriptorSize, 1);
    velocitySRV_ = OffsetGPU(srvGPU_, descriptorSize, 2);

    D3D12_SHADER_RESOURCE_VIEW_DESC currentDesc = {};
    const D3D12_RESOURCE_DESC currentResourceDesc = currentFrame->GetDesc();
    currentDesc.Format = currentResourceDesc.Format;
    currentDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    currentDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    currentDesc.Texture2D.MostDetailedMip = 0;
    currentDesc.Texture2D.MipLevels = currentResourceDesc.MipLevels;
    currentDesc.Texture2D.PlaneSlice = 0;
    currentDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->GetDevice()->CreateShaderResourceView(currentFrame, &currentDesc, currentSrv);

    D3D12_SHADER_RESOURCE_VIEW_DESC historyDesc = {};
    const D3D12_RESOURCE_DESC historyResourceDesc = historyBuffer_[historyIndex_]->GetDesc();
    historyDesc.Format = historyResourceDesc.Format;
    historyDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    historyDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    historyDesc.Texture2D.MostDetailedMip = 0;
    historyDesc.Texture2D.MipLevels = historyResourceDesc.MipLevels;
    historyDesc.Texture2D.PlaneSlice = 0;
    historyDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->GetDevice()->CreateShaderResourceView(historyBuffer_[historyIndex_].Get(), &historyDesc, historySrv);

    D3D12_SHADER_RESOURCE_VIEW_DESC velocityDesc = {};
    velocityDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    velocityDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    velocityDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    velocityDesc.Texture2D.MostDetailedMip = 0;
    velocityDesc.Texture2D.MipLevels = 1;
    velocityDesc.Texture2D.PlaneSlice = 0;
    velocityDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    if (motionVectors) {
        const D3D12_RESOURCE_DESC motionDesc = motionVectors->GetDesc();
        velocityDesc.Format = motionDesc.Format;
        velocityDesc.Texture2D.MipLevels = motionDesc.MipLevels;
        device_->GetDevice()->CreateShaderResourceView(motionVectors, &velocityDesc, velocitySrv);
    } else {
        device_->GetDevice()->CreateShaderResourceView(nullptr, &velocityDesc, velocitySrv);
    }

    return true;
}

bool TemporalAA::UpdateConstants(bool historyValid, bool velocityAvailable) {
    TAAConstantsGPU constants = {};
    constants.blendFactor = std::clamp(params_.blendFactor, 0.0f, 1.0f);
    constants.sharpening = std::max(0.0f, params_.sharpening);
    constants.antiGhosting = std::max(0.0f, params_.antiGhosting);
    constants.velocityScale = params_.velocityScale;
    constants.rectificationBias = std::max(0.0f, params_.rectificationBias);
    constants.historyValid = historyValid ? 1.0f : 0.0f;
    constants.velocityAvailable = velocityAvailable ? 1.0f : 0.0f;
    constants.texelSizeX = width_ > 0 ? 1.0f / static_cast<float>(width_) : 0.0f;
    constants.texelSizeY = height_ > 0 ? 1.0f / static_cast<float>(height_) : 0.0f;

    return constantsBuffer_.UpdateData(&constants, sizeof(constants));
}

void TemporalAA::CopyResolvedFrameToHistory(ID3D12GraphicsCommandList* commandList,
                                            ID3D12Resource* outputFrame) {
    if (!commandList || !outputFrame) {
        return;
    }

    const uint32_t writeIndex = (historyIndex_ + 1) % 2;
    if (!historyBuffer_[writeIndex]) {
        return;
    }

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = outputFrame;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = historyBuffer_[writeIndex].Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(2, barriers);

    commandList->CopyResource(historyBuffer_[writeIndex].Get(), outputFrame);

    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(2, barriers);

    historyIndex_ = writeIndex;
    historyValid_ = true;
}

void TemporalAA::Resolve(ID3D12GraphicsCommandList* commandList,
                         ID3D12Resource* currentFrame,
                         ID3D12Resource* outputFrame,
                         D3D12_CPU_DESCRIPTOR_HANDLE outputRTV,
                         ID3D12Resource* motionVectors) {
    if (!initialized_ || !commandList) {
        NEXT_LOG_ERROR("Cannot resolve TAA: not initialized or invalid command list");
        return;
    }

    if (!currentFrame || !outputFrame || outputRTV.ptr == 0 || !historyBuffer_[historyIndex_]) {
        NEXT_LOG_ERROR("Cannot resolve TAA: current or output frame is null");
        return;
    }

    if (currentFrame == outputFrame) {
        NEXT_LOG_ERROR("Cannot resolve TAA in-place");
        return;
    }

    if (!rootSignature_.GetRootSignature() || !pipelineState_.GetPSO() ||
        constantsBuffer_.GetGPUVirtualAddress() == 0 || srvGPU_.ptr == 0) {
        NEXT_LOG_ERROR("Cannot resolve TAA: pipeline resources are invalid");
        return;
    }

    if (!UpdateShaderResources(currentFrame, motionVectors) ||
        !UpdateConstants(historyValid_, motionVectors != nullptr)) {
        NEXT_LOG_ERROR("Failed to update TAA inputs");
        return;
    }

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

    commandList->OMSetRenderTargets(1, &outputRTV, TRUE, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    commandList->SetGraphicsRootSignature(rootSignature_.GetRootSignature());
    commandList->SetPipelineState(pipelineState_.GetPSO());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootDescriptorTable(0, srvGPU_);
    commandList->SetGraphicsRootConstantBufferView(1, constantsBuffer_.GetGPUVirtualAddress());
    commandList->DrawInstanced(3, 1, 0, 0);

    CopyResolvedFrameToHistory(commandList, outputFrame);

    NEXT_LOG_DEBUG("TAA resolved with temporal history (blend=%.2f)", params_.blendFactor);
}

Mat4 TemporalAA::GetJitteredProjectionMatrix(const Mat4& projection, float jitterX, float jitterY) {
    // Apply sub-pixel jitter to projection matrix (UE5-style)
    // This distributes samples across pixels for better temporal integration

    Mat4 jittered = projection;

    if (width_ == 0 || height_ == 0) {
        return jittered;
    }

    const float jitterX_NDC = jitterX / static_cast<float>(width_) * 2.0f;
    const float jitterY_NDC = jitterY / static_cast<float>(height_) * 2.0f;
    jittered(0, 2) += jitterX_NDC;
    jittered(1, 2) += jitterY_NDC;

    return jittered;
}

void TemporalAA::GetHaltonSequence(uint32_t frameIndex, float& outX, float& outY) {
    // Generate Halton sequence (2,3) for jitter (UE5-style)
    // This provides better distribution than random jitter

    // Halton(2) for X
    float x = 0.0f;
    float f = 0.5f;
    uint32_t index = frameIndex;
    while (index > 0) {
        if (index & 1) x += f;
        f *= 0.5f;
        index >>= 1;
    }
    outX = x;

    // Halton(3) for Y
    float y = 0.0f;
    f = 0.333333f;
    index = frameIndex;
    while (index > 0) {
        if (index % 3 == 1) y += f;
        else if (index % 3 == 2) y += 2.0f * f;
        f *= 0.333333f;
        index /= 3;
    }
    outY = y;

    // Convert to [-0.5, 0.5] range
    outX -= 0.5f;
    outY -= 0.5f;
}

bool TemporalAA::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot resize uninitialized TAA");
        return false;
    }

    NEXT_LOG_INFO("Resizing TAA: %ux%u -> %ux%u", width_, height_, width, height);

    if (width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid TAA resize dimensions: %ux%u", width, height);
        return false;
    }

    historyBuffer_[0].Reset();
    historyBuffer_[1].Reset();
    velocityBuffer_.Reset();
    width_ = width;
    height_ = height;
    historyIndex_ = 0;
    historyValid_ = false;

    if (!CreateHistoryResources()) {
        NEXT_LOG_ERROR("Failed to recreate TAA history resources");
        Shutdown();
        return false;
    }

    if (!CreateVelocityBuffer()) {
        NEXT_LOG_ERROR("Failed to recreate TAA velocity buffer");
        Shutdown();
        return false;
    }

    return true;
}

void TemporalAA::Shutdown() {
    constantsBuffer_.Shutdown();
    pipelineState_.Shutdown();
    pixelShader_.Shutdown();
    vertexShader_.Shutdown();
    rootSignature_.Shutdown();
    historyBuffer_[0].Reset();
    historyBuffer_[1].Reset();
    velocityBuffer_.Reset();

    device_ = nullptr;
    srvHeap_ = nullptr;
    srvCPU_.ptr = 0;
    srvGPU_.ptr = 0;
    width_ = 0;
    height_ = 0;
    outputFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM;
    historyIndex_ = 0;
    historyValid_ = false;
    velocitySRV_.ptr = 0;
    velocityUAV_.ptr = 0;
    for (int i = 0; i < 2; ++i) {
        historySRV_[i].ptr = 0;
        historyUAV_[i].ptr = 0;
    }
    initialized_ = false;

    NEXT_LOG_INFO("TAA shutdown complete");
}

} // namespace Next
