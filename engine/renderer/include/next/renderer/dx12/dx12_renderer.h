#pragma once

#include "next/renderer/renderer.h"
#include "next/renderer/dx12/device.h"
#include "next/renderer/dx12/command_queue.h"
#include "next/renderer/dx12/command_list.h"
#include "next/renderer/dx12/swapchain.h"
#include "next/renderer/dx12/shader.h"
#include "next/renderer/dx12/root_signature.h"
#include "next/renderer/dx12/pipeline_state.h"
#include "next/renderer/dx12/buffer.h"
#include "next/renderer/dx12/constant_buffer.h"
#include "next/renderer/dx12/descriptor_heap.h"
#include "next/renderer/dx12/descriptor_allocator.h"
#include "next/renderer/dx12/texture.h"
#include "next/renderer/dx12/sampler.h"
#include "next/renderer/dx12/light.h"
#include "next/renderer/dx12/material.h"
#include "next/renderer/dx12/post_processing.h"
#include "next/renderer/dx12/taa.h"
#include "next/renderer/dx12/debug_views.h"
#include "next/renderer/dx12/ambient_occlusion.h"
#include "next/renderer/dx12/light_probe.h"
#include "next/renderer/dx12/global_illumination.h"
#include "next/renderer/dx12/mesh_shader_pass.h"
#include "next/renderer/render_graph.h"
#include "next/renderer/math/math.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <wrl/client.h>

namespace Next {

// Render Mode (for experimentation)
enum class RenderMode {
    Basic = 0,      // Basic texture mapping
    PBR = 1         // PBR lighting
};

// DX12 Renderer Implementation
class DX12Renderer : public Renderer {
public:
    using OverlayRenderCallback = std::function<void(ID3D12GraphicsCommandList*)>;

    DX12Renderer();
    ~DX12Renderer() override;

    // Renderer interface
    bool Initialize(Window* window) override;
    void Shutdown() override;

    const char* GetBackendName() const override { return "dx12"; }

    void SetFrameDesc(const RendererFrameDesc& frame) override;
    void BeginFrame() override;
    void EndFrame() override;
    void Render() override;

    void Resize(int width, int height) override;

    // Lighting system access (for experimental control)
    LightingScene& GetLightingScene() { return lightingScene_; }
    void SetRenderMode(RenderMode mode) { renderMode_ = mode; }

    // Advanced rendering features
    GIManager* GetGIManager() { return &giManager_; }
    AmbientOcclusionManager* GetAOManager() { return giManager_.GetAOManager(); }
    LightProbeManager* GetProbeManager() { return giManager_.GetProbeManager(); }

    // Editor/tooling hooks (kept optional; runtime doesn't depend on tools).
    void SetOverlayRenderCallback(OverlayRenderCallback callback) { overlayCallback_ = std::move(callback); }
    ID3D12Device* GetD3DDevice() const { return device_.GetDevice(); } // ID3D12Device5* is-a ID3D12Device*
    ID3D12CommandQueue* GetD3DCommandQueue() const { return commandQueue_.GetQueue(); }
    DXGI_FORMAT GetBackBufferFormat() const { return swapchain_.GetFormat(); }
    uint32_t GetBackBufferWidth() const { return swapchain_.GetWidth(); }
    uint32_t GetBackBufferHeight() const { return swapchain_.GetHeight(); }
    uint32_t GetFramesInFlight() const { return DX12CommandQueue::MAX_FRAME_IN_FLIGHT; }

private:
    void QueueResize(int width, int height);
    void ApplyPendingResizeIfAny();

    bool CreateDeviceResources();
    bool CreateWindowResources();
    bool CreateDepthBuffer();
    bool CreateSceneColorTarget();
    bool CreateTemporalAATarget();
    bool CreatePipelineResources();
    bool CreateMeshShaderResources();
    bool CreateSamplerFeedbackResources();
    bool CreatePBRResources();
    bool CreateDebugCellResources();
    bool UpdateConstantBuffer(float time);
    bool UpdateLightingBuffers();
    void RenderCube();
    void RenderPBRCube();
    void RenderMeshShaderDebug();
    void RenderSamplerFeedbackDebug();
    void RenderDebugCells();
    void WaitForGPU();

    DX12Device device_;
    DX12CommandQueue commandQueue_;
    DX12CommandList commandList_;
    DX12Swapchain swapchain_;

    // Depth buffer
    DX12DSVHeap dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_;
    DX12RTVHeap sceneColorRTVHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> sceneColor_;
    DescriptorAllocation sceneColorSrvAllocation_;
    DX12RTVHeap taaRTVHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> taaOutput_;
    DescriptorAllocation taaSrvAllocation_;

    // Basic cube rendering resources (texture mapping)
    DX12RootSignature rootSignature_;
    DX12VertexShader vertexShader_;
    DX12PixelShader pixelShader_;
    DX12PipelineState pipelineState_;
    DX12PipelineState pipelineStateWireframe_;
    DX12Buffer vertexBuffer_;
    DX12Buffer indexBuffer_;
    DX12Buffer constantBuffer_;
    DX12Buffer debugCellVertexBuffer_;
    DX12Buffer debugCellIndexBuffer_;
    DX12ConstantBuffer debugCellFrameBuffer_;
    UINT debugCellIndexCount_ = 0;

    // PBR rendering resources
    DX12RootSignature pbrRootSignature_;
    DX12VertexShader pbrVertexShader_;
    DX12PixelShader pbrPixelShader_;
    DX12PipelineState pbrPipelineState_;
    DX12PipelineState pbrPipelineStateWireframe_;
    DX12Buffer pbrVertexBuffer_;
    DX12Buffer pbrIndexBuffer_;
    DX12ConstantBuffer pbrMVPBuffer_;
    DX12ConstantBuffer pbrMaterialBuffer_;
    DX12ConstantBuffer pbrLightingBuffer_;
    DX12Material pbrMaterial_;
    DX12Sampler pbrSampler_;
    DescriptorAllocation pbrTextureAllocation_;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> pbrIndirectSignature_;
    DX12Buffer pbrIndirectArgs_;

    // Texture mapping resources
    DX12DescriptorHeapManager descriptorHeapManager_;
    DX12DescriptorHeap* srvHeap_;
    DX12DescriptorHeap* samplerHeap_;
    DX12Texture texture_;
    DX12Sampler sampler_;
    DX12ComputeShader samplerFeedbackShader_;
    DX12RootSignature samplerFeedbackRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> samplerFeedbackPSO_;
    Microsoft::WRL::ComPtr<ID3D12Resource> samplerFeedbackMap_;
    DescriptorAllocation samplerFeedbackUavAllocation_;

    // PBR Lighting
    LightingScene lightingScene_;

    // Advanced Rendering Features
    GIManager giManager_;
    TemporalAA temporalAA_;
    PostProcessing postProcessing_;
    DebugViews debugViews_;
    MeshShaderPass meshShaderDebugPass_;

    // Cube data
    static const UINT NumCubeVertices = 8;
    static const UINT NumCubeIndices = 36;

    D3D12_VIEWPORT viewport_;
    D3D12_RECT scissorRect_;

    Window* window_ = nullptr;
    HWND hwnd_;
    UINT width_;
    UINT height_;
    std::atomic<bool> pendingResize_{false};
    std::atomic<uint32_t> pendingWidth_{0};
    std::atomic<uint32_t> pendingHeight_{0};
    float time_;
    double deltaTime_;
    std::chrono::steady_clock::time_point lastFrameTime_;
    RenderMode renderMode_;
    bool useGpuDriven_;
    bool meshShaderDebugEnabled_;
    bool samplerFeedbackDebugEnabled_;
    bool samplerFeedbackDispatchLogged_;
    bool conservativeGpuSync_;
    bool frameRecording_;
    bool initialized_;
    bool descriptorHeapHighWatermarkLogged_ = false;
    RendererFrameDesc frameDesc_;

    RenderGraph renderGraph_;
    OverlayRenderCallback overlayCallback_;
};

} // namespace Next
