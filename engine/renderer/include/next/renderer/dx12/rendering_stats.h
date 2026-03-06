#pragma once

#include "next/renderer/dx12/device.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <map>
#include <chrono>

namespace Next {

// Forward declarations
enum class AOType : uint32_t;
enum class GITechnique : uint32_t;

//=============================================================================
// Rendering Statistics
//=============================================================================

struct RenderingStats {
    // Frame statistics
    uint32_t frameCount;
    float frameTime;          // ms
    float avgFrameTime;       // ms (last 60 frames)
    float minFrameTime;       // ms
    float maxFrameTime;       // ms

    // FPS
    float fps;
    float avgFps;

    // Draw calls
    uint32_t drawCalls;
    uint32_t pipelineStateChanges;
    uint32_t renderTargetBindings;

    // Geometry
    uint32_t verticesDrawn;
    uint32_t indicesDrawn;
    uint32_t primitivesDrawn;

    // AO Statistics
    struct AOStats {
        bool enabled;
        float aoTime;           // ms
        float avgAOTime;        // ms
        AOType aoType;          // GTAO, HBAO, VXAO, or None
        uint32_t resolutionX;
        uint32_t resolutionY;
        float quality;          // 0-1
    } ao;

    // Light Probe Statistics
    struct ProbeStats {
        bool enabled;
        float probeUpdateTime;  // ms
        uint32_t activeProbes;
        uint32_t totalProbes;
        float avgProbeInfluence;
        uint32_t probesUpdated;
    } probes;

    // Global Illumination Statistics
    struct GIStats {
        bool enabled;
        float giTime;           // ms
        float avgGITime;        // ms
        GITechnique giTechnique;  // LightProbes, ScreenSpaceGI, VoxelGI, Hybrid
        float quality;          // 0-1
        float indirectIntensity;
    } gi;

    // Memory
    uint64_t gpuMemoryUsed;     // bytes
    uint64_t gpuMemoryAvailable;
    uint64_t vramUsed;
    uint64_t vramTotal;

    // Post-processing
    struct PostProcessingStats {
        float bloomTime;        // ms
        float taaTime;          // ms
        float colorGradingTime; // ms
        bool bloomEnabled;
        bool taaEnabled;
    } postProcessing;

    RenderingStats()
    {
        memset(this, 0, sizeof(RenderingStats));
        minFrameTime = 999999.0f;
    }
};

//=============================================================================
// GPU Timer Query
//=============================================================================

struct GPUTimer {
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer;
    uint32_t queryCount;
    uint32_t currentQuery;
    bool resolved;
    uint64_t gpuFrequency;

    GPUTimer() : queryCount(0), currentQuery(0), resolved(false), gpuFrequency(10000000) {}

    bool Initialize(DX12Device* device, uint32_t numQueries);
    void Shutdown();
    void Start(ID3D12GraphicsCommandList* commandList, UINT index);
    void End(ID3D12GraphicsCommandList* commandList, UINT index);
    float Resolve(ID3D12GraphicsCommandList* commandList, UINT index);
};

//=============================================================================
// Performance Monitor
// Tracks rendering performance and statistics
//=============================================================================

class PerformanceMonitor {
public:
    PerformanceMonitor();
    ~PerformanceMonitor();

    // Initialize performance monitoring
    bool Initialize(DX12Device* device);

    // Begin frame capture
    void BeginFrame(ID3D12GraphicsCommandList* commandList);

    // End frame capture
    void EndFrame(ID3D12GraphicsCommandList* commandList);

    // Start GPU timer section
    void StartTimer(const char* name, ID3D12GraphicsCommandList* commandList);
    void EndTimer(const char* name, ID3D12GraphicsCommandList* commandList);

    // Update statistics
    void UpdateStats(float deltaTime);

    // Get current statistics
    const RenderingStats& GetStats() const { return stats_; }

    // Reset statistics
    void ResetStats();

    // Print stats to log
    void PrintStats();

    // Cleanup
    void Shutdown();

    // Is initialized
    bool IsInitialized() const { return initialized_; }

    // Enable/disable profiling
    void SetProfilingEnabled(bool enabled) { profilingEnabled_ = enabled; }
    bool IsProfilingEnabled() const { return profilingEnabled_; }

private:
    // Device
    DX12Device* device_;

    // GPU timers for different sections
    enum TimerSection {
        TIMER_AO = 0,
        TIMER_PROBES,
        TIMER_GI,
        TIMER_BLOOM,
        TIMER_TAA,
        TIMER_COLOR_GRADING,
        TIMER_TOTAL,
        TIMER_COUNT
    };

    GPUTimer gpuTimers_[TIMER_COUNT];

    // Current statistics
    RenderingStats stats_;

    // Frame tracking
    uint64_t frameCounter_;
    std::chrono::steady_clock::time_point lastUpdateTime_;

    // FPS calculation
    std::vector<float> frameTimes_;
    static const uint32_t FRAME_TIME_SAMPLES = 60;

    // Profiling state
    bool profilingEnabled_;
    bool initialized_;
};

//=============================================================================
// Rendering Profiler
// Detailed profiling for rendering passes
//=============================================================================

struct ProfileScope {
    const char* name;
    PerformanceMonitor* monitor;
    ID3D12GraphicsCommandList* commandList;

    ProfileScope(const char* n, PerformanceMonitor* m, ID3D12GraphicsCommandList* cmdList)
        : name(n), monitor(m), commandList(cmdList)
    {
        if (monitor) {
            monitor->StartTimer(name, commandList);
        }
    }

    ~ProfileScope()
    {
        if (monitor) {
            monitor->EndTimer(name, commandList);
        }
    }
};

// Convenience macro for automatic profiling
#define RENDER_PROFILE(name, monitor) \
    ProfileScope _profile_scope(name, monitor)

//=============================================================================
// GPU Memory Tracker
//=============================================================================

struct GPUMemoryStats {
    struct Allocation {
        std::string name;
        uint64_t size;
        uint32_t count;
    };

    std::map<std::string, Allocation> allocations;
    uint64_t totalMemory;
    uint64_t peakMemory;

    void RecordAllocation(const std::string& name, uint64_t size);
    void RecordDeallocation(const std::string& name, uint64_t size);
    uint64_t GetTotalMemory() const { return totalMemory; }
    uint64_t GetPeakMemory() const { return peakMemory; }
};

//=============================================================================
// Rendering Quality Settings
//=============================================================================

struct QualityPreset {
    const char* name;
    int aoSamples;
    int probeRays;
    int giOctaves;
    bool enableBloom;
    bool enableTAA;
    bool enableGI;
};

class QualitySettings {
public:
    static QualityPreset GetLowPreset() {
        return {"Low", 4, 64, 2, false, false, false};
    }

    static QualityPreset GetMediumPreset() {
        return {"Medium", 6, 128, 3, true, true, false};
    }

    static QualityPreset GetHighPreset() {
        return {"High", 8, 256, 4, true, true, true};
    }

    static QualityPreset GetUltraPreset() {
        return {"Ultra", 16, 512, 6, true, true, true};
    }

    static QualityPreset GetCustomPreset() {
        return {"Custom", 8, 256, 4, true, true, true};
    }
};

//=============================================================================
// Statistics Display
// Helper class for displaying stats in-game (for debugging)
//=============================================================================

class StatsDisplay {
public:
    StatsDisplay();

    // Update display
    void Update(const RenderingStats& stats);

    // Get formatted string for display
    std::string GetFormattedStats() const;

    // Enable/disable display
    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }

private:
    bool visible_;
    uint32_t updateFrequency_; // Update every N frames
    uint32_t frameCount_;
    RenderingStats stats_;      // Cached statistics for display
};

} // namespace Next
