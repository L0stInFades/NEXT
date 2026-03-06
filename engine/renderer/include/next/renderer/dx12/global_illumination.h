#pragma once

#include "next/renderer/dx12/ambient_occlusion.h"
#include "next/renderer/dx12/light_probe.h"
#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include "next/renderer/dx12/texture.h"
#include "next/renderer/dx12/shader.h"
#include "next/renderer/dx12/root_signature.h"
#include "next/renderer/dx12/pipeline_state.h"
#include "next/renderer/math/math.h"
#include <d3d12.h>
#include <wrl/client.h>

namespace Next {

//=============================================================================
// Global Illumination Techniques
//=============================================================================

enum class GITechnique : uint32_t {
    None = 0,
    LightProbes = 1,          // Light probe-based GI
    ScreenSpaceGI = 2,        // Screen-space GI (DDGI-style)
    VoxelGI = 3,              // Voxel cone tracing GI
    Hybrid = 4                // Hybrid approach (probes + SS + voxel)
};

//=============================================================================
// Global Illumination Settings
//=============================================================================

struct GISettings {
    // Technique selection
    GITechnique primaryTechnique = GITechnique::Hybrid;

    // Intensity
    float giIntensity = 1.0f;              // Overall GI intensity
    float indirectIntensity = 0.5f;        // Indirect lighting intensity
    float aoInfluence = 0.3f;              // AO contribution to final GI
    float probeInfluence = 0.7f;           // Light probe contribution

    // Quality vs Performance
    int giQuality = 2;                     // 0 = low, 1 = medium, 2 = high, 3 = ultra
    bool enableTemporalAccumulation = true; // Temporal filtering
    bool enableBilateralFilter = true;     // Edge-aware filtering
    int maxBounces = 2;                    // Number of light bounces

    // Optimization
    bool enableHalfResolution = false;     // Use half-res for performance
    int updateFrequency = 1;               // Update every N frames

    GISettings()
        : primaryTechnique(GITechnique::Hybrid)
        , giIntensity(1.0f)
        , indirectIntensity(0.5f)
        , aoInfluence(0.3f)
        , probeInfluence(0.7f)
        , giQuality(2)
        , enableTemporalAccumulation(true)
        , enableBilateralFilter(true)
        , maxBounces(2)
        , enableHalfResolution(false)
        , updateFrequency(1) {}
};

//=============================================================================
// Global Illumination
// Main GI system that combines AO, light probes, and GI techniques
//=============================================================================

class GlobalIllumination {
public:
    GlobalIllumination();
    ~GlobalIllumination();

    // Initialize GI system
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                   uint32_t width, uint32_t height);

    // Update GI (call every frame)
    void Update(ID3D12GraphicsCommandList* commandList,
               ID3D12Resource* depthBuffer,
               ID3D12Resource* normalBuffer,
               ID3D12Resource* colorBuffer);

    // Render GI to output texture
    void Render(ID3D12GraphicsCommandList* commandList,
               ID3D12Resource* output);

    // Resize handler
    bool Resize(uint32_t width, uint32_t height);

    // Cleanup
    void Shutdown();

    // Settings
    void SetSettings(const GISettings& settings) { settings_ = settings; }
    const GISettings& GetSettings() const { return settings_; }

    // Access to subsystems
    AmbientOcclusionManager* GetAOManager() { return &aoManager_; }
    LightProbeManager* GetProbeManager() { return &probeManager_; }
    ScreenSpaceProbeGI* GetScreenSpaceGI() { return &screenSpaceGI_; }

    // Get final GI output
    ID3D12Resource* GetGIOutput() { return giOutput_.Get(); }

    // Is initialized
    bool IsInitialized() const { return initialized_; }

private:
    // Create resources
    bool CreateResources();
    bool CreateShaders();
    bool CreateRootSignature();
    bool CreatePipelineStates();

    // Render passes
    void RenderAmbientOcclusion(ID3D12GraphicsCommandList* commandList);
    void RenderLightProbes(ID3D12GraphicsCommandList* commandList);
    void RenderScreenSpaceGI(ID3D12GraphicsCommandList* commandList);
    void CombineGI(ID3D12GraphicsCommandList* commandList);

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // Subsystems
    AmbientOcclusionManager aoManager_;
    LightProbeManager probeManager_;
    ScreenSpaceProbeGI screenSpaceGI_;

    // Resources
    Microsoft::WRL::ComPtr<ID3D12Resource> aoOutput_;
    Microsoft::WRL::ComPtr<ID3D12Resource> probeOutput_;
    Microsoft::WRL::ComPtr<ID3D12Resource> ssGIOutput_;
    Microsoft::WRL::ComPtr<ID3D12Resource> giOutput_;
    Microsoft::WRL::ComPtr<ID3D12Resource> giHistory_;     // Temporal history
    Microsoft::WRL::ComPtr<ID3D12Resource> giHistoryTemp_; // Ping-pong buffer

    // Pipeline for combining GI techniques
    Microsoft::WRL::ComPtr<ID3D12RootSignature> combineRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> combinePSO_;

    // Shaders
    DX12PixelShader combineShader_;

    // Constant buffer for settings
    Microsoft::WRL::ComPtr<ID3D12Resource> settingsBuffer_;

    // Dimensions
    uint32_t width_;
    uint32_t height_;

    // Settings
    GISettings settings_;

    // Frame counter for temporal updates
    uint32_t frameCount_;

    bool initialized_;
};

//=============================================================================
// VXGI (Voxel-based Global Illumination)
// High-quality GI using voxel cone tracing
//=============================================================================

class VXGI {
public:
    VXGI();
    ~VXGI();

    // Initialize VXGI
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                   const Vec3& worldMin, const Vec3& worldMax,
                   int voxelResolution);

    // Voxelize scene
    void Voxelize(ID3D12GraphicsCommandList* commandList,
                 ID3D12Resource* depthBuffer,
                 ID3D12Resource* normalBuffer,
                 ID3D12Resource* colorBuffer);

    // Cone trace GI
    void ConeTrace(ID3D12GraphicsCommandList* commandList,
                  ID3D12Resource* output);

    // Cleanup
    void Shutdown();

    bool IsInitialized() const { return initialized_; }

private:
    // Create 3D voxel texture
    bool CreateVoxelResources();
    bool CreateShaders();
    bool CreatePipelineStates();

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // Voxel resources
    Microsoft::WRL::ComPtr<ID3D12Resource> voxelAlbedo_;     // RGB albedo
    Microsoft::WRL::ComPtr<ID3D12Resource> voxelNormal_;     // RGB normal
    Microsoft::WRL::ComPtr<ID3D12Resource> voxelEmission_;   // RGB emission

    // Mipmapped voxel textures for cone tracing
    Microsoft::WRL::ComPtr<ID3D12Resource> voxelAlbedoMip_;
    Microsoft::WRL::ComPtr<ID3D12Resource> voxelNormalMip_;

    // Pipeline
    Microsoft::WRL::ComPtr<ID3D12RootSignature> voxelizationRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> voxelizationPSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> coneTraceRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> coneTracePSO_;

    // Shaders
    DX12GeometryShader voxelizationShader_;
    DX12PixelShader coneTraceShader_;

    // World bounds
    Vec3 worldMin_;
    Vec3 worldMax_;
    int voxelResolution_;

    bool initialized_;
};

//=============================================================================
// DDGI (Diffuse Depth Global Illumination)
// Screen-space probe-based GI for real-time applications
//=============================================================================

struct DDGISettings {
    int probeResolution = 256;           // Probe texture resolution
    int probeCountX = 8;                 // Number of probes in X
    int probeCountY = 4;                 // Number of probes in Y
    int probeCountZ = 8;                 // Number of probes in Z
    float probeSpacing = 2.0f;           // Distance between probes
    int raysPerProbe = 256;              // Rays for SH projection
    int irradianceOctaves = 3;           // Octaves for filtering
    float hysteresis = 0.98f;            // Temporal hysteresis
    bool useDepthBias = true;            // Depth bias for ray marching
};

class DDGI {
public:
    DDGI();
    ~DDGI();

    // Initialize DDGI
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                   const DDGISettings& settings);

    // Update probes
    void UpdateProbes(ID3D12GraphicsCommandList* commandList,
                     ID3D12Resource* depthBuffer,
                     ID3D12Resource* normalBuffer);

    // Render GI
    void Render(ID3D12GraphicsCommandList* commandList,
               ID3D12Resource* output);

    // Cleanup
    void Shutdown();

    bool IsInitialized() const { return initialized_; }

private:
    // Create probe volume
    bool CreateProbeVolume();

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // Probe data
    Microsoft::WRL::ComPtr<ID3D12Resource> probeSH_;         // SH coefficients
    Microsoft::WRL::ComPtr<ID3D12Resource> probeDepth_;      // Depth for probes
    Microsoft::WRL::ComPtr<ID3D12Resource> probeOffsets_;    // Probe offsets
    Microsoft::WRL::ComPtr<ID3D12Resource> probeHistory_;    // Temporal history

    // Pipeline
    Microsoft::WRL::ComPtr<ID3D12RootSignature> updateRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> updatePSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> renderRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> renderPSO_;

    // Shaders
    DX12ComputeShader updateShader_;
    DX12PixelShader renderShader_;

    // Settings
    DDGISettings settings_;

    bool initialized_;
};

//=============================================================================
// Lumen-like Hardware-Accelerated Ray Traced GI
// For DXR-capable GPUs (optional)
//=============================================================================

struct RayTracedGISettings {
    int maxRayBounces = 2;
    int raysPerPixel = 1;
    float rayLength = 50.0f;
    float temporalWeight = 0.9f;
    bool useDenoiser = true;
    bool useSpatialReuse = true;
};

class RayTracedGI {
public:
    RayTracedGI();
    ~RayTracedGI();

    // Initialize RTGI
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                   const RayTracedGISettings& settings);

    // Render RTGI
    void Render(ID3D12GraphicsCommandList* commandList,
               ID3D12Resource* depthBuffer,
               ID3D12Resource* normalBuffer,
               ID3D12Resource* output);

    // Cleanup
    void Shutdown();

    bool IsInitialized() const { return initialized_; }
    bool IsDXRAvailable() const { return dxrAvailable_; }

private:
    // Check DXR support
    bool CheckDXRSupport();

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // Ray tracing resources
    Microsoft::WRL::ComPtr<ID3D12Resource> raytracingOutput_;
    Microsoft::WRL::ComPtr<ID3D12Resource> denoisedOutput_;
    Microsoft::WRL::ComPtr<ID3D12Resource> temporalHistory_;

    // Pipeline
    Microsoft::WRL::ComPtr<ID3D12StateObject> rtpso_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> globalRootSignature_;

    // Settings
    RayTracedGISettings settings_;

    bool dxrAvailable_;
    bool initialized_;
};

//=============================================================================
// GI Manager
// High-level manager for all GI techniques
//=============================================================================

class GIManager {
public:
    GIManager();
    ~GIManager();

    // Initialize GI manager
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                   uint32_t width, uint32_t height);

    // Set GI technique
    void SetGITechnique(GITechnique technique);

    // Update GI
    void Update(ID3D12GraphicsCommandList* commandList,
               ID3D12Resource* depthBuffer,
               ID3D12Resource* normalBuffer,
               ID3D12Resource* colorBuffer);

    // Render GI
    void Render(ID3D12GraphicsCommandList* commandList,
               ID3D12Resource* output);

    // Resize
    bool Resize(uint32_t width, uint32_t height);

    // Cleanup
    void Shutdown();

    // Settings
    void SetSettings(const GISettings& settings);
    const GISettings& GetSettings() const { return settings_; }

    // Access to subsystems
    AmbientOcclusionManager* GetAOManager() { return gi_.GetAOManager(); }
    LightProbeManager* GetProbeManager() { return gi_.GetProbeManager(); }

    // Is initialized
    bool IsInitialized() const { return initialized_; }

private:
    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // GI systems
    GlobalIllumination gi_;
    VXGI vxgi_;
    DDGI ddgi_;
    RayTracedGI rti_;

    // Current technique
    GITechnique currentTechnique_;

    // Settings
    GISettings settings_;

    // Dimensions
    uint32_t width_;
    uint32_t height_;

    bool initialized_;
};

} // namespace Next
