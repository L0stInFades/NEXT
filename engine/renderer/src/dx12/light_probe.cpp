#include "next/renderer/dx12/light_probe.h"
#include "next/foundation/logger.h"
#include <cmath>
#include <algorithm>

namespace Next {

//=============================================================================
// Spherical Harmonics Implementation
//=============================================================================

// SH constants for band 0, 1, 2
namespace SHConstants {
    // Band 0 (1 coefficient)
    const float C0 = 0.282095f;  // 1 / (2 * sqrt(PI))

    // Band 1 (3 coefficients)
    const float C1 = 0.488603f;  // sqrt(3 / (4 * PI))

    // Band 2 (5 coefficients)
    const float C2_0 = 1.092548f;  // 0.5 * sqrt(15 / PI)
    const float C2_1 = 0.315392f;  // 0.25 * sqrt(5 / PI)
    const float C2_2 = 0.746655f;  // 0.5 * sqrt(15 / PI)
}

Vec3 SphericalHarmonics::Evaluate(const Vec3& direction) const {
    // Normalize direction
    Vec3 n = direction.Normalize();

    float x = n.x;
    float y = n.y;
    float z = n.z;

    // Band 0 (L0)
    Vec3 result = L[0] * SHConstants::C0;

    // Band 1 (L1)
    result = result + L[1] * (SHConstants::C1 * y);
    result = result + L[2] * (SHConstants::C1 * z);
    result = result + L[3] * (SHConstants::C1 * x);

    // Band 2 (L2)
    result = result + L[4] * (SHConstants::C2_0 * x * y);
    result = result + L[5] * (SHConstants::C2_1 * (3.0f * z * z - 1.0f));
    result = result + L[6] * (SHConstants::C2_0 * y * z);
    result = result + L[7] * (SHConstants::C2_2 * (x * x - y * y));
    result = result + L[8] * (SHConstants::C2_2 * x * z);

    return result;
}

void SphericalHarmonics::Add(const SphericalHarmonics& other) {
    for (int i = 0; i < 9; ++i) {
        L[i] = L[i] + other.L[i];
    }
}

void SphericalHarmonics::Scale(float scale) {
    for (int i = 0; i < 9; ++i) {
        L[i] = L[i] * scale;
    }
}

void SphericalHarmonics::ToIrradiance() {
    // Convert radiance to irradiance by dividing by PI
    const float invPI = 1.0f / 3.14159265359f;
    Scale(invPI);
}

void SphericalHarmonics::ProjectLight(const Vec3& direction, const Vec3& color, float intensity) {
    Vec3 n = direction.Normalize();
    Vec3 radiance = color * intensity;

    float x = n.x;
    float y = n.y;
    float z = n.z;

    // Band 0
    L[0] = L[0] + radiance * SHConstants::C0;

    // Band 1
    L[1] = L[1] + radiance * (SHConstants::C1 * y);
    L[2] = L[2] + radiance * (SHConstants::C1 * z);
    L[3] = L[3] + radiance * (SHConstants::C1 * x);

    // Band 2
    L[4] = L[4] + radiance * (SHConstants::C2_0 * x * y);
    L[5] = L[5] + radiance * (SHConstants::C2_1 * (3.0f * z * z - 1.0f));
    L[6] = L[6] + radiance * (SHConstants::C2_0 * y * z);
    L[7] = L[7] + radiance * (SHConstants::C2_2 * (x * x - y * y));
    L[8] = L[8] + radiance * (SHConstants::C2_2 * x * z);
}

//=============================================================================
// Light Probe Volume Implementation
//=============================================================================

LightProbeVolume::LightProbeVolume()
    : origin_(0.0f, 0.0f, 0.0f)
    , size_(10.0f, 10.0f, 10.0f)
    , probesX_(0)
    , probesY_(0)
    , probesZ_(0) {
}

LightProbeVolume::~LightProbeVolume() {
}

bool LightProbeVolume::Initialize(const Vec3& origin, const Vec3& size,
                                 int probesX, int probesY, int probesZ) {
    if (probesX <= 0 || probesY <= 0 || probesZ <= 0) {
        NEXT_LOG_ERROR("Invalid probe grid dimensions");
        return false;
    }

    origin_ = origin;
    size_ = size;
    probesX_ = probesX;
    probesY_ = probesY;
    probesZ_ = probesZ;

    // Allocate probes
    int totalProbes = probesX * probesY * probesZ;
    probes_.resize(totalProbes);

    // Initialize probe positions
    float stepX = size.x / static_cast<float>(probesX - 1);
    float stepY = size.y / static_cast<float>(probesY - 1);
    float stepZ = size.z / static_cast<float>(probesZ - 1);
    Vec3 step(stepX, stepY, stepZ);

    for (int z = 0; z < probesZ; ++z) {
        for (int y = 0; y < probesY; ++y) {
            for (int x = 0; x < probesX; ++x) {
                int index = z * probesX * probesY + y * probesX + x;
                Vec3 offset(x * step.x, y * step.y, z * step.z);
                probes_[index].position = origin + offset;
                probes_[index].radius = step.Length() * 1.5f;  // Overlapping influence
            }
        }
    }

    NEXT_LOG_INFO("Light probe volume created: %dx%dx%d = %d probes",
                  probesX, probesY, probesZ, totalProbes);

    return true;
}

LightProbe* LightProbeVolume::GetProbe(int x, int y, int z) {
    if (x < 0 || x >= probesX_ || y < 0 || y >= probesY_ || z < 0 || z >= probesZ_) {
        return nullptr;
    }

    int index = z * probesX_ * probesY_ + y * probesX_ + x;
    return &probes_[index];
}

std::vector<LightProbe*> LightProbeVolume::FindNearestProbes(const Vec3& position) {
    std::vector<LightProbe*> nearest;

    // Convert world position to grid coordinates
    Vec3 localPos = position - origin_;
    Vec3 normalizedPos(
        localPos.x / size_.x,
        localPos.y / size_.y,
        localPos.z / size_.z
    );

    int x = static_cast<int>(normalizedPos.x * (probesX_ - 1));
    int y = static_cast<int>(normalizedPos.y * (probesY_ - 1));
    int z = static_cast<int>(normalizedPos.z * (probesZ_ - 1));

    // Clamp to valid range
    x = std::max(0, std::min(probesX_ - 1, x));
    y = std::max(0, std::min(probesY_ - 1, y));
    z = std::max(0, std::min(probesZ_ - 1, z));

    // Get probes in local neighborhood (2x2x2 cube for trilinear interpolation)
    for (int dz = 0; dz <= 1; ++dz) {
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dx = 0; dx <= 1; ++dx) {
                LightProbe* probe = GetProbe(x + dx, y + dy, z + dz);
                if (probe) {
                    nearest.push_back(probe);
                }
            }
        }
    }

    return nearest;
}

Vec3 LightProbeVolume::InterpolateIrradiance(const Vec3& position, const Vec3& normal) {
    std::vector<LightProbe*> nearest = FindNearestProbes(position);

    if (nearest.empty()) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }

    // Trilinear interpolation
    // Find interpolation weights based on distance
    Vec3 irradiance(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    for (LightProbe* probe : nearest) {
        float distance = (probe->position - position).Length();
        if (distance < probe->radius) {
            // Weight by distance and normal alignment
            float weight = 1.0f - (distance / probe->radius);
            float normalWeight = std::max(0.0f, normal.Dot(probe->normal));
            weight *= (0.5f + 0.5f * normalWeight);

            Vec3 probeIrradiance = probe->EvaluateIrradiance(normal);
            irradiance = irradiance + probeIrradiance * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0f) {
        irradiance = irradiance / totalWeight;
    }

    return irradiance;
}

void LightProbeVolume::UpdateProbe(int x, int y, int z, const SphericalHarmonics& sh) {
    LightProbe* probe = GetProbe(x, y, z);
    if (probe) {
        probe->sh = sh;
    }
}

//=============================================================================
// Light Probe Renderer Implementation
//=============================================================================

LightProbeRenderer::LightProbeRenderer()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , maxProbes_(4096)
    , currentProbeCount_(0)
    , initialized_(false) {
}

LightProbeRenderer::~LightProbeRenderer() {
    Shutdown();
}

bool LightProbeRenderer::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for probe renderer");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;

    // Create resources
    if (!CreateResources()) {
        NEXT_LOG_ERROR("Failed to create probe renderer resources");
        return false;
    }

    // Create shaders
    if (!CreateShaders()) {
        NEXT_LOG_ERROR("Failed to create probe renderer shaders");
        return false;
    }

    // Create root signature
    if (!CreateRootSignature()) {
        NEXT_LOG_ERROR("Failed to create probe renderer root signature");
        return false;
    }

    // Create pipeline states
    if (!CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create probe renderer pipeline states");
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("Light probe renderer initialized (max probes: %u)", maxProbes_);
    return true;
}

bool LightProbeRenderer::CreateResources() {
    // Create structured buffer for probes
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = maxProbes_ * sizeof(LightProbe);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&probeBuffer_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create probe buffer");
        return false;
    }

    // Create upload buffer
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&probeBufferUpload_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create probe upload buffer");
        return false;
    }

    return true;
}

bool LightProbeRenderer::CreateShaders() {
    // Placeholder - actual shader files need to be created
    NEXT_LOG_WARNING("Light probe shaders not yet implemented - using placeholder");
    return true;
}

bool LightProbeRenderer::CreateRootSignature() {
    // Placeholder
    NEXT_LOG_WARNING("Light probe root signature not yet implemented - using placeholder");
    return true;
}

bool LightProbeRenderer::CreatePipelineStates() {
    // Placeholder
    NEXT_LOG_WARNING("Light probe pipeline states not yet implemented - using placeholder");
    return true;
}

void LightProbeRenderer::BakeProbes(LightProbeVolume* volume,
                                   ID3D12GraphicsCommandList* commandList,
                                   ID3D12Resource* sceneDepth,
                                   ID3D12Resource* sceneNormal) {
    if (!initialized_ || !volume) {
        return;
    }

    // Bake each probe in the volume
    for (int z = 0; z < volume->GetProbeCountZ(); ++z) {
        for (int y = 0; y < volume->GetProbeCountY(); ++y) {
            for (int x = 0; x < volume->GetProbeCountX(); ++x) {
                LightProbe* probe = volume->GetProbe(x, y, z);
                if (probe) {
                    BakeProbeSH(probe, commandList);
                }
            }
        }
    }

    NEXT_LOG_INFO("Baked %d light probes", volume->GetTotalProbeCount());
}

void LightProbeRenderer::BakeProbeSH(LightProbe* probe, ID3D12GraphicsCommandList* commandList) {
    // Placeholder - capture cubemap and project to SH
    NEXT_LOG_WARNING("Probe baking not yet implemented - using placeholder");
}

void LightProbeRenderer::UpdateProbes(ID3D12GraphicsCommandList* commandList) {
    // Placeholder - temporal probe updates
    NEXT_LOG_WARNING("Probe updates not yet implemented - using placeholder");
}

void LightProbeRenderer::RenderProbeVisualization(ID3D12GraphicsCommandList* commandList,
                                                  const LightProbeVolume* volume) {
    if (!initialized_ || !volume) {
        return;
    }

    // Placeholder - render sphere at each probe position
    NEXT_LOG_WARNING("Probe visualization not yet implemented - using placeholder");
}

void LightProbeRenderer::UploadProbeData(ID3D12GraphicsCommandList* commandList,
                                        const LightProbeVolume* volume) {
    if (!initialized_ || !volume) {
        return;
    }

    // Map upload buffer
    void* pData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    HRESULT hr = probeBufferUpload_->Map(0, &readRange, &pData);
    if (SUCCEEDED(hr)) {
        // Copy probe data
        const std::vector<LightProbe>& probes = volume->GetProbes();
        memcpy(pData, probes.data(), probes.size() * sizeof(LightProbe));

        probeBufferUpload_->Unmap(0, nullptr);

        // Copy to GPU buffer
        commandList->CopyResource(probeBuffer_.Get(), probeBufferUpload_.Get());

        // TODO: Add proper resource barrier transition
        // D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(...);
    }

    currentProbeCount_ = volume->GetTotalProbeCount();
}

void LightProbeRenderer::Shutdown() {
    probeBuffer_.Reset();
    probeBufferUpload_.Reset();
    probeSphereMesh_.Reset();
    probeSphereIndices_.Reset();
    rootSignature_.Reset();
    visualizationPSO_.Reset();
    bakingPSO_.Reset();
    initialized_ = false;
}

//=============================================================================
// Light Probe Manager Implementation
//=============================================================================

LightProbeManager::LightProbeManager()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , initialized_(false) {
}

LightProbeManager::~LightProbeManager() {
    Shutdown();
}

bool LightProbeManager::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for probe manager");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;

    // Initialize probe renderer
    if (!renderer_.Initialize(device, srvHeap)) {
        NEXT_LOG_ERROR("Failed to initialize probe renderer");
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("Light probe manager initialized");
    return true;
}

LightProbeVolume* LightProbeManager::CreateVolume(const Vec3& origin, const Vec3& size,
                                                  int probesX, int probesY, int probesZ) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Probe manager not initialized");
        return nullptr;
    }

    LightProbeVolume* volume = new LightProbeVolume();
    if (!volume->Initialize(origin, size, probesX, probesY, probesZ)) {
        delete volume;
        return nullptr;
    }

    volumes_.push_back(volume);
    NEXT_LOG_INFO("Created probe volume %d: %dx%dx%d probes",
                  volumes_.size() - 1, probesX, probesY, probesZ);

    return volume;
}

void LightProbeManager::RemoveVolume(LightProbeVolume* volume) {
    auto it = std::find(volumes_.begin(), volumes_.end(), volume);
    if (it != volumes_.end()) {
        volumes_.erase(it);
        delete volume;
        NEXT_LOG_INFO("Removed probe volume");
    }
}

void LightProbeManager::UpdateProbes(ID3D12GraphicsCommandList* commandList,
                                    ID3D12Resource* sceneDepth,
                                    ID3D12Resource* sceneNormal) {
    if (!initialized_) {
        return;
    }

    // Update all volumes
    for (LightProbeVolume* volume : volumes_) {
        renderer_.BakeProbes(volume, commandList, sceneDepth, sceneNormal);
    }

    // Upload probe data to GPU
    for (LightProbeVolume* volume : volumes_) {
        renderer_.UploadProbeData(commandList, volume);
    }
}

Vec3 LightProbeManager::SampleIrradiance(const Vec3& position, const Vec3& normal) {
    if (!initialized_ || volumes_.empty()) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }

    Vec3 totalIrradiance(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    // Sample from all volumes and blend
    for (LightProbeVolume* volume : volumes_) {
        Vec3 irradiance = volume->InterpolateIrradiance(position, normal);
        float weight = 1.0f;  // Could be based on distance to volume center

        totalIrradiance = totalIrradiance + irradiance * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.0f) {
        return totalIrradiance / totalWeight;
    }

    return Vec3(0.0f, 0.0f, 0.0f);
}

void LightProbeManager::RenderDebugVisualization(ID3D12GraphicsCommandList* commandList) {
    if (!initialized_) {
        return;
    }

    for (LightProbeVolume* volume : volumes_) {
        renderer_.RenderProbeVisualization(commandList, volume);
    }
}

void LightProbeManager::Shutdown() {
    // Remove all volumes
    for (LightProbeVolume* volume : volumes_) {
        delete volume;
    }
    volumes_.clear();

    // Shutdown renderer
    renderer_.Shutdown();

    initialized_ = false;
    NEXT_LOG_INFO("Light probe manager shutdown complete");
}

//=============================================================================
// Probe Capture Implementation
//=============================================================================

ProbeCapture::ProbeCapture()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , initialized_(false) {
}

ProbeCapture::~ProbeCapture() {
    Shutdown();
}

bool ProbeCapture::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                             const ProbeCaptureSettings& settings) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for probe capture");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    settings_ = settings;

    // Create cube render target
    if (!CreateCubeRenderTarget()) {
        NEXT_LOG_ERROR("Failed to create cube render target");
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("Probe capture initialized (resolution: %d)",
                  settings_.captureResolution);
    return true;
}

bool ProbeCapture::CreateCubeRenderTarget() {
    // Placeholder - create cubemap render target
    NEXT_LOG_WARNING("Probe capture cube render target not yet implemented");
    return true;
}

ID3D12Resource* ProbeCapture::CaptureEnvironment(ID3D12GraphicsCommandList* commandList,
                                                 const Vec3& position,
                                                 ID3D12Resource* sceneColor,
                                                 ID3D12Resource* sceneDepth) {
    // Placeholder - render scene to cubemap from probe position
    NEXT_LOG_WARNING("Probe capture not yet implemented - using placeholder");
    return nullptr;
}

SphericalHarmonics ProbeCapture::ProjectToSH(ID3D12GraphicsCommandList* commandList,
                                            ID3D12Resource* cubemap) {
    // Placeholder - project cubemap to spherical harmonics
    NEXT_LOG_WARNING("SH projection not yet implemented - using placeholder");
    SphericalHarmonics sh;
    return sh;
}

void ProbeCapture::Shutdown() {
    cubeRenderTarget_.Reset();
    cubeDepthStencil_.Reset();
    captureTexture_.Reset();
    captureRootSignature_.Reset();
    capturePSO_.Reset();
    initialized_ = false;
}

//=============================================================================
// Screen Space Probe GI Implementation
//=============================================================================

ScreenSpaceProbeGI::ScreenSpaceProbeGI()
    : device_(nullptr)
    , srvHeap_(nullptr)
    , probeSpacing_(16)  // 16 pixels between probes
    , width_(0)
    , height_(0)
    , initialized_(false) {
}

ScreenSpaceProbeGI::~ScreenSpaceProbeGI() {
    Shutdown();
}

bool ScreenSpaceProbeGI::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                                   uint32_t width, uint32_t height) {
    if (!device || !srvHeap) {
        NEXT_LOG_ERROR("Invalid device or heap for screen space probe GI");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    width_ = width;
    height_ = height;

    // Placeholder - create resources and pipeline
    NEXT_LOG_WARNING("Screen space probe GI not yet fully implemented");

    initialized_ = true;
    NEXT_LOG_INFO("Screen space probe GI initialized (%ux%u, spacing: %u)",
                  width, height, probeSpacing_);
    return true;
}

void ScreenSpaceProbeGI::Update(ID3D12GraphicsCommandList* commandList,
                                ID3D12Resource* depthBuffer,
                                ID3D12Resource* normalBuffer,
                                ID3D12Resource* colorBuffer) {
    if (!initialized_) {
        return;
    }

    // Placeholder - update probes from GBuffer
    NEXT_LOG_WARNING("Screen space probe update not yet implemented");
}

void ScreenSpaceProbeGI::Render(ID3D12GraphicsCommandList* commandList,
                               ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    // Placeholder - render GI
    NEXT_LOG_WARNING("Screen space probe GI render not yet implemented");
}

bool ScreenSpaceProbeGI::Resize(uint32_t width, uint32_t height) {
    // Placeholder - resize resources
    width_ = width;
    height_ = height;
    return true;
}

void ScreenSpaceProbeGI::Shutdown() {
    probeData_.Reset();
    probeHistory_.Reset();
    rootSignature_.Reset();
    updatePSO_.Reset();
    renderPSO_.Reset();
    initialized_ = false;
}

} // namespace Next
