#include "next/renderer/dx12/dx12_renderer.h"
#include "next/foundation/logger.h"
#include <windows.h>
#include <cmath>

namespace Next {

namespace {
constexpr UINT kPbrTextureSlots = 5;

struct CubeConstants {
    Mat4 model;
    Mat4 view;
    Mat4 projection;
    float time;
    float padding[3];
};

struct CameraDataGPU {
    float position[3];
    float padding;
};

struct LightingSettingsGPU {
    float ambientColor[3];
    float ambientIntensity;
    float exposure;
    float gamma;
    int toneMapMode;
    float padding; // Align to 16 bytes
};

struct DirectionalLightGPU {
    float direction[3];
    float intensity;
    float color[3];
    int enabled;
};

struct PointLightGPU {
    float position[3];
    float intensity;
    float color[3];
    float radius;
    float constant;
    float linearAttenuation;
    float quadratic;
    int enabled;
};

struct LightingBufferGPU {
    CameraDataGPU camera;
    LightingSettingsGPU settings;
    DirectionalLightGPU directionalLight;
    PointLightGPU pointLights[LightingScene::MAX_POINT_LIGHTS];
    int numPointLights;
    int numSpotLights;
    int padding[2];
};
} // namespace

DX12Renderer::DX12Renderer()
    : srvHeap_(nullptr)
    , samplerHeap_(nullptr)
    , hwnd_(nullptr)
    , width_(0)
    , height_(0)
    , time_(0.0f)
    , deltaTime_(0.0)
    , renderMode_(RenderMode::PBR)
    , useGpuDriven_(true)
    , initialized_(false) {
}

DX12Renderer::~DX12Renderer() {
    Shutdown();
}

void DX12Renderer::QueueResize(int width, int height) {
    // Ignore minimize (0,0) and bogus sizes.
    if (width <= 0 || height <= 0) {
        return;
    }
    pendingWidth_.store(static_cast<uint32_t>(width), std::memory_order_relaxed);
    pendingHeight_.store(static_cast<uint32_t>(height), std::memory_order_relaxed);
    pendingResize_.store(true, std::memory_order_release);
}

void DX12Renderer::ApplyPendingResizeIfAny() {
    if (!pendingResize_.load(std::memory_order_acquire)) {
        return;
    }
    pendingResize_.store(false, std::memory_order_release);

    const uint32_t w = pendingWidth_.load(std::memory_order_relaxed);
    const uint32_t h = pendingHeight_.load(std::memory_order_relaxed);
    if (w == 0 || h == 0) {
        return;
    }

    // Avoid expensive GPU flush if nothing changed.
    if (w == width_ && h == height_) {
        return;
    }
    Resize(static_cast<int>(w), static_cast<int>(h));
}

bool DX12Renderer::Initialize(Window* window) {
    NEXT_LOG_INFO("Initializing DX12 Renderer...");

    if (!window || !window->GetNativeHandle()) {
        NEXT_LOG_ERROR("Invalid window handle");
        return false;
    }

    window_ = window;
    hwnd_ = static_cast<HWND>(window->GetNativeHandle());
    width_ = window->GetWidth();
    height_ = window->GetHeight();
    time_ = 0.0f;
    deltaTime_ = 0.016;  // Initial estimate

    // Keep the swapchain/backbuffer matched to the Win32 client size. Not doing this causes blur
    // (stretch/blit) and mouse hit-testing mismatch for ImGui overlays.
    window_->SetResizeCallback([this](int w, int h) { QueueResize(w, h); });

    // Initialize timing
    lastFrameTime_ = std::chrono::steady_clock::now();

    // Create device resources
    if (!CreateDeviceResources()) {
        NEXT_LOG_ERROR("Failed to create device resources");
        return false;
    }

    // Create window resources (swapchain, etc.)
    if (!CreateWindowResources()) {
        NEXT_LOG_ERROR("Failed to create window resources");
        return false;
    }

    // Create depth buffer
    if (!CreateDepthBuffer()) {
        NEXT_LOG_ERROR("Failed to create depth buffer");
        return false;
    }

    // Create pipeline resources (shaders, PSO, vertex buffer)
    if (!CreatePipelineResources()) {
        NEXT_LOG_ERROR("Failed to create pipeline resources");
        return false;
    }

    // Create PBR resources (optional - log warning if fails)
    if (!CreatePBRResources()) {
        NEXT_LOG_ERROR("Failed to create PBR resources");
        return false;
    }

    // Initialize advanced rendering features (GI system)
    if (!giManager_.Initialize(&device_, srvHeap_, width_, height_)) {
        NEXT_LOG_WARNING("Failed to initialize GI manager - advanced features disabled");
    }

    initialized_ = true;
    NEXT_LOG_INFO("DX12 Renderer initialized successfully (%ux%u)", width_, height_);
    return true;
}

void DX12Renderer::Shutdown() {
    if (!initialized_) {
        return;
    }

    NEXT_LOG_INFO("Shutting down DX12 Renderer...");

    // Wait for GPU to finish
    WaitForGPU();

    // Detach from window callbacks to avoid invoking a dead renderer during teardown.
    if (window_) {
        window_->SetResizeCallback({});
        window_ = nullptr;
    }

    // Shutdown resources
    depthBuffer_.Reset();
    dsvHeap_.Shutdown();
    constantBuffer_.Shutdown();
    indexBuffer_.Shutdown();
    vertexBuffer_.Shutdown();
    pipelineState_.Shutdown();
    pixelShader_.Shutdown();
    vertexShader_.Shutdown();
    rootSignature_.Shutdown();

    // PBR resources
    pbrMVPBuffer_.Shutdown();
    pbrMaterialBuffer_.Shutdown();
    pbrLightingBuffer_.Shutdown();
    pbrIndexBuffer_.Shutdown();
    pbrVertexBuffer_.Shutdown();
    pbrPipelineState_.Shutdown();
    pbrPixelShader_.Shutdown();
    pbrVertexShader_.Shutdown();
    pbrRootSignature_.Shutdown();
    pbrMaterial_.Shutdown();
    pbrSampler_.Shutdown();

    // Shutdown advanced rendering features
    giManager_.Shutdown();

    texture_.Shutdown();
    sampler_.Shutdown();
    srvHeap_ = nullptr;
    samplerHeap_ = nullptr;
    descriptorHeapManager_.Shutdown();
    commandList_.Shutdown();
    swapchain_.Shutdown();
    commandQueue_.Shutdown();
    device_.Shutdown();

    hwnd_ = nullptr;
    width_ = 0;
    height_ = 0;
    time_ = 0.0f;
    initialized_ = false;

    NEXT_LOG_INFO("DX12 Renderer shutdown complete");
}

void DX12Renderer::BeginFrame() {
    if (!initialized_) {
        return;
    }

    // Apply resize at a stable point (outside WndProc / mid-frame). This avoids doing ResizeBuffers()
    // from inside message dispatch and reduces chances of device removal.
    ApplyPendingResizeIfAny();

    // Begin frame-in-flight synchronization
    commandQueue_.BeginFrame();

    // Advance descriptor heap frame tracking
    descriptorHeapManager_.AdvanceFrame();

    // Reset command list
    commandList_.Reset(commandQueue_.GetFrameIndex());

    if (srvHeap_ && samplerHeap_) {
        ID3D12DescriptorHeap* heaps[] = {
            srvHeap_->GetHeap(),
            samplerHeap_->GetHeap()
        };
        commandList_.SetDescriptorHeaps(2, heaps);
    }
}

void DX12Renderer::EndFrame() {
    if (!initialized_) {
        return;
    }

    // Close command list
    commandList_.Close();

    // Execute command list
    commandQueue_.ExecuteCommandList(&commandList_);

    // Present
    swapchain_.Present(1, 0);

    // Update time with actual delta time
    auto currentTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = currentTime - lastFrameTime_;
    deltaTime_ = diff.count();
    time_ += static_cast<float>(deltaTime_);
    lastFrameTime_ = currentTime;

    // End frame-in-flight synchronization
    commandQueue_.EndFrame();

    // No need to wait for GPU here - frame-in-flight handles it
}

void DX12Renderer::Render() {
    if (!initialized_) {
        return;
    }

    // Update constant buffers
    UpdateConstantBuffer(time_);
    UpdateLightingBuffers();

    renderGraph_.Reset();

    auto backBuffer = renderGraph_.ImportRenderTarget(
        "BackBuffer",
        swapchain_.GetCurrentRenderTarget(),
        swapchain_.GetCurrentRenderTargetView(),
        D3D12_RESOURCE_STATE_PRESENT);

    auto depthBuffer = renderGraph_.ImportDepthTarget(
        "Depth",
        depthBuffer_.Get(),
        dsvHeap_.GetCPUDescriptorHandle(0),
        D3D12_RESOURCE_STATE_DEPTH_WRITE);

    renderGraph_.AddPass(
        "Main",
        [&](RenderGraphBuilder& builder) {
            builder.Write(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
            builder.Write(depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        },
        [&](RenderGraphContext& ctx) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = ctx.GetRTV(backBuffer);
            D3D12_CPU_DESCRIPTOR_HANDLE dsv = ctx.GetDSV(depthBuffer);

            float clearColor[4] = {0.39f, 0.58f, 0.93f, 1.0f}; // Cornflower blue
            commandList_.ClearRenderTargetView(rtv, clearColor);
            commandList_.ClearDepthStencilView(dsv, 1.0f, 0);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {rtv};
            commandList_.OMSetRenderTargets(1, rtvs, TRUE, dsv);

            if (renderMode_ == RenderMode::PBR) {
                RenderPBRCube();
            } else {
                RenderCube();
            }

            // Optional UI/debug overlay (e.g. editor ImGui). Runs after scene draw, before present.
            if (overlayCallback_) {
                overlayCallback_(commandList_.GetCommandList());
            }
        });

    renderGraph_.AddPass(
        "Present",
        [&](RenderGraphBuilder& builder) {
            builder.Write(backBuffer, D3D12_RESOURCE_STATE_PRESENT);
        },
        [](RenderGraphContext&) {});

    renderGraph_.Execute(&commandList_);
}

void DX12Renderer::Resize(int width, int height) {
    if (!initialized_) {
        return;
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    if (static_cast<UINT>(width) == width_ && static_cast<UINT>(height) == height_) {
        return;
    }

    NEXT_LOG_INFO("Resizing renderer to %dx%d", width, height);

    // Wait for GPU to finish
    WaitForGPU();

    // Resize swapchain
    if (!swapchain_.Resize(width, height)) {
        NEXT_LOG_ERROR("Failed to resize swapchain");
        return;
    }

    width_ = width;
    height_ = height;

    viewport_.Width = static_cast<float>(width_);
    viewport_.Height = static_cast<float>(height_);
    scissorRect_.right = width_;
    scissorRect_.bottom = height_;

    // Recreate depth buffer for new size
    depthBuffer_.Reset();
    dsvHeap_.Shutdown();
    if (!CreateDepthBuffer()) {
        NEXT_LOG_ERROR("Failed to recreate depth buffer after resize");
        return;
    }

    // Resize advanced render targets if present.
    giManager_.Resize(width_, height_);
}

bool DX12Renderer::CreateDeviceResources() {
    NEXT_LOG_DEBUG("Creating device resources...");

    // Initialize device
    if (!device_.Initialize()) {
        NEXT_LOG_ERROR("Failed to initialize DX12 device");
        return false;
    }

    // Initialize command queue
    if (!commandQueue_.Initialize(&device_, D3D12_COMMAND_LIST_TYPE_DIRECT)) {
        NEXT_LOG_ERROR("Failed to initialize command queue");
        return false;
    }

    // Initialize command list (one allocator per frame-in-flight to avoid resetting allocators while GPU is executing).
    if (!commandList_.Initialize(&device_, D3D12_COMMAND_LIST_TYPE_DIRECT, DX12CommandQueue::MAX_FRAME_IN_FLIGHT)) {
        NEXT_LOG_ERROR("Failed to initialize command list");
        return false;
    }

    // Initialize descriptor heap manager
    if (!descriptorHeapManager_.Initialize(&device_, 2)) {
        NEXT_LOG_ERROR("Failed to initialize descriptor heap manager");
        return false;
    }

    NEXT_LOG_DEBUG("Device resources created successfully");
    return true;
}

bool DX12Renderer::CreateWindowResources() {
    NEXT_LOG_DEBUG("Creating window resources...");

    // Initialize swapchain
    if (!swapchain_.Initialize(&device_, &commandQueue_, hwnd_, width_, height_)) {
        NEXT_LOG_ERROR("Failed to initialize swapchain");
        return false;
    }

    NEXT_LOG_DEBUG("Window resources created successfully");
    return true;
}

bool DX12Renderer::CreateDepthBuffer() {
    NEXT_LOG_INFO("Creating depth buffer...");

    // Create DSV heap
    if (!dsvHeap_.Initialize(&device_, 1)) {
        NEXT_LOG_ERROR("Failed to create DSV heap");
        return false;
    }

    // Create depth buffer resource
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width_;
    desc.Height = height_;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    HRESULT hr = device_.GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&depthBuffer_)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create depth buffer: 0x%X", hr);
        return false;
    }

    // Create DSV
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    device_.GetDevice()->CreateDepthStencilView(
        depthBuffer_.Get(),
        &dsvDesc,
        dsvHeap_.GetCPUDescriptorHandle(0)
    );

    NEXT_LOG_INFO("Depth buffer created successfully");
    return true;
}

void DX12Renderer::WaitForGPU() {
    // Proper fence-based synchronization
    commandQueue_.WaitForGPU();
}


bool DX12Renderer::CreatePipelineResources() {
    NEXT_LOG_INFO("Creating pipeline resources for cube rendering...");

    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(width_);
    viewport_.Height = static_cast<float>(height_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissorRect_.left = 0;
    scissorRect_.top = 0;
    scissorRect_.right = width_;
    scissorRect_.bottom = height_;

    // Create SRV and Sampler descriptor heaps
    if (!descriptorHeapManager_.CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256, true)) {
        NEXT_LOG_ERROR("Failed to create managed SRV heap");
        return false;
    }

    if (!descriptorHeapManager_.CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 64, true)) {
        NEXT_LOG_ERROR("Failed to create managed sampler heap");
        return false;
    }

    srvHeap_ = descriptorHeapManager_.GetHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    samplerHeap_ = descriptorHeapManager_.GetHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    if (!srvHeap_ || !samplerHeap_) {
        NEXT_LOG_ERROR("Managed descriptor heaps not available");
        return false;
    }

    if (!vertexShader_.LoadFromFile(&device_, "engine/renderer/shaders/cube.vs.hlsl")) {
        NEXT_LOG_ERROR("Failed to load vertex shader");
        return false;
    }

    if (!pixelShader_.LoadFromFile(&device_, "engine/renderer/shaders/cube.ps.hlsl")) {
        NEXT_LOG_ERROR("Failed to load pixel shader");
        return false;
    }

    // Create root signature with descriptor table for texture and sampler
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    D3D12_ROOT_PARAMETER rootParameters[3] = {};

    // Parameter 0: CBV (constant buffer)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 1: Descriptor table for SRV (texture)
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 2: Descriptor table for Sampler
    D3D12_DESCRIPTOR_RANGE samplerRange = {};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = 1;
    samplerRange.BaseShaderRegister = 0;
    samplerRange.RegisterSpace = 0;
    samplerRange.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[2].DescriptorTable.pDescriptorRanges = &samplerRange;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootSignatureDesc.NumParameters = 3;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error
    );

    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("Root signature serialization failed: %s", (const char*)error->GetBufferPointer());
        } else {
            NEXT_LOG_ERROR("Root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12RootSignature> tempRootSig;
    hr = device_.GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&tempRootSig)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create root signature: 0x%X", hr);
        return false;
    }

    rootSignature_.SetRootSignature(tempRootSig.Get());

    // Update input layout to include texture coordinates
    std::vector<InputElementDesc> inputLayout = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    if (!pipelineState_.Initialize(
            &device_,
            &rootSignature_,
            &vertexShader_,
            &pixelShader_,
            inputLayout,
            DXGI_FORMAT_R8G8B8A8_UNORM)) {
        NEXT_LOG_ERROR("Failed to create PSO");
        return false;
    }

    // Vertex structure with texture coordinates
    struct Vertex {
        float position[3];
        float color[4];
        float texcoord[2];
    };

    // Cube vertices with texture coordinates
    Vertex vertices[NumCubeVertices] = {
        {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-1.0f, -1.0f,  1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f,  1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f,   1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}
    };

    if (!vertexBuffer_.Initialize(&device_, sizeof(vertices), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create vertex buffer");
        return false;
    }

    if (!vertexBuffer_.UploadData(vertices, sizeof(vertices))) {
        NEXT_LOG_ERROR("Failed to upload vertex data");
        return false;
    }

    UINT16 indices[NumCubeIndices] = {
        0, 1, 2, 0, 2, 3,
        5, 4, 6, 5, 6, 7,
        4, 1, 0, 4, 5, 1,
        3, 2,  6, 3, 6, 7,
        1, 5, 2, 5, 6, 2,
        4, 0, 3, 4, 3, 7
    };

    if (!indexBuffer_.Initialize(&device_, sizeof(indices), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create index buffer");
        return false;
    }

    if (!indexBuffer_.UploadData(indices, sizeof(indices))) {
        NEXT_LOG_ERROR("Failed to upload index data");
        return false;
    }

    if (!constantBuffer_.Initialize(&device_, sizeof(CubeConstants), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create constant buffer");
        return false;
    }

    // Initialize texture and sampler
    if (!texture_.Initialize(&device_, srvHeap_)) {
        NEXT_LOG_ERROR("Failed to initialize texture");
        return false;
    }

    if (!sampler_.Initialize(&device_, samplerHeap_)) {
        NEXT_LOG_ERROR("Failed to initialize sampler");
        return false;
    }

    // Create sampler
    DescriptorAllocation samplerAllocation = descriptorHeapManager_.Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1);
    if (samplerAllocation.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate sampler descriptor");
        return false;
    }

    sampler_.SetGPUDescriptorHandle(samplerAllocation.gpuHandle);
    if (!sampler_.Create(samplerAllocation.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to create sampler");
        return false;
    }

    // Load texture (will create checkerboard if file not found)
    pbrTextureAllocation_ = descriptorHeapManager_.Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kPbrTextureSlots);
    if (pbrTextureAllocation_.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate texture descriptors");
        return false;
    }

    bool textureLoaded = texture_.LoadFromFile(
        L"engine/renderer/textures/checkerboard.png",
        commandQueue_.GetQueue(),
        pbrTextureAllocation_.cpuHandle);

    if (!textureLoaded) {
        NEXT_LOG_WARNING("Failed to load texture file, will need to create one");
    } else {
        UINT descriptorSize = device_.GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (UINT i = 1; i < kPbrTextureSlots; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = pbrTextureAllocation_.cpuHandle;
            dstHandle.ptr += static_cast<SIZE_T>(i) * descriptorSize;
            device_.GetDevice()->CopyDescriptorsSimple(
                1,
                dstHandle,
                pbrTextureAllocation_.cpuHandle,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    NEXT_LOG_INFO("Pipeline resources created successfully");
    return true;
}

void DX12Renderer::UpdateConstantBuffer(float time) {
    // Calculate MVP matrix
    Mat4 model = Mat4::RotateX(time * 0.5f) * Mat4::RotateY(time * 0.7f);

    Vec3 eye(0.0f, 0.0f, -5.0f);
    Vec3 target(0.0f, 0.0f, 0.0f);
    Vec3 up(0.0f, 1.0f, 0.0f);
    Mat4 view = Mat4::LookAt(eye, target, up);

    float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    Mat4 projection = Mat4::Perspective(3.14159f / 4.0f, aspect, 0.1f, 100.0f);

    CubeConstants constants;
    constants.model = model.Transpose();
    constants.view = view.Transpose();
    constants.projection = projection.Transpose();
    constants.time = time;
    constants.padding[0] = 0.0f;
    constants.padding[1] = 0.0f;
    constants.padding[2] = 0.0f;

    constantBuffer_.UploadData(&constants, sizeof(CubeConstants));
}

void DX12Renderer::RenderCube() {
    // Set root signature
    commandList_.SetGraphicsRootSignature(rootSignature_.GetRootSignature());

    // Set constant buffer (root parameter 0)
    commandList_.GetCommandList()->SetGraphicsRootConstantBufferView(0, constantBuffer_.GetGPUVirtualAddress());

    // Set texture descriptor table (root parameter 1)
    commandList_.GetCommandList()->SetGraphicsRootDescriptorTable(1, texture_.GetGPUDescriptorHandle());

    // Set sampler descriptor table (root parameter 2)
    commandList_.GetCommandList()->SetGraphicsRootDescriptorTable(2, sampler_.GetGPUDescriptorHandle());

    // Set pipeline state
    commandList_.SetPipelineState(pipelineState_.GetPSO());

    // Set viewport and scissor rect
    commandList_.RSSetViewports(1, &viewport_);
    commandList_.RSSetScissorRects(1, &scissorRect_);

    // Set primitive topology
    commandList_.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set vertex and index buffers
    D3D12_VERTEX_BUFFER_VIEW vbv = vertexBuffer_.GetVertexBufferView(sizeof(float) * 9);  // 3 pos + 4 color + 2 texcoord
    commandList_.IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv = indexBuffer_.GetIndexBufferView(DXGI_FORMAT_R16_UINT);
    commandList_.IASetIndexBuffer(&ibv, DXGI_FORMAT_R16_UINT);

    // Draw indexed cube
    commandList_.DrawIndexedInstanced(NumCubeIndices, 1, 0, 0, 0);
}

bool DX12Renderer::CreatePBRResources() {
    NEXT_LOG_INFO("Creating PBR rendering resources...");

    // Initialize PBR material
    if (!pbrMaterial_.Initialize(&device_, &descriptorHeapManager_)) {
        NEXT_LOG_ERROR("Failed to initialize PBR material");
        return false;
    }

    // Set default PBR material properties
    pbrMaterial_.SetAlbedo(Vec3(0.8f, 0.2f, 0.2f));  // Red color
    pbrMaterial_.SetMetallic(0.5f);
    pbrMaterial_.SetRoughness(0.3f);
    pbrMaterial_.SetAO(1.0f);

    // Setup default lighting scene
    lightingScene_.camera.position = Vec3(0.0f, 0.0f, -5.0f);
    lightingScene_.settings.ambientColor = Vec3(0.1f, 0.1f, 0.1f);
    lightingScene_.settings.ambientIntensity = 0.1f;
    lightingScene_.settings.exposure = 1.0f;
    lightingScene_.settings.gamma = 2.2f;
    lightingScene_.settings.toneMapMode = 1;  // ACES

    // Add a point light for dynamic lighting
    PointLight pointLight;
    pointLight.position = Vec3(2.0f, 2.0f, -3.0f);
    pointLight.intensity = 2.0f;
    pointLight.color = Vec3(1.0f, 0.9f, 0.7f);  // Warm light
    pointLight.radius = 10.0f;
    pointLight.enabled = 1;
    lightingScene_.AddPointLight(pointLight);

    // Load PBR shaders (use simplified version for debugging)
    if (!pbrVertexShader_.LoadFromFile(&device_, "engine/renderer/shaders/pbr.vs.hlsl")) {
        NEXT_LOG_ERROR("Failed to load PBR vertex shader");
        return false;
    }

    if (!pbrPixelShader_.LoadFromFile(&device_, "engine/renderer/shaders/pbr.ps.hlsl")) {
        NEXT_LOG_ERROR("Failed to load PBR pixel shader");
        return false;
    }

    // Create PBR root signature with CBVs + texture/sampler tables
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    D3D12_ROOT_PARAMETER rootParameters[5] = {};
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    D3D12_DESCRIPTOR_RANGE samplerRange = {};

    // Parameter 0: MVP matrix CBV
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 1: Material CBV
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].Descriptor.ShaderRegister = 0;
    rootParameters[1].Descriptor.RegisterSpace = 1;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 2: Lighting CBV
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 1;
    rootParameters[2].Descriptor.RegisterSpace = 1;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 3: SRV descriptor table (t0-t4, space1)
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = kPbrTextureSlots;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 1;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 4: Sampler descriptor table (s0, space1)
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = 1;
    samplerRange.BaseShaderRegister = 0;
    samplerRange.RegisterSpace = 1;
    samplerRange.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges = &samplerRange;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootSignatureDesc.NumParameters = 5;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error
    );

    if (FAILED(hr)) {
        if (error) {
            NEXT_LOG_ERROR("PBR root signature serialization failed: %s", (const char*)error->GetBufferPointer());
        } else {
            NEXT_LOG_ERROR("PBR root signature serialization failed: 0x%X", hr);
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12RootSignature> tempRootSig;
    hr = device_.GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&tempRootSig)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create PBR root signature: 0x%X", hr);
        return false;
    }

    pbrRootSignature_.SetRootSignature(tempRootSig.Get());

    // Input layout for PBR (simplified: position, normal, texcoord)
    std::vector<InputElementDesc> inputLayout = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    // Debug: Log PSO creation parameters
    NEXT_LOG_DEBUG("Creating PBR PSO with:");
    NEXT_LOG_DEBUG("  - Input Layout elements: %zu", inputLayout.size());
    NEXT_LOG_DEBUG("  - Render Target Format: DXGI_FORMAT_R8G8B8A8_UNORM");
    NEXT_LOG_DEBUG("  - Root Signature: 0x%p", pbrRootSignature_.GetRootSignature());

    if (!pbrPipelineState_.Initialize(
            &device_,
            &pbrRootSignature_,
            &pbrVertexShader_,
            &pbrPixelShader_,
            inputLayout,
            DXGI_FORMAT_R8G8B8A8_UNORM)) {
        NEXT_LOG_ERROR("Failed to create PBR PSO: 0x%X (E_INVALIDARG - Invalid parameter)");

        // Try to validate Root Signature
        ID3D12RootSignature* rs = pbrRootSignature_.GetRootSignature();
        if (!rs) {
            NEXT_LOG_ERROR("  Root Signature is NULL!");
        } else {
            NEXT_LOG_DEBUG("  Root Signature pointer is valid");
        }

        return false;
    }

    // PBR Vertex structure (simplified: position, normal, texcoord)
    struct PBRVertex {
        float position[3];
        float normal[3];
        float texcoord[2];
    };

    // Cube vertices with normals for PBR
    PBRVertex vertices[NumCubeVertices] = {
        // Front face
        {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        // Back face
        {{-1.0f, -1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f,   1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}}
    };

    if (!pbrVertexBuffer_.Initialize(&device_, sizeof(vertices), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create PBR vertex buffer");
        return false;
    }

    if (!pbrVertexBuffer_.UploadData(vertices, sizeof(vertices))) {
        NEXT_LOG_ERROR("Failed to upload PBR vertex data");
        return false;
    }

    UINT16 indices[NumCubeIndices] = {
        0, 1, 2, 0, 2, 3,
        5, 4, 6, 5, 6, 7,
        4, 1, 0, 4, 5, 1,
        3, 2,  6, 3, 6, 7,
        1, 5, 2, 5, 6, 2,
        4, 0, 3, 4, 3, 7
    };

    if (!pbrIndexBuffer_.Initialize(&device_, sizeof(indices), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create PBR index buffer");
        return false;
    }

    if (!pbrIndexBuffer_.UploadData(indices, sizeof(indices))) {
        NEXT_LOG_ERROR("Failed to upload PBR index data");
        return false;
    }

    // Create MVP constant buffer for PBR
    if (!pbrMVPBuffer_.Initialize(&device_, sizeof(Mat4) * 2)) {  // MVP + Model matrix
        NEXT_LOG_ERROR("Failed to create PBR MVP buffer");
        return false;
    }

    // Create material constant buffer
    if (!pbrMaterialBuffer_.Initialize(&device_, sizeof(PBRMaterial))) {
        NEXT_LOG_ERROR("Failed to create PBR material buffer");
        return false;
    }

    // Create lighting constant buffer (GPU-aligned layout)
    if (!pbrLightingBuffer_.Initialize(&device_, sizeof(LightingBufferGPU))) {
        NEXT_LOG_ERROR("Failed to create PBR lighting buffer");
        return false;
    }

    // Initialize PBR sampler
    if (!pbrSampler_.Initialize(&device_, samplerHeap_)) {
        NEXT_LOG_ERROR("Failed to initialize PBR sampler");
        return false;
    }

    DescriptorAllocation pbrSamplerAllocation = descriptorHeapManager_.Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1);
    if (pbrSamplerAllocation.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate PBR sampler descriptor");
        return false;
    }

    pbrSampler_.SetGPUDescriptorHandle(pbrSamplerAllocation.gpuHandle);
    if (!pbrSampler_.Create(pbrSamplerAllocation.cpuHandle)) {
        NEXT_LOG_ERROR("Failed to create PBR sampler");
        return false;
    }

    // Enable albedo map sampling if the default texture loaded
    if (texture_.GetResource()) {
        pbrMaterial_.GetMaterialData().setUseAlbedoMap(true);
    }

    // Create indirect command signature for GPU-driven path
    D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
    argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
    sigDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    sigDesc.NumArgumentDescs = 1;
    sigDesc.pArgumentDescs = &argDesc;

    hr = device_.GetDevice()->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&pbrIndirectSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create indirect command signature: 0x%X", hr);
        return false;
    }

    if (!pbrIndirectArgs_.Initialize(&device_, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create indirect args buffer");
        return false;
    }

    NEXT_LOG_INFO("PBR rendering resources created successfully");
    return true;
}

void DX12Renderer::UpdateLightingBuffers() {
    // Update material buffer
    pbrMaterialBuffer_.UpdateData(&pbrMaterial_.GetMaterialData(), sizeof(PBRMaterial));

    LightingBufferGPU lighting = {};
    lighting.camera.position[0] = lightingScene_.camera.position.x;
    lighting.camera.position[1] = lightingScene_.camera.position.y;
    lighting.camera.position[2] = lightingScene_.camera.position.z;

    lighting.settings.ambientColor[0] = lightingScene_.settings.ambientColor.x;
    lighting.settings.ambientColor[1] = lightingScene_.settings.ambientColor.y;
    lighting.settings.ambientColor[2] = lightingScene_.settings.ambientColor.z;
    lighting.settings.ambientIntensity = lightingScene_.settings.ambientIntensity;
    lighting.settings.exposure = lightingScene_.settings.exposure;
    lighting.settings.gamma = lightingScene_.settings.gamma;
    lighting.settings.toneMapMode = lightingScene_.settings.toneMapMode;

    lighting.directionalLight.direction[0] = lightingScene_.directionalLight.direction.x;
    lighting.directionalLight.direction[1] = lightingScene_.directionalLight.direction.y;
    lighting.directionalLight.direction[2] = lightingScene_.directionalLight.direction.z;
    lighting.directionalLight.intensity = lightingScene_.directionalLight.intensity;
    lighting.directionalLight.color[0] = lightingScene_.directionalLight.color.x;
    lighting.directionalLight.color[1] = lightingScene_.directionalLight.color.y;
    lighting.directionalLight.color[2] = lightingScene_.directionalLight.color.z;
    lighting.directionalLight.enabled = lightingScene_.directionalLight.enabled;

    for (size_t i = 0; i < LightingScene::MAX_POINT_LIGHTS; i++) {
        PointLightGPU& dst = lighting.pointLights[i];
        if (i < lightingScene_.pointLights.size()) {
            const PointLight& src = lightingScene_.pointLights[i];
            dst.position[0] = src.position.x;
            dst.position[1] = src.position.y;
            dst.position[2] = src.position.z;
            dst.intensity = src.intensity;
            dst.color[0] = src.color.x;
            dst.color[1] = src.color.y;
            dst.color[2] = src.color.z;
            dst.radius = src.radius;
            dst.constant = src.constant;
            dst.linearAttenuation = src.linear;
            dst.quadratic = src.quadratic;
            dst.enabled = src.enabled;
        } else {
            dst.enabled = 0;
        }
    }

    int pointCount = static_cast<int>(lightingScene_.pointLights.size());
    if (pointCount > static_cast<int>(LightingScene::MAX_POINT_LIGHTS)) {
        pointCount = static_cast<int>(LightingScene::MAX_POINT_LIGHTS);
    }
    lighting.numPointLights = pointCount;
    lighting.numSpotLights = static_cast<int>(lightingScene_.spotLights.size());

    pbrLightingBuffer_.UpdateData(&lighting, sizeof(lighting));
}

void DX12Renderer::RenderPBRCube() {
    // Calculate MVP matrix
    Mat4 model = Mat4::RotateX(time_ * 0.5f) * Mat4::RotateY(time_ * 0.7f);

    Vec3 eye(0.0f, 0.0f, -5.0f);
    Vec3 target(0.0f, 0.0f, 0.0f);
    Vec3 up(0.0f, 1.0f, 0.0f);
    Mat4 view = Mat4::LookAt(eye, target, up);

    float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    Mat4 projection = Mat4::Perspective(3.14159f / 4.0f, aspect, 0.1f, 100.0f);

    Mat4 mvp = projection * view * model;
    Mat4 mvpTransposed = mvp.Transpose();
    Mat4 modelTransposed = model.Transpose();

    // Upload MVP and Model matrices
    Mat4 matrices[2] = {mvpTransposed, modelTransposed};
    pbrMVPBuffer_.UpdateData(matrices, sizeof(matrices));

    // Set root signature
    commandList_.SetGraphicsRootSignature(pbrRootSignature_.GetRootSignature());

    // Set constant buffers
    commandList_.GetCommandList()->SetGraphicsRootConstantBufferView(0, pbrMVPBuffer_.GetGPUVirtualAddress());
    commandList_.GetCommandList()->SetGraphicsRootConstantBufferView(1, pbrMaterialBuffer_.GetGPUVirtualAddress());
    commandList_.GetCommandList()->SetGraphicsRootConstantBufferView(2, pbrLightingBuffer_.GetGPUVirtualAddress());
    commandList_.GetCommandList()->SetGraphicsRootDescriptorTable(3, pbrTextureAllocation_.gpuHandle);
    commandList_.GetCommandList()->SetGraphicsRootDescriptorTable(4, pbrSampler_.GetGPUDescriptorHandle());

    // Set pipeline state
    commandList_.SetPipelineState(pbrPipelineState_.GetPSO());

    // Set viewport and scissor rect
    commandList_.RSSetViewports(1, &viewport_);
    commandList_.RSSetScissorRects(1, &scissorRect_);

    // Set primitive topology
    commandList_.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set vertex and index buffers (32 bytes per vertex: 3+3+2 floats * 4 bytes)
    D3D12_VERTEX_BUFFER_VIEW vbv = pbrVertexBuffer_.GetVertexBufferView(sizeof(float) * 8);
    commandList_.IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv = pbrIndexBuffer_.GetIndexBufferView(DXGI_FORMAT_R16_UINT);
    commandList_.IASetIndexBuffer(&ibv, DXGI_FORMAT_R16_UINT);

    if (useGpuDriven_ && pbrIndirectSignature_ && pbrIndirectArgs_.GetResource()) {
        D3D12_DRAW_INDEXED_ARGUMENTS args = {};
        args.IndexCountPerInstance = NumCubeIndices;
        args.InstanceCount = 1;
        args.StartIndexLocation = 0;
        args.BaseVertexLocation = 0;
        args.StartInstanceLocation = 0;
        pbrIndirectArgs_.UploadData(&args, sizeof(args));

        commandList_.ExecuteIndirect(
            pbrIndirectSignature_.Get(),
            1,
            pbrIndirectArgs_.GetResource(),
            0);
    } else {
        // Draw indexed cube
        commandList_.DrawIndexedInstanced(NumCubeIndices, 1, 0, 0, 0);
    }
}

} // namespace Next
