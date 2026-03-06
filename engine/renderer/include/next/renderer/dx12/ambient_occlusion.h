#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include "next/renderer/dx12/texture.h"
#include "next/renderer/dx12/shader.h"
#include "next/renderer/dx12/root_signature.h"
#include "next/renderer/dx12/pipeline_state.h"
#include "next/renderer/dx12/constant_buffer.h"
#include <d3d12.h>
#include <wrl/client.h>

namespace Next {

// Forward declarations
class DX12Device;
class DX12DescriptorHeap;

//=============================================================================
// Ambient Occlusion Types
//=============================================================================

enum class AOType : uint32_t {
    None = 0,
    GTAO = 1,        // Ground Truth Ambient Occlusion (highest quality SSAO)
    HBAO = 2,        // Horizon-Based Ambient Occlusion (good balance)
    VXAO = 3         // Voxel-based Ambient Occlusion (most expensive, best quality)
};

//=============================================================================
// Ambient Occlusion Parameters
//=============================================================================

struct GTAOParameters {
    float radius = 0.5f;              // Sampling radius (world space)
    float power = 2.0f;               // Occlusion power (contrast)
    int samples = 8;                  // Number of samples (quality vs performance)
    float temporalStability = 0.9f;   // Temporal accumulation (0-1)
    bool bilateralFilter = true;      // Use edge-aware filtering
};

struct HBAOParameters {
    float radius = 0.3f;              // Sampling radius
    float bias = 0.1f;                // Bias to reduce self-occlusion
    int steps = 4;                    // Number of steps along the direction
    float power = 2.0f;               // Occlusion power
    bool blurEnabled = true;          // Enable blur pass
};

struct VXAOParameters {
    float voxelSize = 0.1f;           // Voxel world size
    int voxelResolution = 128;        // Voxel grid resolution (per axis)
    int coneSamples = 8;              // Samples per cone direction
    int coneDirections = 6;           // Number of cone directions
    float range = 1.0f;               // Tracing range
    float hardness = 1.0f;            // Edge hardness
    bool enableVoxelization = true;   // Dynamic voxelization
};

//=============================================================================
// Base Ambient Occlusion Interface
//=============================================================================

class AmbientOcclusion {
public:
    virtual ~AmbientOcclusion() = default;

    // Initialize AO technique
    virtual bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                           uint32_t width, uint32_t height) = 0;

    // Render AO (should output to a texture)
    virtual void Render(ID3D12GraphicsCommandList* commandList,
                       ID3D12Resource* depthBuffer,
                       ID3D12Resource* normalBuffer,
                       ID3D12Resource* output) = 0;

    // Resize handler
    virtual bool Resize(uint32_t width, uint32_t height) = 0;

    // Cleanup
    virtual void Shutdown() = 0;

    // Get output texture (for integration into pipeline)
    virtual ID3D12Resource* GetOutputTexture() = 0;

    // Get type
    virtual AOType GetType() const = 0;

    // Is initialized
    virtual bool IsInitialized() const = 0;
};

//=============================================================================
// GTAO Implementation (Ground Truth Ambient Occlusion)
// Based on "Ground Truth Ambient Occlusion" (HOT3D 2016)
//=============================================================================

class GTAO : public AmbientOcclusion {
public:
    GTAO();
    ~GTAO() override;

    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                   uint32_t width, uint32_t height) override;
    void Render(ID3D12GraphicsCommandList* commandList,
               ID3D12Resource* depthBuffer,
               ID3D12Resource* normalBuffer,
               ID3D12Resource* output) override;
    bool Resize(uint32_t width, uint32_t height) override;
    void Shutdown() override;

    ID3D12Resource* GetOutputTexture() override;
    AOType GetType() const override { return AOType::GTAO; }
    bool IsInitialized() const override { return initialized_; }

    // Parameters
    void SetParameters(const GTAOParameters& params) { params_ = params; }
    const GTAOParameters& GetParameters() const { return params_; }

private:
    // Create resources
    bool CreateShaders();
    bool CreateRootSignature();
    bool CreatePipelineStates();
    bool CreateResources();

    // Rendering passes
    void RenderGTAO(ID3D12GraphicsCommandList* commandList);
    void ApplySpatialFilter(ID3D12GraphicsCommandList* commandList);

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // Resources
    Microsoft::WRL::ComPtr<ID3D12Resource> aoTexture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> aoTextureTemp_;

    // Pipeline
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gtaoPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> filterPSO_;

    // Shaders
    DX12PixelShader gtaoShader_;
    DX12PixelShader filterShader_;

    // Constant buffer
    DX12ConstantBuffer constantBuffer_;

    // Dimensions
    uint32_t width_;
    uint32_t height_;

    // Parameters
    GTAOParameters params_;

    bool initialized_;
};

//=============================================================================
// HBAO Implementation (Horizon-Based Ambient Occlusion)
// Based on NVIDIA's HBAO+ algorithm
//=============================================================================

class HBAO : public AmbientOcclusion {
public:
    HBAO();
    ~HBAO() override;

    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                   uint32_t width, uint32_t height) override;
    void Render(ID3D12GraphicsCommandList* commandList,
               ID3D12Resource* depthBuffer,
               ID3D12Resource* normalBuffer,
               ID3D12Resource* output) override;
    bool Resize(uint32_t width, uint32_t height) override;
    void Shutdown() override;

    ID3D12Resource* GetOutputTexture() override;
    AOType GetType() const override { return AOType::HBAO; }
    bool IsInitialized() const override { return initialized_; }

    // Parameters
    void SetParameters(const HBAOParameters& params) { params_ = params; }
    const HBAOParameters& GetParameters() const { return params_; }

private:
    // Create resources
    bool CreateShaders();
    bool CreateRootSignature();
    bool CreatePipelineStates();
    bool CreateResources();

    // Rendering passes
    void RenderHBAO(ID3D12GraphicsCommandList* commandList);
    void ApplyBlur(ID3D12GraphicsCommandList* commandList);

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // Resources
    Microsoft::WRL::ComPtr<ID3D12Resource> aoTexture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> aoTextureTemp_;

    // Pipeline
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> hbaoPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> blurPSO_;

    // Shaders
    DX12PixelShader hbaoShader_;
    DX12PixelShader blurShader_;

    // Constant buffer
    DX12ConstantBuffer constantBuffer_;

    // Dimensions
    uint32_t width_;
    uint32_t height_;

    // Parameters
    HBAOParameters params_;

    bool initialized_;
};

//=============================================================================
// VXAO Implementation (Voxel-based Ambient Occlusion)
// Based on voxel cone tracing for high-quality AO
//=============================================================================

class VXAO : public AmbientOcclusion {
public:
    VXAO();
    ~VXAO() override;

    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                   uint32_t width, uint32_t height) override;
    void Render(ID3D12GraphicsCommandList* commandList,
               ID3D12Resource* depthBuffer,
               ID3D12Resource* normalBuffer,
               ID3D12Resource* output) override;
    bool Resize(uint32_t width, uint32_t height) override;
    void Shutdown() override;

    ID3D12Resource* GetOutputTexture() override;
    AOType GetType() const override { return AOType::VXAO; }
    bool IsInitialized() const override { return initialized_; }

    // Parameters
    void SetParameters(const VXAOParameters& params) { params_ = params; }
    const VXAOParameters& GetParameters() const { return params_; }

private:
    // Create resources
    bool CreateShaders();
    bool CreateRootSignature();
    bool CreatePipelineStates();
    bool CreateResources();

    // Rendering passes
    void VoxelizeScene(ID3D12GraphicsCommandList* commandList);
    void RenderVXAO(ID3D12GraphicsCommandList* commandList);

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // Resources
    Microsoft::WRL::ComPtr<ID3D12Resource> aoTexture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> voxelTexture_;     // 3D voxel texture
    Microsoft::WRL::ComPtr<ID3D12Resource> voxelTextureTemp_; // For double-buffering

    // Pipeline
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> voxelizationRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> vxaoPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> voxelizationPSO_;

    // Shaders
    DX12PixelShader vxaoShader_;
    DX12GeometryShader voxelizationShader_;

    // Constant buffers
    DX12ConstantBuffer constantBuffer_;
    DX12ConstantBuffer voxelizationBuffer_;

    // Dimensions
    uint32_t width_;
    uint32_t height_;

    // Parameters
    VXAOParameters params_;

    // UAVs for voxel access
    Microsoft::WRL::ComPtr<ID3D12Resource> voxelUAV_;

    bool initialized_;
};

//=============================================================================
// Unified Ambient Occlusion Manager
// Manages AO techniques and provides simple interface
//=============================================================================

class AmbientOcclusionManager {
public:
    AmbientOcclusionManager();
    ~AmbientOcclusionManager();

    // Initialize AO manager
    bool Initialize(DX12Device* device, DX12DescriptorHeap* srvHeap,
                   uint32_t width, uint32_t height);

    // Set AO type (re-initializes if needed)
    bool SetAOType(AOType type);

    // Render AO (dispatches to active AO technique)
    void Render(ID3D12GraphicsCommandList* commandList,
               ID3D12Resource* depthBuffer,
               ID3D12Resource* normalBuffer,
               ID3D12Resource* output);

    // Resize handler
    bool Resize(uint32_t width, uint32_t height);

    // Cleanup
    void Shutdown();

    // Get current AO output
    ID3D12Resource* GetOutputTexture();

    // Get current AO type
    AOType GetCurrentType() const { return currentType_; }

    // Access to specific AO parameters
    void SetGTAOParameters(const GTAOParameters& params);
    void SetHBAOParameters(const HBAOParameters& params);
    void SetVXAOParameters(const VXAOParameters& params);

    const GTAOParameters& GetGTAOParameters() const;
    const HBAOParameters& GetHBAOParameters() const;
    const VXAOParameters& GetVXAOParameters() const;

    // Is initialized
    bool IsInitialized() const { return initialized_; }

private:
    // Create AO technique instance
    bool CreateAOTechnique(AOType type);

    // Device
    DX12Device* device_;
    DX12DescriptorHeap* srvHeap_;

    // AO technique instances
    GTAO* gtao_;
    HBAO* hbao_;
    VXAO* vxao_;
    AmbientOcclusion* currentAO_;

    // Current type
    AOType currentType_;

    // Dimensions
    uint32_t width_;
    uint32_t height_;

    bool initialized_;
};

} // namespace Next
