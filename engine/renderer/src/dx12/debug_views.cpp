#include "next/renderer/dx12/debug_views.h"
#include "next/foundation/logger.h"

namespace Next {

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

void DebugViews::RenderDebugOverlay(ID3D12GraphicsCommandList* commandList) {
    if (!initialized_ || !commandList) {
        NEXT_LOG_ERROR("Cannot render debug overlay: not initialized or invalid command list");
        return;
    }

    // Render based on current debug mode
    switch (debugMode_) {
        case DebugViewMode::Wireframe:
            RenderWireframe(commandList);
            break;
        case DebugViewMode::Normals:
            RenderNormals(commandList);
            break;
        case DebugViewMode::Depth:
            RenderDepth(commandList);
            break;
        default:
            // Default rendering - no debug overlay
            break;
    }

    NEXT_LOG_DEBUG("Debug overlay rendered: mode=%d", static_cast<int>(debugMode_));
}

void DebugViews::RenderWireframe(ID3D12GraphicsCommandList* commandList) {
    // Placeholder for wireframe rendering implementation
    // Full implementation requires: wireframe PSO, rasterizer state
    NEXT_LOG_DEBUG("Wireframe rendering enabled (placeholder)");
}

void DebugViews::RenderNormals(ID3D12GraphicsCommandList* commandList) {
    // Placeholder for normal visualization implementation
    // Full implementation requires: normal shader, constant buffer
    NEXT_LOG_DEBUG("Normal visualization enabled (placeholder)");
}

void DebugViews::RenderDepth(ID3D12GraphicsCommandList* commandList) {
    // Placeholder for depth visualization implementation
    // Full implementation requires: depth shader, depth buffer SRV
    NEXT_LOG_DEBUG("Depth visualization enabled (placeholder)");
}

void DebugViews::RenderHeatmap(ID3D12GraphicsCommandList* commandList, const char* label) {
    // TODO: Implement heatmap visualization
    // 1. Render value-based heatmap
    // 2. Add text label
    // 3. Use UE5-style color ramp

    NEXT_LOG_DEBUG("Heatmap rendered: %s", label);
}

void DebugViews::Shutdown() {
    debugTexture_.Reset();
    device_ = nullptr;
    debugMode_ = DebugViewMode::Default;
    initialized_ = false;

    NEXT_LOG_INFO("Debug views shutdown complete");
}

} // namespace Next
