#pragma once

#include "next/renderer/math/math.h"
#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include "next/renderer/dx12/texture.h"
#include "next/renderer/dx12/shader.h"
#include "next/renderer/dx12/root_signature.h"
#include "next/renderer/dx12/pipeline_state.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <memory>

namespace Next {

//=============================================================================
// Spherical Harmonics - for efficient light probe storage
//=============================================================================

// Order 3 spherical harmonics (9 coefficients per color channel)
// L0, L1, L2 bands - sufficient for high-quality indirect lighting
struct SphericalHarmonics {
    // 9 coefficients per RGB channel (27 total)
    Vec3 L[9];  // L[0-2] = band 0, L[3-8] = band 1, L[9-26] = band 2

    // Default constructor - black (no light)
    SphericalHarmonics() {
        for (int i = 0; i < 9; ++i) {
            L[i] = Vec3(0.0f, 0.0f, 0.0f);
        }
    }

    // Evaluate SH at a direction (returns RGB irradiance)
    Vec3 Evaluate(const Vec3& direction) const;

    // Add another SH to this one
    void Add(const SphericalHarmonics& other);

    // Scale SH coefficients
    void Scale(float scale);

    // Convert from radiance to irradiance (divide by PI)
    void ToIrradiance();

    // Project a single light source onto SH
    void ProjectLight(const Vec3& direction, const Vec3& color, float intensity);
};

//=============================================================================
// Light Probe
//=============================================================================

struct LightProbe {
    Vec3 position;                    // Probe position in world space
    SphericalHarmonics sh;            // Stored indirect lighting (SH coefficients)
    float radius;                     // Influence radius for interpolation
    float interior;                   // Interior vs exterior probe (0-1)
    Vec3 normal;                      // Probe normal (for directional bias)
    int padding;                      // Alignment

    LightProbe()
        : position(0.0f, 0.0f, 0.0f)
        , radius(1.0f)
        , interior(0.0f)
        , normal(0.0f, 1.0f, 0.0f)
        , padding(0) {}

    // Evaluate irradiance at a direction from this probe
    Vec3 EvaluateIrradiance(const Vec3& direction) const {
        return sh.Evaluate(direction);
    }
};

//=============================================================================
// Light Probe Volume
// Manages a 3D grid of light probes for interpolation
//=============================================================================

class LightProbeVolume {
public:
    LightProbeVolume();
    ~LightProbeVolume();

    // Initialize probe volume
    bool Initialize(const Vec3& origin, const Vec3& size,
                   int probesX, int probesY, int probesZ);

    // Get probe at grid position
    LightProbe* GetProbe(int x, int y, int z);

    // Find nearest probes for interpolation (returns up to 8 probes)
    std::vector<LightProbe*> FindNearestProbes(const Vec3& position);

    // Interpolate irradiance at world position
    Vec3 InterpolateIrradiance(const Vec3& position, const Vec3& normal);

    // Update probe SH coefficients (for baking or runtime updates)
    void UpdateProbe(int x, int y, int z, const SphericalHarmonics& sh);

    // Get volume bounds
    Vec3 GetOrigin() const { return origin_; }
    Vec3 GetSize() const { return size_; }

    // Get probe counts
    int GetProbeCountX() const { return probesX_; }
    int GetProbeCountY() const { return probesY_; }
    int GetProbeCountZ() const { return probesZ_; }
    int GetTotalProbeCount() const { return probesX_ * probesY_ * probesZ_; }

    // Get raw probe data (for GPU upload)
    const std::vector<LightProbe>& GetProbes() const { return probes_; }
    std::vector<LightProbe>& GetProbes() { return probes_; }

private:
    Vec3 origin_;                    // Volume origin (min corner)
    Vec3 size_;                      // Volume size (extents)
    int probesX_, probesY_, probesZ_; // Probe counts per axis
    std::vector<LightProbe> probes_;  // Flat probe storage
};

//=============================================================================
// Light Probe Renderer
// Renders and updates light probes on the GPU
//=============================================================================

class LightProbeRenderer {
public:
    LightProbeRenderer();
    ~LightProbeRenderer();

    // Initialize probe renderer
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap);

    // Bake probes (offline or runtime)
    void BakeProbes(LightProbeVolume* volume,
                   ID3D12GraphicsCommandList* commandList,
                   ID3D12Resource* sceneDepth,
                   ID3D12Resource* sceneNormal);

    // Update probes dynamically
    void UpdateProbes(ID3D12GraphicsCommandList* commandList);

    // Render probe visualization (debug)
    void RenderProbeVisualization(ID3D12GraphicsCommandList* commandList,
                                  const LightProbeVolume* volume);

    // Upload probe data to GPU
    void UploadProbeData(ID3D12GraphicsCommandList* commandList,
                        const LightProbeVolume* volume);

    // Cleanup
    void Shutdown();

    // Is initialized
    bool IsInitialized() const { return initialized_; }

    // Get probe GPU resource (for shader binding)
    ID3D12Resource* GetProbeBuffer() const { return probeBuffer_.Get(); }

private:
    // Create resources
    bool CreateShaders();
    bool CreateRootSignature();
    bool CreatePipelineStates();
    bool CreateResources();

    // Baking methods
    void BakeProbeSH(LightProbe* probe, ID3D12GraphicsCommandList* commandList);

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // GPU resources
    Microsoft::WRL::ComPtr<ID3D12Resource> probeBuffer_;     // Structured buffer for probes
    Microsoft::WRL::ComPtr<ID3D12Resource> probeBufferUpload_; // Upload buffer

    // Visualization resources
    Microsoft::WRL::ComPtr<ID3D12Resource> probeSphereMesh_;
    Microsoft::WRL::ComPtr<ID3D12Resource> probeSphereIndices_;

    // Pipeline
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> visualizationPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bakingPSO_;

    // Shaders
    DX12VertexShader visVertexShader_;
    DX12PixelShader visPixelShader_;
    DX12ComputeShader bakingComputeShader_;

    // Probe info
    uint32_t maxProbes_;
    uint32_t currentProbeCount_;
    bool probeBufferShaderReadable_;

    bool initialized_;
    bool renderEvidenceLogged_;
};

//=============================================================================
// Light Probe Manager
// Manages multiple probe volumes and rendering
//=============================================================================

class LightProbeManager {
public:
    LightProbeManager();
    ~LightProbeManager();

    // Initialize probe manager
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap);

    // Create new probe volume
    LightProbeVolume* CreateVolume(const Vec3& origin, const Vec3& size,
                                  int probesX, int probesY, int probesZ);

    // Remove volume
    void RemoveVolume(LightProbeVolume* volume);

    // Update all probes (call once per frame or when scene changes)
    void UpdateProbes(ID3D12GraphicsCommandList* commandList,
                     ID3D12Resource* sceneDepth,
                     ID3D12Resource* sceneNormal);

    // Get irradiance at world position (searches all volumes)
    Vec3 SampleIrradiance(const Vec3& position, const Vec3& normal);

    // Render probe visualization (debug)
    void RenderDebugVisualization(ID3D12GraphicsCommandList* commandList);

    // Cleanup
    void Shutdown();

    // Is initialized
    bool IsInitialized() const { return initialized_; }

    // Get probe renderer (for shader binding)
    LightProbeRenderer* GetRenderer() { return &renderer_; }

    // Get all volumes
    const std::vector<LightProbeVolume*>& GetVolumes() const { return volumes_; }

private:
    bool CreateShaders();
    bool CreateRootSignature();
    bool CreatePipelineStates();

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // Probe volumes
    std::vector<LightProbeVolume*> volumes_;

    // Probe renderer
    LightProbeRenderer renderer_;

    bool initialized_;
};

//=============================================================================
// Light Probe GBuffer Capture
// Captures scene radiance for probe baking
//=============================================================================

struct ProbeCaptureSettings {
    int captureResolution = 256;     // Cubemap face resolution
    int captureMipLevels = 5;        // Mip levels for filtering
    float nearPlane = 0.1f;          // Near plane for capture
    float farPlane = 100.0f;         // Far plane for capture
    bool filterEdges = true;         // Edge filtering for better SH projection
};

class ProbeCapture {
public:
    ProbeCapture();
    ~ProbeCapture();

    // Initialize capture system
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                   const ProbeCaptureSettings& settings);

    // Capture environment at probe position
    ID3D12Resource* CaptureEnvironment(ID3D12GraphicsCommandList* commandList,
                                      const Vec3& position,
                                      ID3D12Resource* sceneColor,
                                      ID3D12Resource* sceneDepth);

    // Project captured cubemap to SH
    SphericalHarmonics ProjectToSH(ID3D12GraphicsCommandList* commandList,
                                   ID3D12Resource* cubemap);

    // Cleanup
    void Shutdown();

    bool IsInitialized() const { return initialized_; }

private:
    // Create cube render target
    bool CreateCubeRenderTarget();

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // Capture resources
    Microsoft::WRL::ComPtr<ID3D12Resource> cubeRenderTarget_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cubeDepthStencil_;
    Microsoft::WRL::ComPtr<ID3D12Resource> captureTexture_;

    // Pipeline
    Microsoft::WRL::ComPtr<ID3D12RootSignature> captureRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> capturePSO_;

    // Settings
    ProbeCaptureSettings settings_;

    bool initialized_;
};

//=============================================================================
// Irradiance Probe (simplified for real-time GI)
//=============================================================================

struct IrradianceProbe {
    Vec3 position;
    Vec3 irradiance[9];  // SH coefficients (RGB for each)
    float radius;
    float validity;      // For temporal updates (0-1)

    IrradianceProbe()
        : position(0.0f, 0.0f, 0.0f)
        , radius(1.0f)
        , validity(1.0f) {
        for (int i = 0; i < 9; ++i) {
            irradiance[i] = Vec3(0.0f, 0.0f, 0.0f);
        }
    }
};

// Real-time GI using screen-space probes (DDGI-style)
class ScreenSpaceProbeGI {
public:
    ScreenSpaceProbeGI();
    ~ScreenSpaceProbeGI();

    // Initialize
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                   uint32_t width, uint32_t height);

    // Update probes (every frame)
    void Update(ID3D12GraphicsCommandList* commandList,
               ID3D12Resource* depthBuffer,
               ID3D12Resource* normalBuffer,
               ID3D12Resource* colorBuffer);

    // Render GI
    void Render(ID3D12GraphicsCommandList* commandList,
               ID3D12Resource* output);

    // Resize
    bool Resize(uint32_t width, uint32_t height);

    // Shutdown
    void Shutdown();

    bool IsInitialized() const { return initialized_; }

private:
    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // Probe data
    Microsoft::WRL::ComPtr<ID3D12Resource> probeData_;
    Microsoft::WRL::ComPtr<ID3D12Resource> probeHistory_;  // Temporal history
    DX12RTVHeap rtvHeap_;

    // Pipeline
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> updatePSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> renderPSO_;

    // Shaders
    DX12VertexShader fullscreenVertexShader_;
    DX12ComputeShader updateShader_;
    DX12PixelShader renderShader_;

    // Settings
    uint32_t probeSpacing_;  // Pixels between probes
    uint32_t width_;
    uint32_t height_;

    bool initialized_;
};

} // namespace Next
