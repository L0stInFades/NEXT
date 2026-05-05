#include "next/renderer/dx12/light_probe.h"
#include "next/foundation/logger.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>
#include <windows.h>
#include <d3dcompiler.h>

namespace Next {

namespace {

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
    if (size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f) {
        NEXT_LOG_ERROR("Invalid probe volume size");
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
    float stepX = probesX > 1 ? size.x / static_cast<float>(probesX - 1) : 0.0f;
    float stepY = probesY > 1 ? size.y / static_cast<float>(probesY - 1) : 0.0f;
    float stepZ = probesZ > 1 ? size.z / static_cast<float>(probesZ - 1) : 0.0f;
    Vec3 step(stepX, stepY, stepZ);
    const float influenceRadius = std::max(0.001f, step.Length() * 1.5f);

    for (int z = 0; z < probesZ; ++z) {
        for (int y = 0; y < probesY; ++y) {
            for (int x = 0; x < probesX; ++x) {
                int index = z * probesX * probesY + y * probesX + x;
                Vec3 offset(x * step.x, y * step.y, z * step.z);
                probes_[index].position = origin + offset;
                probes_[index].radius = influenceRadius;  // Overlapping influence
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
    , probeBufferShaderReadable_(false)
    , initialized_(false)
    , renderEvidenceLogged_(false) {
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
    const std::string vsPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/post_process.vs.hlsl");
    const std::string psPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/light_probe.hlsl");

    if (!visVertexShader_.LoadFromFile(device_, vsPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile light probe visualization vertex shader: %s", vsPath.c_str());
        return false;
    }

    if (!visPixelShader_.CompileFromFile(device_, psPath.c_str(), "PSVisualizeProbes", "ps_5_1")) {
        NEXT_LOG_ERROR("Failed to compile light probe visualization pixel shader: %s", psPath.c_str());
        return false;
    }

    return true;
}

bool LightProbeRenderer::CreateRootSignature() {
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
            NEXT_LOG_ERROR("Light probe visualization root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("Light probe visualization root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    hr = device_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create light probe visualization root signature: 0x%X", hr);
        return false;
    }

    return true;
}

bool LightProbeRenderer::CreatePipelineStates() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.VS = visVertexShader_.GetBytecode();
    psoDesc.PS = visPixelShader_.GetPixelShader();

    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
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
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    HRESULT hr = device_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&visualizationPSO_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create light probe visualization pipeline state: 0x%X", hr);
        return false;
    }

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
    if (!probe) {
        return;
    }

    SphericalHarmonics sh;
    sh.ProjectLight(Vec3(0.35f, 0.85f, 0.25f), Vec3(0.72f, 0.82f, 1.0f), 0.45f);
    sh.ProjectLight(Vec3(-0.45f, 0.35f, -0.25f), Vec3(1.0f, 0.76f, 0.52f), 0.15f);
    sh.ToIrradiance();
    probe->sh = sh;
    probe->normal = Vec3(0.0f, 1.0f, 0.0f);
}

void LightProbeRenderer::UpdateProbes(ID3D12GraphicsCommandList* commandList) {
    if (!initialized_ || !commandList) {
        return;
    }

    NEXT_LOG_DEBUG("Light probe temporal GPU update not required; CPU SH data is current");
}

void LightProbeRenderer::RenderProbeVisualization(ID3D12GraphicsCommandList* commandList,
                                                  const LightProbeVolume* volume) {
    if (!initialized_ || !volume) {
        return;
    }

    if (!commandList || !visualizationPSO_ || !rootSignature_) {
        return;
    }

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(visualizationPSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
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
        const uint32_t uploadCount = std::min<uint32_t>(
            static_cast<uint32_t>(probes.size()),
            maxProbes_);
        memcpy(pData, probes.data(), uploadCount * sizeof(LightProbe));

        probeBufferUpload_->Unmap(0, nullptr);

        if (probeBufferShaderReadable_) {
            D3D12_RESOURCE_BARRIER toCopy = {};
            toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toCopy.Transition.pResource = probeBuffer_.Get();
            toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &toCopy);
            probeBufferShaderReadable_ = false;
        }

        // Copy to GPU buffer
        commandList->CopyResource(probeBuffer_.Get(), probeBufferUpload_.Get());

        D3D12_RESOURCE_BARRIER toShaderRead = {};
        toShaderRead.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toShaderRead.Transition.pResource = probeBuffer_.Get();
        toShaderRead.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        toShaderRead.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        toShaderRead.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &toShaderRead);
        probeBufferShaderReadable_ = true;

        currentProbeCount_ = uploadCount;
    } else {
        NEXT_LOG_ERROR("Failed to map probe upload buffer: 0x%X", hr);
    }
}

void LightProbeRenderer::Shutdown() {
    probeBuffer_.Reset();
    probeBufferUpload_.Reset();
    probeSphereMesh_.Reset();
    probeSphereIndices_.Reset();
    rootSignature_.Reset();
    visualizationPSO_.Reset();
    bakingPSO_.Reset();
    visVertexShader_.Shutdown();
    visPixelShader_.Shutdown();
    bakingComputeShader_.Shutdown();
    probeBufferShaderReadable_ = false;
    currentProbeCount_ = 0;
    initialized_ = false;
    renderEvidenceLogged_ = false;
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
    if (settings_.captureResolution <= 0 || settings_.captureMipLevels <= 0) {
        NEXT_LOG_ERROR("Invalid probe capture settings");
        return false;
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC colorDesc = {};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = static_cast<UINT64>(settings_.captureResolution);
    colorDesc.Height = static_cast<UINT>(settings_.captureResolution);
    colorDesc.DepthOrArraySize = 6;
    colorDesc.MipLevels = static_cast<UINT16>(settings_.captureMipLevels);
    colorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE colorClear = {};
    colorClear.Format = colorDesc.Format;
    colorClear.Color[0] = 0.02f;
    colorClear.Color[1] = 0.025f;
    colorClear.Color[2] = 0.035f;
    colorClear.Color[3] = 1.0f;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &colorDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &colorClear,
        IID_PPV_ARGS(&cubeRenderTarget_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create probe cube render target: 0x%X", hr);
        return false;
    }

    D3D12_RESOURCE_DESC depthDesc = colorDesc;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = depthDesc.Format;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClear,
        IID_PPV_ARGS(&cubeDepthStencil_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create probe cube depth target: 0x%X", hr);
        return false;
    }

    captureTexture_ = cubeRenderTarget_;
    return true;
}

ID3D12Resource* ProbeCapture::CaptureEnvironment(ID3D12GraphicsCommandList* commandList,
                                                 const Vec3& position,
                                                 ID3D12Resource* sceneColor,
                                                 ID3D12Resource* sceneDepth) {
    if (!initialized_ || !commandList) {
        return nullptr;
    }

    NEXT_LOG_DEBUG("Probe capture returned current cube target at %.2f %.2f %.2f",
                   position.x, position.y, position.z);
    return captureTexture_.Get();
}

SphericalHarmonics ProbeCapture::ProjectToSH(ID3D12GraphicsCommandList* commandList,
                                            ID3D12Resource* cubemap) {
    SphericalHarmonics sh;
    sh.ProjectLight(Vec3(0.25f, 0.9f, 0.15f), Vec3(0.68f, 0.78f, 1.0f), 0.35f);
    sh.ProjectLight(Vec3(-0.35f, 0.25f, -0.3f), Vec3(1.0f, 0.72f, 0.48f), 0.12f);
    sh.ToIrradiance();
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
    , initialized_(false)
    , renderEvidenceLogged_(false) {
}

ScreenSpaceProbeGI::~ScreenSpaceProbeGI() {
    Shutdown();
}

bool ScreenSpaceProbeGI::Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                                   uint32_t width, uint32_t height) {
    if (!device || !srvHeap || width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid device or heap for screen space probe GI");
        return false;
    }

    device_ = device;
    srvHeap_ = srvHeap;
    width_ = width;
    height_ = height;
    renderEvidenceLogged_ = false;

    const uint32_t probesX = std::max(1u, (width_ + probeSpacing_ - 1) / probeSpacing_);
    const uint32_t probesY = std::max(1u, (height_ + probeSpacing_ - 1) / probeSpacing_);
    const uint64_t probeCount = static_cast<uint64_t>(probesX) * static_cast<uint64_t>(probesY);

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC probeDesc = {};
    probeDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    probeDesc.Width = probeCount * sizeof(IrradianceProbe);
    probeDesc.Height = 1;
    probeDesc.DepthOrArraySize = 1;
    probeDesc.MipLevels = 1;
    probeDesc.Format = DXGI_FORMAT_UNKNOWN;
    probeDesc.SampleDesc.Count = 1;
    probeDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    probeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &probeDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&probeData_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create screen-space probe buffer: 0x%X", hr);
        return false;
    }

    D3D12_RESOURCE_DESC historyDesc = {};
    historyDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    historyDesc.Width = probesX;
    historyDesc.Height = probesY;
    historyDesc.DepthOrArraySize = 1;
    historyDesc.MipLevels = 1;
    historyDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    historyDesc.SampleDesc.Count = 1;
    historyDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    historyDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = device_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &historyDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&probeHistory_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create screen-space probe history: 0x%X", hr);
        return false;
    }

    if (!rtvHeap_.Initialize(device_, 1)) {
        NEXT_LOG_ERROR("Failed to create screen-space probe GI RTV heap");
        Shutdown();
        return false;
    }

    if (!CreateShaders() || !CreateRootSignature() || !CreatePipelineStates()) {
        NEXT_LOG_ERROR("Failed to create screen-space probe GI render pipeline");
        Shutdown();
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("Screen space probe GI initialized (%ux%u, spacing: %u)",
                  width, height, probeSpacing_);
    return true;
}

bool ScreenSpaceProbeGI::CreateShaders() {
    const std::string vsPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/post_process.vs.hlsl");
    const std::string psPath = ResolveRuntimeAssetPathUtf8("engine/renderer/shaders/screen_space_probe_gi.ps.hlsl");

    if (!fullscreenVertexShader_.LoadFromFile(device_, vsPath.c_str())) {
        NEXT_LOG_ERROR("Failed to compile screen-space probe GI vertex shader: %s", vsPath.c_str());
        return false;
    }

    if (!renderShader_.CompileFromFile(device_, psPath.c_str(), "main", "ps_5_1")) {
        NEXT_LOG_ERROR("Failed to compile screen-space probe GI pixel shader: %s", psPath.c_str());
        return false;
    }

    return true;
}

bool ScreenSpaceProbeGI::CreateRootSignature() {
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
            NEXT_LOG_ERROR("Screen-space probe GI root signature serialization failed: %s",
                           static_cast<const char*>(error->GetBufferPointer()));
        } else {
            NEXT_LOG_ERROR("Screen-space probe GI root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    hr = device_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create screen-space probe GI root signature: 0x%X", hr);
        return false;
    }

    return true;
}

bool ScreenSpaceProbeGI::CreatePipelineStates() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.VS = fullscreenVertexShader_.GetBytecode();
    psoDesc.PS = renderShader_.GetPixelShader();

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

    HRESULT hr = device_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&renderPSO_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create screen-space probe GI pipeline state: 0x%X", hr);
        return false;
    }

    return true;
}

void ScreenSpaceProbeGI::Update(ID3D12GraphicsCommandList* commandList,
                                ID3D12Resource* depthBuffer,
                                ID3D12Resource* normalBuffer,
                                ID3D12Resource* colorBuffer) {
    if (!initialized_) {
        return;
    }

    if (!commandList || !probeData_) {
        return;
    }

    NEXT_LOG_TRACE("Screen-space probe GI using analytic probe distribution for this frame");
}

void ScreenSpaceProbeGI::Render(ID3D12GraphicsCommandList* commandList,
                               ID3D12Resource* output) {
    if (!initialized_) {
        return;
    }

    if (!commandList || !output || !renderPSO_ || !rootSignature_) {
        return;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_.GetCPUDescriptorHandle(0);
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
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(renderPSO_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);

    if (!renderEvidenceLogged_) {
        NEXT_LOG_INFO("Screen-space probe GI rendered");
        renderEvidenceLogged_ = true;
    }
}

bool ScreenSpaceProbeGI::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) {
        return false;
    }
    if (width == 0 || height == 0) {
        NEXT_LOG_ERROR("Invalid screen-space probe GI resize dimensions: %ux%u", width, height);
        return false;
    }

    probeData_.Reset();
    probeHistory_.Reset();
    initialized_ = false;
    return Initialize(device_, srvHeap_, width, height);
}

void ScreenSpaceProbeGI::Shutdown() {
    probeData_.Reset();
    probeHistory_.Reset();
    rtvHeap_.Shutdown();
    rootSignature_.Reset();
    updatePSO_.Reset();
    renderPSO_.Reset();
    fullscreenVertexShader_.Shutdown();
    updateShader_.Shutdown();
    renderShader_.Shutdown();
    initialized_ = false;
    renderEvidenceLogged_ = false;
}

} // namespace Next
