#pragma once

#include "next/renderer/dx12/device.h"
#include <d3d12.h>
#include <wrl/client.h>

namespace Next {

// Forward declarations
class DX12Device;

// Debug Visualization Modes (RAGE/UE5 Style)
enum class DebugViewMode {
    Default = 0,         // Normal rendering
    Wireframe = 1,       // Wireframe overlay
    Normals = 2,         // World-space normals
    Tangents = 3,        // Tangent space
    Bitangents = 4,      // Bitangent space
    Depth = 5,           // Linear depth
    Roughness = 6,       // Roughness visualization
    Metallic = 7,        // Metallic visualization
    Albedo = 8,          // Albedo visualization
    AO = 9,              // Ambient occlusion
    MotionVectors = 10,  // Motion vectors
    UV = 11,             // Texture coordinates
    TriangleCount = 12,  // Overdraw/triangle count
    Heatmap = 13,        // Generic performance density heatmap
};

// Debug Views (UE5/RAGE Style Visualization)
// Design principles:
// - Sustainable Experimental: Easy to add new debug modes
// - Advanced: Full-featured visualization (heatmaps, overlays)
// - Refactor Friendly: Self-contained debug system
class DebugViews {
public:
    DebugViews();
    ~DebugViews();

    // Initialize debug views
    bool Initialize(DX12Device* device);

    // Set debug mode
    void SetDebugMode(DebugViewMode mode) { debugMode_ = mode; }
    DebugViewMode GetDebugMode() const { return debugMode_; }

    // Toggle debug mode
    void ToggleDebugMode() {
        int mode = static_cast<int>(debugMode_) + 1;
        if (mode > static_cast<int>(DebugViewMode::Heatmap)) {
            mode = static_cast<int>(DebugViewMode::Default);
        }
        debugMode_ = static_cast<DebugViewMode>(mode);
    }

    // Render debug overlay
    void RenderDebugOverlay(ID3D12GraphicsCommandList* commandList,
                            D3D12_CPU_DESCRIPTOR_HANDLE outputRTV,
                            uint32_t width,
                            uint32_t height);

    // Visualize specific component
    void RenderWireframe(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE outputRTV);
    void RenderNormals(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE outputRTV);
    void RenderDepth(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE outputRTV);
    void RenderHeatmap(ID3D12GraphicsCommandList* commandList,
                       D3D12_CPU_DESCRIPTOR_HANDLE outputRTV,
                       const char* label);

    // Cleanup
    void Shutdown();

    bool IsInitialized() const { return initialized_; }

private:
    // Device
    DX12Device* device_;

    // Current debug mode
    DebugViewMode debugMode_;

    // Debug visualization resources
    Microsoft::WRL::ComPtr<ID3D12Resource> debugTexture_;

    void ClearDebugOutput(ID3D12GraphicsCommandList* commandList,
                          D3D12_CPU_DESCRIPTOR_HANDLE outputRTV,
                          const float color[4]);

    bool initialized_;
};

} // namespace Next
