#include "next/renderer/dx12/global_illumination.h"
#include "next/foundation/logger.h"
#include <algorithm>

namespace Next {

//=============================================================================
// Global Illumination Implementation
//=============================================================================

GlobalIllumination::GlobalIllumination()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , width_(0)
    , height_(0)
    , frameCount_(0)
    , initialized_(false) {
}

GlobalIllumination::~GlobalIllumination() {
    Shutdown();
}

bool GlobalIllumination::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                                   uint32_t width, uint32_t height) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for GI system");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    width_ = width;
    height_ = height;
    frameCount_ = 0;

    // Initialize subsystems
    if (!aoManager_.Initialize(device, srvHeap, width, height)) {
        NEXT_LOG_ERROR("Failed to initialize AO manager");
        return false;
    }

    if (!probeManager_.Initialize(device, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize probe manager");
        return false;
    }

    if (!screenSpaceGI_.Initialize(device, srvHeap, width, height)) {
        NEXT_LOG_WARNING("Failed to initialize screen space GI - will not be available");
    }

    // Create resources
    if (!CreateResources()) {
        NEXT_LOG_ERROR("Failed to create GI resources");
        return false;
    }

    // Create shaders
    if (!CreateShaders()) {
        NEXT_LOG_ERROR("Failed to create GI shaders");
        return false;
    }

    // Create root signature
    if (!CreateRootSignature()) {
        NEXT_LOG_ERROR("Failed to create GI root signature");
        return false;
    }

    // Create pipeline states
    if (!CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create GI pipeline states");
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("Global Illumination system initialized: %ux%u", width, height);
    return true;
}

bool GlobalIllumination::CreateResources() {
    // Create GI textures (all R11G11B10_FLOAT for HDR)
    D3D12_RESOURCE_DESC giDesc = {};
    giDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    giDesc.Alignment = 0;
    giDesc.Width = width_;
    giDesc.Height = height_;
    giDesc.DepthOrArraySize = 1;
    giDesc.MipLevels = 1;
    giDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    giDesc.SampleDesc.Count = 1;
    giDesc.SampleDesc.Quality = 0;
    giDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    giDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    // AO output
    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &giDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
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
        nullptr,
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
        nullptr,
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
        nullptr,
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
        nullptr,
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
        nullptr,
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
    bufferDesc.Width = sizeof(GISettings);
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

    return true;
}

bool GlobalIllumination::CreateShaders() {
    // Placeholder - shader files need to be created
    NEXT_LOG_WARNING("GI shaders not yet implemented - using placeholder");
    return true;
}

bool GlobalIllumination::CreateRootSignature() {
    // Placeholder
    NEXT_LOG_WARNING("GI root signature not yet implemented - using placeholder");
    return true;
}

bool GlobalIllumination::CreatePipelineStates() {
    // Placeholder
    NEXT_LOG_WARNING("GI pipeline states not yet implemented - using placeholder");
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
    RenderAmbientOcclusion(commandList);
    RenderLightProbes(commandList);
    RenderScreenSpaceGI(commandList);

    frameCount_++;
}

void GlobalIllumination::Render(ID3D12GraphicsCommandList* commandList,
                               ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    // Combine all GI techniques
    CombineGI(commandList);
}

void GlobalIllumination::RenderAmbientOcclusion(ID3D12GraphicsCommandList* commandList) {
    // Render AO based on current type
    aoManager_.Render(commandList, nullptr, nullptr, aoOutput_.Get());
}

void GlobalIllumination::RenderLightProbes(ID3D12GraphicsCommandList* commandList) {
    // Render light probe contribution
    NEXT_LOG_WARNING("Light probe render not yet implemented");
}

void GlobalIllumination::RenderScreenSpaceGI(ID3D12GraphicsCommandList* commandList) {
    // Render screen-space GI
    if (screenSpaceGI_.IsInitialized()) {
        screenSpaceGI_.Render(commandList, ssGIOutput_.Get());
    }
}

void GlobalIllumination::CombineGI(ID3D12GraphicsCommandList* commandList) {
    // Update settings buffer
    void* pData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    HRESULT hr = settingsBuffer_->Map(0, &readRange, &pData);
    if (SUCCEEDED(hr)) {
        memcpy(pData, &settings_, sizeof(settings_));
        settingsBuffer_->Unmap(0, nullptr);
    }

    // Combine AO + Probes + SS GI based on weights
    // This is where the actual GI computation happens
    NEXT_LOG_WARNING("GI combination pass not yet implemented");
}

bool GlobalIllumination::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) {
        return false;
    }

    width_ = width;
    height_ = height;

    // Resize subsystems
    aoManager_.Resize(width, height);
    screenSpaceGI_.Resize(width, height);

    // Recreate resources (would need proper implementation)
    NEXT_LOG_WARNING("GI resize not yet fully implemented");

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
    combineRootSignature_.Reset();
    combinePSO_.Reset();

    // Shutdown subsystems
    aoManager_.Shutdown();
    probeManager_.Shutdown();
    screenSpaceGI_.Shutdown();

    initialized_ = false;
    NEXT_LOG_INFO("Global Illumination system shutdown complete");
}

//=============================================================================
// VXGI Implementation
//=============================================================================

VXGI::VXGI()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , worldMin_(0.0f, 0.0f, 0.0f)
    , worldMax_(10.0f, 10.0f, 10.0f)
    , voxelResolution_(128)
    , initialized_(false) {
}

VXGI::~VXGI() {
    Shutdown();
}

bool VXGI::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                     const Vec3& worldMin, const Vec3& worldMax,
                     int voxelResolution) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for VXGI");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    worldMin_ = worldMin;
    worldMax_ = worldMax;
    voxelResolution_ = voxelResolution;

    // Create voxel resources
    if (!CreateVoxelResources()) {
        NEXT_LOG_ERROR("Failed to create VXGI voxel resources");
        return false;
    }

    // Create shaders
    if (!CreateShaders()) {
        NEXT_LOG_ERROR("Failed to create VXGI shaders");
        return false;
    }

    // Create pipeline states
    if (!CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create VXGI pipeline states");
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

    // TODO: Create mipmapped versions for cone tracing

    return true;
}

bool VXGI::CreateShaders() {
    NEXT_LOG_WARNING("VXGI shaders not yet implemented - using placeholder");
    return true;
}

bool VXGI::CreatePipelineStates() {
    NEXT_LOG_WARNING("VXGI pipeline states not yet implemented - using placeholder");
    return true;
}

void VXGI::Voxelize(ID3D12GraphicsCommandList* commandList,
                   ID3D12Resource* depthBuffer,
                   ID3D12Resource* normalBuffer,
                   ID3D12Resource* colorBuffer) {
    if (!initialized_) {
        return;
    }

    // Voxelization pass - rasterize scene into 3D voxel textures
    NEXT_LOG_WARNING("VXGI voxelization pass not yet implemented");
}

void VXGI::ConeTrace(ID3D12GraphicsCommandList* commandList,
                    ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    // Cone tracing pass - trace cones through voxel volume
    NEXT_LOG_WARNING("VXGI cone tracing pass not yet implemented");
}

void VXGI::Shutdown() {
    voxelAlbedo_.Reset();
    voxelNormal_.Reset();
    voxelEmission_.Reset();
    voxelAlbedoMip_.Reset();
    voxelNormalMip_.Reset();
    voxelizationRootSignature_.Reset();
    voxelizationPSO_.Reset();
    coneTraceRootSignature_.Reset();
    coneTracePSO_.Reset();
    initialized_ = false;
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

    device_ = device;
    srvHeap_ = srvHeap;
    settings_ = settings;

    // Create probe volume
    if (!CreateProbeVolume()) {
        NEXT_LOG_ERROR("Failed to create DDGI probe volume");
        return false;
    }

    // TODO: Create shaders and pipeline

    initialized_ = true;
    NEXT_LOG_INFO("DDGI initialized: %dx%dx%d probes, %d rays per probe",
                  settings_.probeCountX, settings_.probeCountY, settings_.probeCountZ,
                  settings_.raysPerProbe);
    return true;
}

bool DDGI::CreateProbeVolume() {
    NEXT_LOG_WARNING("DDGI probe volume creation not yet implemented");
    return true;
}

void DDGI::UpdateProbes(ID3D12GraphicsCommandList* commandList,
                       ID3D12Resource* depthBuffer,
                       ID3D12Resource* normalBuffer) {
    if (!initialized_) {
        return;
    }

    NEXT_LOG_WARNING("DDGI probe update not yet implemented");
}

void DDGI::Render(ID3D12GraphicsCommandList* commandList,
                 ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    NEXT_LOG_WARNING("DDGI render not yet implemented");
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
    initialized_ = false;
}

//=============================================================================
// Ray Traced GI Implementation
//=============================================================================

RayTracedGI::RayTracedGI()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , dxrAvailable_(false)
    , initialized_(false) {
}

RayTracedGI::~RayTracedGI() {
    Shutdown();
}

bool RayTracedGI::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                            const RayTracedGISettings& settings) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for RTGI");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    settings_ = settings;

    // Check DXR support
    if (!CheckDXRSupport()) {
        NEXT_LOG_WARNING("DXR not available on this device - RTGI disabled");
        return false;
    }

    dxrAvailable_ = true;

    // TODO: Create DXR pipeline and resources

    initialized_ = true;
    NEXT_LOG_INFO("Ray Traced GI initialized (DXR available)");
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

void RayTracedGI::Render(ID3D12GraphicsCommandList* commandList,
                        ID3D12Resource* depthBuffer,
                        ID3D12Resource* normalBuffer,
                        ID3D12Resource* output) {
    if (!initialized_ || !dxrAvailable_) {
        return;
    }

    NEXT_LOG_WARNING("RTGI render not yet implemented");
}

void RayTracedGI::Shutdown() {
    raytracingOutput_.Reset();
    denoisedOutput_.Reset();
    temporalHistory_.Reset();
    rtpso_.Reset();
    globalRootSignature_.Reset();
    dxrAvailable_ = false;
    initialized_ = false;
}

//=============================================================================
// GI Manager Implementation
//=============================================================================

GIManager::GIManager()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , currentTechnique_(GITechnique::Hybrid)
    , width_(0)
    , height_(0)
    , initialized_(false) {
}

GIManager::~GIManager() {
    Shutdown();
}

bool GIManager::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                          uint32_t width, uint32_t height) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for GI manager");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    width_ = width;
    height_ = height;

    // Initialize main GI system
    if (!gi_.Initialize(device, srvHeap, width, height)) {
        NEXT_LOG_ERROR("Failed to initialize GI system");
        return false;
    }

    // Initialize VXGI
    Vec3 worldMin(0.0f, 0.0f, 0.0f);
    Vec3 worldMax(20.0f, 10.0f, 20.0f);
    if (!vxgi_.Initialize(device, srvHeap, worldMin, worldMax, 128)) {
        NEXT_LOG_WARNING("Failed to initialize VXGI - will not be available");
    }

    // Initialize DDGI
    DDGISettings ddgiSettings;
    if (!ddgi_.Initialize(device, srvHeap, ddgiSettings)) {
        NEXT_LOG_WARNING("Failed to initialize DDGI - will not be available");
    }

    // Initialize RTGI
    RayTracedGISettings rtiSettings;
    if (!rti_.Initialize(device, srvHeap, rtiSettings)) {
        NEXT_LOG_WARNING("Failed to initialize RTGI - will not be available");
    }

    initialized_ = true;
    NEXT_LOG_INFO("GI Manager initialized (technique: Hybrid)");
    return true;
}

void GIManager::SetGITechnique(GITechnique technique) {
    currentTechnique_ = technique;
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
            break;

        case GITechnique::VoxelGI:
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
            break;

        case GITechnique::VoxelGI:
            if (vxgi_.IsInitialized()) {
                vxgi_.ConeTrace(commandList, output);
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

    width_ = width;
    height_ = height;

    gi_.Resize(width, height);

    return true;
}

void GIManager::SetSettings(const GISettings& settings) {
    settings_ = settings;
    gi_.SetSettings(settings);
}

void GIManager::Shutdown() {
    gi_.Shutdown();
    vxgi_.Shutdown();
    ddgi_.Shutdown();
    rti_.Shutdown();
    initialized_ = false;
    NEXT_LOG_INFO("GI Manager shutdown complete");
}

} // namespace Next
