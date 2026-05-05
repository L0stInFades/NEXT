#include "next/renderer/dx12/debug_views.h"
#include "next/foundation/logger.h"
#include <cstdint>

namespace Next {

namespace {

const char* DebugViewModeName(DebugViewMode mode) {
    switch (mode) {
        case DebugViewMode::Default: return "Default";
        case DebugViewMode::Wireframe: return "Wireframe";
        case DebugViewMode::Normals: return "Normals";
        case DebugViewMode::Tangents: return "Tangents";
        case DebugViewMode::Bitangents: return "Bitangents";
        case DebugViewMode::Depth: return "Depth";
        case DebugViewMode::Roughness: return "Roughness";
        case DebugViewMode::Metallic: return "Metallic";
        case DebugViewMode::Albedo: return "Albedo";
        case DebugViewMode::AO: return "AO";
        case DebugViewMode::MotionVectors: return "MotionVectors";
        case DebugViewMode::UV: return "UV";
        case DebugViewMode::TriangleCount: return "TriangleCount";
        case DebugViewMode::Heatmap: return "Heatmap";
    }
    return "Unknown";
}

} // namespace

DebugViews::DebugViews()
    : device_(nullptr)
    , debugMode_(DebugViewMode::Default)
    , initialized_(false) {
}

DebugViews::~DebugViews() {
    Shutdown();
}

bool DebugViews::Initialize(DX12Device* device) {
    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for debug views");
        return false;
    }

    device_ = device;
    debugMode_ = DebugViewMode::Default;

    initialized_ = true;
    NEXT_LOG_INFO("Debug views initialized (Phase 5: UE5/RAGE-style debugging)");
    return true;
}

void DebugViews::RenderDebugOverlay(ID3D12GraphicsCommandList* commandList,
                                    D3D12_CPU_DESCRIPTOR_HANDLE outputRTV,
                                    uint32_t width,
                                    uint32_t height) {
    if (!initialized_ || !commandList || outputRTV.ptr == 0 || width == 0 || height == 0) {
        NEXT_LOG_ERROR("Cannot render debug overlay: invalid state or output target");
        return;
    }

    switch (debugMode_) {
        case DebugViewMode::Default:
            break;
        case DebugViewMode::Wireframe:
            RenderWireframe(commandList, outputRTV);
            break;
        case DebugViewMode::Normals:
            RenderNormals(commandList, outputRTV);
            break;
        case DebugViewMode::Depth:
            RenderDepth(commandList, outputRTV);
            break;
        case DebugViewMode::Tangents:
        case DebugViewMode::Bitangents:
        case DebugViewMode::Roughness:
        case DebugViewMode::Metallic:
        case DebugViewMode::Albedo:
        case DebugViewMode::AO:
        case DebugViewMode::MotionVectors:
        case DebugViewMode::UV:
        case DebugViewMode::TriangleCount:
        case DebugViewMode::Heatmap:
            RenderHeatmap(commandList, outputRTV, DebugViewModeName(debugMode_));
            break;
        default:
            NEXT_LOG_WARNING("Unknown debug view mode: %d", static_cast<int>(debugMode_));
            break;
    }

    NEXT_LOG_DEBUG("Debug overlay processed: mode=%s", DebugViewModeName(debugMode_));
}

void DebugViews::RenderWireframe(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE outputRTV) {
    if (!commandList || outputRTV.ptr == 0) {
        return;
    }

    const float wireframeColor[4] = {0.05f, 0.85f, 0.30f, 1.0f};
    ClearDebugOutput(commandList, outputRTV, wireframeColor);
}

void DebugViews::RenderNormals(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE outputRTV) {
    if (!commandList || outputRTV.ptr == 0) {
        return;
    }

    const float normalColor[4] = {0.50f, 0.50f, 1.0f, 1.0f};
    ClearDebugOutput(commandList, outputRTV, normalColor);
}

void DebugViews::RenderDepth(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE outputRTV) {
    if (!commandList || outputRTV.ptr == 0) {
        return;
    }

    const float depthColor[4] = {0.18f, 0.18f, 0.18f, 1.0f};
    ClearDebugOutput(commandList, outputRTV, depthColor);
}

void DebugViews::RenderHeatmap(ID3D12GraphicsCommandList* commandList,
                               D3D12_CPU_DESCRIPTOR_HANDLE outputRTV,
                               const char* label) {
    if (!commandList || outputRTV.ptr == 0) {
        return;
    }

    const uint32_t hash = label ? static_cast<uint32_t>(label[0]) + static_cast<uint32_t>(label[1] ? label[1] : 0) : 0;
    const float heat = 0.35f + static_cast<float>(hash % 7) * 0.07f;
    const float heatmapColor[4] = {heat, 0.18f, 1.0f - heat * 0.5f, 1.0f};
    ClearDebugOutput(commandList, outputRTV, heatmapColor);
}

void DebugViews::ClearDebugOutput(ID3D12GraphicsCommandList* commandList,
                                  D3D12_CPU_DESCRIPTOR_HANDLE outputRTV,
                                  const float color[4]) {
    if (!commandList || outputRTV.ptr == 0 || !color) {
        return;
    }

    commandList->ClearRenderTargetView(outputRTV, color, 0, nullptr);
}

void DebugViews::Shutdown() {
    debugTexture_.Reset();
    device_ = nullptr;
    debugMode_ = DebugViewMode::Default;
    initialized_ = false;

    NEXT_LOG_INFO("Debug views shutdown complete");
}

} // namespace Next
