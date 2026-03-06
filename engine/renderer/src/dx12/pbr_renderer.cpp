#include "next/renderer/dx12/pbr_renderer.h"
#include "next/renderer/dx12/root_signature.h"
#include "next/renderer/dx12/pipeline_state.h"
#include "next/foundation/logger.h"
#include <cmath>

namespace Next {

namespace {
constexpr UINT kPbrTextureSlots = 5;

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

// Inline helper structures (replacing d3dx12.h)
struct CD3DX12_DEFAULT {};
extern const CD3DX12_DEFAULT D3D12_DEFAULT;

struct CD3DX12_BLEND_DESC : public D3D12_BLEND_DESC {
    CD3DX12_BLEND_DESC() {
        AlphaToCoverageEnable = FALSE;
        IndependentBlendEnable = FALSE;
        const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc = {
            FALSE, FALSE,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP,
            D3D12_COLOR_WRITE_ENABLE_ALL,
        };
        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            RenderTarget[i] = defaultRenderTargetBlendDesc;
    }
};

struct CD3DX12_RASTERIZER_DESC : public D3D12_RASTERIZER_DESC {
    CD3DX12_RASTERIZER_DESC() {
        FillMode = D3D12_FILL_MODE_SOLID;
        CullMode = D3D12_CULL_MODE_BACK;
        FrontCounterClockwise = FALSE;
        DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        DepthClipEnable = TRUE;
        MultisampleEnable = FALSE;
        AntialiasedLineEnable = FALSE;
        ForcedSampleCount = 0;
        ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    }
};

struct CD3DX12_DEPTH_STENCIL_DESC : public D3D12_DEPTH_STENCIL_DESC {
    CD3DX12_DEPTH_STENCIL_DESC() {
        DepthEnable = TRUE;
        DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        StencilEnable = FALSE;
        StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp = {
            D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
            D3D12_COMPARISON_FUNC_ALWAYS
        };
        FrontFace = defaultStencilOp;
        BackFace = defaultStencilOp;
    }
};

PBRRenderer::PBRRenderer()
    : device_(nullptr)
    , heapManager_(nullptr)
    , srvHeap_(nullptr)
    , dsvHeap_(nullptr)
    , vertexShader_(nullptr)
    , pixelShader_(nullptr)
    , cameraPosition_(0.0f, 0.0f, -5.0f)
    , cameraTarget_(0.0f, 0.0f, 0.0f)
    , cameraUp_(0.0f, 1.0f, 0.0f)
    , width_(0)
    , height_(0)
    , numVertices_(0)
    , numIndices_(0)
    , initialized_(false) {
    srvTableHandle_.ptr = 0;
}

PBRRenderer::~PBRRenderer() {
    Shutdown();
}

bool PBRRenderer::Initialize(DX12Device* device, DX12DescriptorHeapManager* heapManager,
                              DX12DescriptorHeap* dsvHeap,
                              uint32_t width, uint32_t height) {
    if (!device || !heapManager || !dsvHeap) {
        NEXT_LOG_ERROR("Invalid parameters for PBR renderer");
        return false;
    }

    NEXT_LOG_INFO("Initializing PBR renderer: %ux%u", width, height);

    device_ = device;
    heapManager_ = heapManager;
    srvHeap_ = heapManager_->GetHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dsvHeap_ = dsvHeap;
    width_ = width;
    height_ = height;

    if (!srvHeap_) {
        NEXT_LOG_ERROR("No SRV heap available for PBR renderer");
        return false;
    }

    if (srvHeap_->GetNumDescriptors() < kPbrTextureSlots) {
        NEXT_LOG_ERROR("SRV heap too small for PBR renderer (%u required)", kPbrTextureSlots);
        return false;
    }

    // Setup viewport
    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(width);
    viewport_.Height = static_cast<float>(height);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissorRect_.left = 0;
    scissorRect_.top = 0;
    scissorRect_.right = static_cast<LONG>(width);
    scissorRect_.bottom = static_cast<LONG>(height);

    // Create sphere geometry for PBR testing
    if (!CreateSphereGeometry()) {
        NEXT_LOG_ERROR("Failed to create sphere geometry");
        return false;
    }

    // Create depth buffer
    if (!depthBuffer_.Initialize(device_, dsvHeap_, width_, height_)) {
        NEXT_LOG_ERROR("Failed to create depth buffer");
        return false;
    }

    // Create constant buffers
    if (!transformBuffer_.Initialize(device_, sizeof(Mat4) * 2)) {
        NEXT_LOG_ERROR("Failed to create transform buffer");
        return false;
    }

    if (!materialBuffer_.Initialize(device_, sizeof(PBRMaterial))) {
        NEXT_LOG_ERROR("Failed to create material buffer");
        return false;
    }

    if (!lightingBuffer_.Initialize(device_, sizeof(LightingBufferGPU))) {
        NEXT_LOG_ERROR("Failed to create lighting buffer");
        return false;
    }

    srvAllocation_ = heapManager_->Allocate(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kPbrTextureSlots);
    if (srvAllocation_.count == 0) {
        NEXT_LOG_ERROR("Failed to allocate SRV table for PBR renderer");
        return false;
    }

    // Initialize SRV table with null descriptors (t0-t4, space1)
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvAllocation_.cpuHandle;
    UINT descriptorSize = device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (UINT i = 0; i < kPbrTextureSlots; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE dst = cpuHandle;
        dst.ptr += static_cast<SIZE_T>(i) * descriptorSize;
        device_->GetDevice()->CreateShaderResourceView(nullptr, nullptr, dst);
    }

    srvTableHandle_ = srvAllocation_.gpuHandle;

    // Create root signature
    if (!CreateRootSignature()) {
        NEXT_LOG_ERROR("Failed to create root signature");
        return false;
    }

    // Create pipeline state
    if (!CreatePipelineState()) {
        NEXT_LOG_ERROR("Failed to create pipeline state");
        return false;
    }

    // Initialize default lighting
    lightingScene_ = LightingScene();

    initialized_ = true;
    NEXT_LOG_INFO("PBR renderer initialized successfully (Phase 4: PBR Pipeline)");
    return true;
}

bool PBRRenderer::CreateSphereGeometry() {
    NEXT_LOG_INFO("Creating sphere geometry for PBR testing...");

    // Create a UV sphere
    const int latBands = 30;
    const int lonBands = 30;
    const float radius = 1.0f;

    std::vector<PBRVertex> vertices;
    std::vector<uint16_t> indices;

    // Generate vertices
    for (int lat = 0; lat <= latBands; lat++) {
        float theta = lat * 3.14159f / latBands;
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (int lon = 0; lon <= lonBands; lon++) {
            float phi = lon * 2.0f * 3.14159f / lonBands;
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            PBRVertex vertex;
            vertex.position[0] = cosPhi * sinTheta * radius;
            vertex.position[1] = cosTheta * radius;
            vertex.position[2] = sinPhi * sinTheta * radius;

            vertex.normal[0] = vertex.position[0] / radius;
            vertex.normal[1] = vertex.position[1] / radius;
            vertex.normal[2] = vertex.position[2] / radius;

            vertex.texCoord[0] = static_cast<float>(lon) / lonBands;
            vertex.texCoord[1] = static_cast<float>(lat) / latBands;

            // Calculate tangent (for normal mapping)
            Vec3 bitangent(0.0f, 1.0f, 0.0f);
            Vec3 normal(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
            Vec3 tangent = bitangent.Cross(normal).Normalize();

            vertex.tangent[0] = tangent.x;
            vertex.tangent[1] = tangent.y;
            vertex.tangent[2] = tangent.z;

            vertex.bitangent[0] = bitangent.x;
            vertex.bitangent[1] = bitangent.y;
            vertex.bitangent[2] = bitangent.z;

            vertices.push_back(vertex);
        }
    }

    // Generate indices
    for (int lat = 0; lat < latBands; lat++) {
        for (int lon = 0; lon < lonBands; lon++) {
            int first = (lat * (lonBands + 1)) + lon;
            int second = first + lonBands + 1;

            indices.push_back(first);
            indices.push_back(second);
            indices.push_back(first + 1);

            indices.push_back(second);
            indices.push_back(second + 1);
            indices.push_back(first + 1);
        }
    }

    numVertices_ = static_cast<uint32_t>(vertices.size());
    numIndices_ = static_cast<uint32_t>(indices.size());

    // Create vertex buffer
    if (!vertexBuffer_.Initialize(device_, numVertices_ * sizeof(PBRVertex), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create vertex buffer");
        return false;
    }

    if (!vertexBuffer_.UploadData(vertices.data(), numVertices_ * sizeof(PBRVertex))) {
        NEXT_LOG_ERROR("Failed to upload vertex data");
        return false;
    }

    // Create index buffer
    if (!indexBuffer_.Initialize(device_, numIndices_ * sizeof(uint16_t), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create index buffer");
        return false;
    }

    if (!indexBuffer_.UploadData(indices.data(), numIndices_ * sizeof(uint16_t))) {
        NEXT_LOG_ERROR("Failed to upload index data");
        return false;
    }

    NEXT_LOG_INFO("Sphere geometry created: %u vertices, %u indices", numVertices_, numIndices_);
    return true;
}

bool PBRRenderer::CreateRootSignature() {
    // Root signature for PBR shaders with register spaces
    // Space 0 (Vertex Shader):
    //   b0: Transform buffer (Model, View, Projection)
    // Space 1 (Pixel Shader):
    //   b0: Material buffer
    //   b1: Lighting buffer

    D3D12_ROOT_PARAMETER rootParameters[4];
    D3D12_DESCRIPTOR_RANGE srvRange = {};

    // Parameter 0: Transform buffer (vertex shader, space0, b0)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;  // space0
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // Parameter 1: Material buffer (pixel shader, space1, b0)
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].Descriptor.ShaderRegister = 0;
    rootParameters[1].Descriptor.RegisterSpace = 1;  // space1
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 2: Lighting buffer (pixel shader, space1, b1)
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 1;
    rootParameters[2].Descriptor.RegisterSpace = 1;  // space1
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 3: SRV descriptor table (pixel shader, t0-t4, space1)
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = kPbrTextureSlots;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 1;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 4;
    rootSignatureDesc.pParameters = rootParameters;
    D3D12_STATIC_SAMPLER_DESC staticSampler = {};
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.MipLODBias = 0.0f;
    staticSampler.MaxAnisotropy = 1;
    staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSampler.MinLOD = 0.0f;
    staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
    staticSampler.ShaderRegister = 0;
    staticSampler.RegisterSpace = 1;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = &staticSampler;
    rootSignatureDesc.Flags = rootSignatureFlags;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to serialize root signature: 0x%X", hr);
        if (error) {
            NEXT_LOG_ERROR("Root signature error: %s", error->GetBufferPointer());
        }
        return false;
    }

    hr = device_->GetDevice()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create root signature: 0x%X", hr);
        return false;
    }

    NEXT_LOG_INFO("PBR Root signature created successfully (VS: space0/b0, PS: space1/b0,b1)");
    return true;
}

bool PBRRenderer::CreatePipelineState() {
    // Load shaders
    vertexShader_ = new DX12Shader();
    if (!vertexShader_->InitializeFromFile(device_, "engine/renderer/shaders/pbr.vs.hlsl", "main", "vs_5_1")) {
        NEXT_LOG_ERROR("Failed to load vertex shader");
        return false;
    }

    pixelShader_ = new DX12Shader();
    if (!pixelShader_->InitializeFromFile(device_, "engine/renderer/shaders/pbr.ps.hlsl", "main", "ps_5_1")) {
        NEXT_LOG_ERROR("Failed to load pixel shader");
        return false;
    }

    // Define input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    // Create pipeline state
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.VS = vertexShader_->GetBytecode();
    psoDesc.PS = pixelShader_->GetBytecode();
    psoDesc.BlendState = CD3DX12_BLEND_DESC();
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC();
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC();
    psoDesc.InputLayout = {inputLayout, ARRAYSIZE(inputLayout)};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    HRESULT hr = device_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create pipeline state: 0x%X", hr);
        return false;
    }

    NEXT_LOG_INFO("Pipeline state created successfully");
    return true;
}

PBRMaterialAsset* PBRRenderer::CreateMaterial() {
    PBRMaterialAsset* material = new PBRMaterialAsset();
    if (!material->Initialize(device_, srvHeap_)) {
        delete material;
        return nullptr;
    }

    materials_.push_back(material);
    return material;
}

void PBRRenderer::Render(ID3D12GraphicsCommandList* commandList, double time) {
    if (!initialized_ || !commandList) {
        NEXT_LOG_ERROR("Cannot render: not initialized or invalid command list");
        return;
    }

    // Update transforms
    UpdateTransforms(time);

    // Update lighting
    UpdateLightingBuffers();

    // Clear depth buffer
    depthBuffer_.Clear(commandList, 1.0f, 0);

    // Set pipeline state
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->SetGraphicsRootSignature(rootSignature_.Get());

    // Set root parameters
    commandList->SetGraphicsRootConstantBufferView(0, transformBuffer_.GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, materialBuffer_.GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, lightingBuffer_.GetGPUVirtualAddress());
    if (srvTableHandle_.ptr != 0) {
        commandList->SetGraphicsRootDescriptorTable(3, srvTableHandle_);
    }

    // Set viewport and scissor
    commandList->RSSetViewports(1, &viewport_);
    commandList->RSSetScissorRects(1, &scissorRect_);

    // Set vertex buffer
    D3D12_VERTEX_BUFFER_VIEW vbv = vertexBuffer_.GetVertexBufferView(sizeof(PBRVertex));
    commandList->IASetVertexBuffers(0, 1, &vbv);

    // Set index buffer
    D3D12_INDEX_BUFFER_VIEW ibv = indexBuffer_.GetIndexBufferView(DXGI_FORMAT_R16_UINT);
    commandList->IASetIndexBuffer(&ibv);

    // Set primitive topology
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Draw sphere
    commandList->DrawIndexedInstanced(numIndices_, 1, 0, 0, 0);
}

void PBRRenderer::UpdateTransforms(double time) {
    // Model matrix: Rotate sphere
    Mat4 model = Mat4::RotateY(static_cast<float>(time) * 0.3f);

    // View matrix: Look at sphere
    Mat4 view = Mat4::LookAt(cameraPosition_, cameraTarget_, cameraUp_);

    // Projection matrix: Perspective
    float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    float fov = 3.14159f / 4.0f;  // 45 degrees
    float nearZ = 0.1f;
    float farZ = 100.0f;
    Mat4 projection = Mat4::Perspective(fov, aspect, nearZ, farZ);

    Mat4 mvp = projection * view * model;

    struct TransformData {
        Mat4 mvp;
        Mat4 model;
    };

    TransformData transforms;
    transforms.mvp = mvp.Transpose();
    transforms.model = model.Transpose();

    // Update constant buffer
    if (!transformBuffer_.UpdateData(&transforms, sizeof(transforms))) {
        NEXT_LOG_ERROR("Failed to update transform buffer");
    }
}

void PBRRenderer::UpdateLightingBuffers() {
    // Update camera data
    lightingScene_.camera.position = cameraPosition_;

    // Update material buffer (default material)
    PBRMaterial defaultMaterial;
    defaultMaterial.albedo = Vec3(1.0f, 0.0f, 0.0f);  // Red color
    defaultMaterial.metallic = 0.5f;
    defaultMaterial.roughnessAndAO = Vec3(0.3f, 1.0f, 0.0f);  // roughness=0.3, ao=1.0

    if (!materialBuffer_.UpdateData(&defaultMaterial, sizeof(defaultMaterial))) {
        NEXT_LOG_ERROR("Failed to update material buffer");
    }

    LightingBufferGPU lighting = {};
    lighting.camera.position[0] = cameraPosition_.x;
    lighting.camera.position[1] = cameraPosition_.y;
    lighting.camera.position[2] = cameraPosition_.z;

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

    if (!lightingBuffer_.UpdateData(&lighting, sizeof(lighting))) {
        NEXT_LOG_ERROR("Failed to update lighting buffer");
    }
}

void PBRRenderer::UpdateLighting(const LightingScene& scene) {
    lightingScene_ = scene;
}

bool PBRRenderer::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot resize uninitialized PBR renderer");
        return false;
    }

    NEXT_LOG_INFO("Resizing PBR renderer: %ux%u -> %ux%u", width_, height_, width, height);

    width_ = width;
    height_ = height;

    // Update viewport
    viewport_.Width = static_cast<float>(width);
    viewport_.Height = static_cast<float>(height);

    scissorRect_.right = static_cast<LONG>(width);
    scissorRect_.bottom = static_cast<LONG>(height);

    // Resize depth buffer
    return depthBuffer_.Resize(width, height);
}

void PBRRenderer::SetCameraPosition(const Vec3& pos) {
    cameraPosition_ = pos;
}

void PBRRenderer::SetCameraTarget(const Vec3& target) {
    cameraTarget_ = target;
}

void PBRRenderer::SetCameraUp(const Vec3& up) {
    cameraUp_ = up;
}

void PBRRenderer::Shutdown() {
    if (vertexShader_) {
        vertexShader_->Shutdown();
        delete vertexShader_;
        vertexShader_ = nullptr;
    }

    if (pixelShader_) {
        pixelShader_->Shutdown();
        delete pixelShader_;
        pixelShader_ = nullptr;
    }

    for (auto* material : materials_) {
        if (material) {
            material->Shutdown();
            delete material;
        }
    }
    materials_.clear();

    depthBuffer_.Shutdown();
    lightingBuffer_.Shutdown();
    materialBuffer_.Shutdown();
    transformBuffer_.Shutdown();
    indexBuffer_.Shutdown();
    vertexBuffer_.Shutdown();

    rootSignature_.Reset();
    pipelineState_.Reset();

    if (heapManager_ && srvAllocation_.count > 0) {
        heapManager_->Release(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srvAllocation_);
    }
    srvAllocation_ = DescriptorAllocation();

    device_ = nullptr;
    heapManager_ = nullptr;
    srvHeap_ = nullptr;
    dsvHeap_ = nullptr;

    width_ = 0;
    height_ = 0;
    initialized_ = false;

    NEXT_LOG_INFO("PBR renderer shutdown complete");
}

} // namespace Next
