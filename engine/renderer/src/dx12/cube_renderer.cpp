#include "next/renderer/dx12/cube_renderer.h"
#include "next/foundation/logger.h"
#include <cmath>

namespace Next {

// Cube vertices (8 corners) - position + color
static const CubeRenderer::CubeVertex CUBE_VERTICES[8] = {
    // Front face
    {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 1.0f}},  // 0: Blue
    {{-1.0f,  1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},  // 1: Green
    {{ 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f, 0.0f}},  // 2: Yellow
    {{ 1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}},  // 3: Red
    // Back face
    {{-1.0f, -1.0f,  1.0f}, {1.0f, 0.0f, 1.0f}},  // 4: Magenta
    {{-1.0f,  1.0f,  1.0f}, {0.0f, 1.0f, 1.0f}},  // 5: Cyan
    {{ 1.0f,  1.0f,  1.0f}, {1.0f, 1.0f, 1.0f}},  // 6: White
    {{ 1.0f, -1.0f,  1.0f}, {0.5f, 0.0f, 0.5f}}   // 7: Purple
};

// Cube indices (36 indices = 12 triangles)
static const uint16_t CUBE_INDICES[36] = {
    // Front face
    0, 1, 2, 0, 2, 3,
    // Back face
    5, 4, 6, 5, 6, 7,
    // Left face
    4, 1, 0, 4, 5, 1,
    // Right face
    3, 2, 6, 3, 6, 7,
    // Top face
    1, 5, 2, 5, 6, 2,
    // Bottom face
    4, 0, 3, 4, 3, 7
};

CubeRenderer::CubeRenderer()
    : device_(nullptr)
    , dsvHeap_(nullptr)
    , width_(0)
    , height_(0)
    , initialized_(false) {
}

CubeRenderer::~CubeRenderer() {
    Shutdown();
}

bool CubeRenderer::Initialize(DX12Device* device, DX12DescriptorHeap* dsvHeap,
                               uint32_t width, uint32_t height) {
    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for cube renderer");
        return false;
    }

    NEXT_LOG_INFO("Initializing CubeRenderer: %ux%u", width, height);

    device_ = device;
    dsvHeap_ = dsvHeap;
    width_ = width;
    height_ = height;

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

    // Create cube geometry
    if (!CreateCubeGeometry()) {
        NEXT_LOG_ERROR("Failed to create cube geometry");
        return false;
    }

    // Create depth buffer
    if (!depthBuffer_.Initialize(device_, dsvHeap_, width_, height_)) {
        NEXT_LOG_ERROR("Failed to create depth buffer");
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("CubeRenderer initialized successfully (Phase 3: 3D Cube with MVP)");
    return true;
}

bool CubeRenderer::CreateCubeGeometry() {
    NEXT_LOG_INFO("Creating cube geometry...");

    // Create vertex buffer
    if (!vertexBuffer_.Initialize(device_, sizeof(CUBE_VERTICES), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create vertex buffer");
        return false;
    }

    if (!vertexBuffer_.UploadData(CUBE_VERTICES, sizeof(CUBE_VERTICES))) {
        NEXT_LOG_ERROR("Failed to upload vertex data");
        return false;
    }

    NEXT_LOG_DEBUG("Vertex buffer: %zu bytes", sizeof(CUBE_VERTICES));

    // Create index buffer
    if (!indexBuffer_.Initialize(device_, sizeof(CUBE_INDICES), D3D12_HEAP_TYPE_UPLOAD)) {
        NEXT_LOG_ERROR("Failed to create index buffer");
        return false;
    }

    if (!indexBuffer_.UploadData(CUBE_INDICES, sizeof(CUBE_INDICES))) {
        NEXT_LOG_ERROR("Failed to upload index data");
        return false;
    }

    NEXT_LOG_DEBUG("Index buffer: %zu bytes", sizeof(CUBE_INDICES));

    // Create constant buffer (MVP matrices)
    if (!constantBuffer_.Initialize(device_, sizeof(MVPConstants))) {
        NEXT_LOG_ERROR("Failed to create constant buffer");
        return false;
    }

    NEXT_LOG_INFO("Cube geometry created: %u vertices, %u indices",
                 NUM_VERTICES, NUM_INDICES);
    return true;
}

void CubeRenderer::UpdateMVPMatrix(double time) {
    // Model matrix: Rotate on all axes
    float rotationSpeed = 0.5f;
    Mat4 modelX = Mat4::RotateX(static_cast<float>(time) * rotationSpeed);
    Mat4 modelY = Mat4::RotateY(static_cast<float>(time) * rotationSpeed * 0.7f);
    Mat4 modelZ = Mat4::RotateZ(static_cast<float>(time) * rotationSpeed * 0.3f);
    Mat4 model = modelX * modelY * modelZ;

    // View matrix: Look at the cube from (0, 0, -5)
    Vec3 eye(0.0f, 0.0f, -5.0f);
    Vec3 target(0.0f, 0.0f, 0.0f);
    Vec3 up(0.0f, 1.0f, 0.0f);
    Mat4 view = Mat4::LookAt(eye, target, up);

    // Projection matrix: Perspective
    float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    float fov = 3.14159f / 4.0f;  // 45 degrees
    float nearZ = 0.1f;
    float farZ = 100.0f;
    Mat4 projection = Mat4::Perspective(fov, aspect, nearZ, farZ);

    // Pack into constant buffer structure
    MVPConstants constants;
    constants.modelMatrix = model;
    constants.viewMatrix = view;
    constants.projectionMatrix = projection;
    constants.time = static_cast<float>(time);

    // Transpose matrices for HLSL (row-major to column-major)
    constants.modelMatrix = constants.modelMatrix.Transpose();
    constants.viewMatrix = constants.viewMatrix.Transpose();
    constants.projectionMatrix = constants.projectionMatrix.Transpose();

    // Update constant buffer
    if (!constantBuffer_.UpdateData(&constants, sizeof(constants))) {
        NEXT_LOG_ERROR("Failed to update constant buffer");
    }
}

void CubeRenderer::Render(ID3D12GraphicsCommandList* commandList, double time) {
    if (!initialized_ || !commandList) {
        NEXT_LOG_ERROR("Cannot render: not initialized or invalid command list");
        return;
    }

    // Update MVP matrix
    UpdateMVPMatrix(time);

    // Clear depth buffer
    depthBuffer_.Clear(commandList, 1.0f, 0);

    // Note: Pipeline setup (root signature, shaders, etc.) should be done by caller
    // This allows for batch rendering and better resource management

    // Set constant buffer (will be bound by root signature)
    // commandList->SetGraphicsRootConstantBufferView(0, constantBuffer_.GetGPUVirtualAddress());

    // Set vertex buffer
    D3D12_VERTEX_BUFFER_VIEW vbv = vertexBuffer_.GetVertexBufferView(sizeof(CubeVertex));
    commandList->IASetVertexBuffers(0, 1, &vbv);

    // Set index buffer
    D3D12_INDEX_BUFFER_VIEW ibv = indexBuffer_.GetIndexBufferView(DXGI_FORMAT_R16_UINT);
    commandList->IASetIndexBuffer(&ibv);

    // Draw indexed cube
    commandList->DrawIndexedInstanced(NUM_INDICES, 1, 0, 0, 0);
}

bool CubeRenderer::Resize(uint32_t width, uint32_t height) {
    if (!initialized_) {
        NEXT_LOG_ERROR("Cannot resize uninitialized cube renderer");
        return false;
    }

    NEXT_LOG_INFO("Resizing CubeRenderer: %ux%u -> %ux%u", width_, height_, width, height);

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

void CubeRenderer::Shutdown() {
    depthBuffer_.Shutdown();
    constantBuffer_.Shutdown();
    indexBuffer_.Shutdown();
    vertexBuffer_.Shutdown();

    device_ = nullptr;
    dsvHeap_ = nullptr;
    width_ = 0;
    height_ = 0;
    initialized_ = false;

    NEXT_LOG_INFO("CubeRenderer shutdown complete");
}

} // namespace Next
