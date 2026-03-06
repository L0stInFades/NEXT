#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include "next/renderer/dx12/buffer.h"
#include "next/renderer/dx12/constant_buffer.h"
#include "next/renderer/dx12/depth_buffer.h"
#include "next/renderer/dx12/descriptor_allocator.h"
#include "next/renderer/dx12/pbr_material.h"
#include "next/renderer/dx12/shader.h"
#include "next/renderer/dx12/light.h"
#include "next/renderer/math/math.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

namespace Next {

// Forward declarations
class DX12Device;
class DX12DescriptorHeap;
class DX12RootSignature;
class DX12PipelineState;

// PBR Renderer - Complete PBR rendering pipeline
// Design principles:
// - Sustainable Experimental: Self-contained, easy to test and modify
// - Advanced: Full Cook-Torrance BRDF with multiple lights
// - Refactor Friendly: Clear separation of concerns
class PBRRenderer {
public:
    // Vertex structure for PBR rendering
    struct PBRVertex {
        float position[3];
        float normal[3];
        float texCoord[2];
        float tangent[3];
        float bitangent[3];
    };

    PBRRenderer();
    ~PBRRenderer();

    // Initialize PBR renderer
    bool Initialize(DX12Device* device, DX12DescriptorHeapManager* heapManager,
                    DX12DescriptorHeap* dsvHeap,
                    uint32_t width, uint32_t height);

    // Create PBR material asset
    PBRMaterialAsset* CreateMaterial();

    // Render scene
    void Render(ID3D12GraphicsCommandList* commandList, double time);

    // Update lighting scene
    void UpdateLighting(const LightingScene& scene);

    // Resize (for window resize)
    bool Resize(uint32_t width, uint32_t height);

    // Cleanup
    void Shutdown();

    // Get viewport
    const D3D12_VIEWPORT& GetViewport() const { return viewport_; }
    const D3D12_RECT& GetScissorRect() const { return scissorRect_; }

    // Camera control
    void SetCameraPosition(const Vec3& pos);
    void SetCameraTarget(const Vec3& target);
    void SetCameraUp(const Vec3& up);

private:
    // Create geometry (sphere for PBR testing)
    bool CreateSphereGeometry();
    bool CreatePlaneGeometry();

    // Create pipeline state
    bool CreateRootSignature();
    bool CreatePipelineState();

    // Update transform matrices
    void UpdateTransforms(double time);
    void UpdateLightingBuffers();

    // Device resources
    DX12Device* device_;
    DX12DescriptorHeapManager* heapManager_;
    DX12DescriptorHeap* srvHeap_;
    DX12DescriptorHeap* dsvHeap_;

    // Geometry buffers
    DX12Buffer vertexBuffer_;
    DX12Buffer indexBuffer_;

    // Constant buffers
    DX12ConstantBuffer transformBuffer_;   // Model, View, Projection
    DX12ConstantBuffer materialBuffer_;    // PBR material parameters
    DX12ConstantBuffer lightingBuffer_;    // Lights and camera

    // Depth buffer
    DX12DepthBuffer depthBuffer_;

    // Pipeline objects
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    // Descriptor bindings
    D3D12_GPU_DESCRIPTOR_HANDLE srvTableHandle_;
    DescriptorAllocation srvAllocation_;

    // Shaders
    DX12Shader* vertexShader_;
    DX12Shader* pixelShader_;

    // Materials
    std::vector<PBRMaterialAsset*> materials_;

    // Lighting scene
    LightingScene lightingScene_;

    // Camera
    Vec3 cameraPosition_;
    Vec3 cameraTarget_;
    Vec3 cameraUp_;

    // Viewport and scissor
    D3D12_VIEWPORT viewport_;
    D3D12_RECT scissorRect_;

    // Dimensions
    uint32_t width_;
    uint32_t height_;

    // Geometry info
    uint32_t numVertices_;
    uint32_t numIndices_;

    // State
    bool initialized_;
};

} // namespace Next
