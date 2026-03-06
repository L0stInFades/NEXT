#pragma once

#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/command_list.h"
#include "next/renderer/dx12/buffer.h"
#include "next/renderer/dx12/constant_buffer.h"
#include "next/renderer/dx12/depth_buffer.h"
#include "next/renderer/dx12/shader.h"
#include "next/renderer/dx12/root_signature.h"
#include "next/renderer/dx12/pipeline_state.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include "next/renderer/math/math.h"
#include <wrl/client.h>

namespace Next {

// Cube Renderer - Phase 3: 3D Cube with MVP Transform
// Design principles:
// - Self-contained component (refactor-friendly)
// - Clear separation of concerns
// - Easy to test and modify
class CubeRenderer {
public:
    struct CubeVertex {
        float position[3];
        float color[3];
    };

    CubeRenderer();
    ~CubeRenderer();

    // Initialize cube renderer
    bool Initialize(DX12Device* device, DX12DescriptorHeap* dsvHeap,
                    uint32_t width, uint32_t height);

    void Shutdown();

    // Resize (for window resize)
    bool Resize(uint32_t width, uint32_t height);

    // Render the cube
    void Render(ID3D12GraphicsCommandList* commandList, double time);

    // Accessors for testing
    bool IsInitialized() const { return initialized_; }
    const DX12Buffer& GetVertexBuffer() const { return vertexBuffer_; }
    const DX12Buffer& GetIndexBuffer() const { return indexBuffer_; }
    const DX12ConstantBuffer& GetConstantBuffer() const { return constantBuffer_; }

private:
    bool CreateCubeGeometry();
    bool CreateShaders();
    bool CreateRootSignature();
    bool CreatePipelineState();
    void UpdateMVPMatrix(double time);

    // Device resources
    DX12Device* device_;
    DX12DescriptorHeap* dsvHeap_;

    // Geometry
    DX12Buffer vertexBuffer_;
    DX12Buffer indexBuffer_;
    static const uint32_t NUM_VERTICES = 8;
    static const uint32_t NUM_INDICES = 36;

    // Constant buffer (MVP matrices)
    struct MVPConstants {
        Mat4 modelMatrix;
        Mat4 viewMatrix;
        Mat4 projectionMatrix;
        float time;
        float padding[3];
    };
    DX12ConstantBuffer constantBuffer_;

    // Depth buffer
    DX12DepthBuffer depthBuffer_;

    // Shaders and pipeline
    DX12VertexShader vertexShader_;
    DX12PixelShader pixelShader_;
    DX12RootSignature rootSignature_;
    DX12PipelineState pipelineState_;

    // Viewport
    D3D12_VIEWPORT viewport_;
    D3D12_RECT scissorRect_;
    uint32_t width_;
    uint32_t height_;

    bool initialized_;
};

} // namespace Next
