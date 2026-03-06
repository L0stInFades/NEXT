#include "next/renderer/dx12/rendering_stats.h"
#include "next/foundation/logger.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace Next {

//=============================================================================
// GPUTimer Implementation
//=============================================================================

bool GPUTimer::Initialize(DX12Device* device, uint32_t numQueries)
{
    if (!device || !device->GetDevice()) {
        NEXT_LOG_ERROR("Invalid device for GPU timer");
        return false;
    }

    queryCount = numQueries;
    currentQuery = 0;
    resolved = false;

    // Default GPU timestamp frequency (10 MHz for most GPUs)
    // This should be updated from the actual command queue when available
    gpuFrequency = 10000000;

    // Create query heap
    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = queryCount * 2; // Start and end for each query
    heapDesc.NodeMask = 0;

    HRESULT hr = device->GetDevice()->CreateQueryHeap(&heapDesc,
        IID_PPV_ARGS(&queryHeap));

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GPU query heap: 0x%X", hr);
        return false;
    }

    // Create readback buffer
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = queryCount * 2 * sizeof(uint64_t);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    hr = device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readbackBuffer)
    );

    if (FAILED(hr)) {
        NEXT_LOG_ERROR("Failed to create GPU timer readback buffer: 0x%X", hr);
        return false;
    }

    NEXT_LOG_INFO("GPU timer initialized: %u queries", queryCount);
    return true;
}

void GPUTimer::Shutdown()
{
    queryHeap.Reset();
    readbackBuffer.Reset();
    queryCount = 0;
}

void GPUTimer::Start(ID3D12GraphicsCommandList* commandList, UINT index)
{
    if (!queryHeap || !commandList || index >= queryCount) {
        return;
    }

    UINT startQueryIndex = index * 2;
    commandList->EndQuery(queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startQueryIndex);
}

void GPUTimer::End(ID3D12GraphicsCommandList* commandList, UINT index)
{
    if (!queryHeap || !commandList || index >= queryCount) {
        return;
    }

    UINT endQueryIndex = index * 2 + 1;
    commandList->EndQuery(queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, endQueryIndex);

    // Resolve queries
    commandList->ResolveQueryData(
        queryHeap.Get(),
        D3D12_QUERY_TYPE_TIMESTAMP,
        0,
        queryCount * 2,
        readbackBuffer.Get(),
        0
    );
}

float GPUTimer::Resolve(ID3D12GraphicsCommandList* commandList, UINT index)
{
    if (!readbackBuffer || !commandList) {
        return 0.0f;
    }

    // Map readback buffer
    D3D12_RANGE range = {0, queryCount * 2 * sizeof(uint64_t)};
    uint64_t* data = nullptr;
    HRESULT hr = readbackBuffer->Map(0, &range, reinterpret_cast<void**>(&data));

    if (SUCCEEDED(hr) && data) {
        UINT startQueryIndex = index * 2;
        UINT endQueryIndex = index * 2 + 1;

        uint64_t start = data[startQueryIndex];
        uint64_t end = data[endQueryIndex];

        readbackBuffer->Unmap(0, nullptr);

        // Calculate time in milliseconds using stored GPU frequency
        float timeMs = static_cast<float>(end - start) * 1000.0f / static_cast<float>(gpuFrequency);
        return timeMs;
    }

    return 0.0f;
}

//=============================================================================
// PerformanceMonitor Implementation
//=============================================================================

PerformanceMonitor::PerformanceMonitor()
    : device_(nullptr)
    , frameCounter_(0)
    , profilingEnabled_(true)
    , initialized_(false)
{
    frameTimes_.reserve(FRAME_TIME_SAMPLES);
}

PerformanceMonitor::~PerformanceMonitor()
{
    Shutdown();
}

bool PerformanceMonitor::Initialize(DX12Device* device)
{
    if (!device) {
        NEXT_LOG_ERROR("Invalid device for performance monitor");
        return false;
    }

    device_ = device;

    // Initialize GPU timers for each section
    for (int i = 0; i < TIMER_COUNT; ++i) {
        if (!gpuTimers_[i].Initialize(device, 2)) { // 2 queries per timer
            NEXT_LOG_WARNING("Failed to initialize GPU timer %d", i);
        }
    }

    lastUpdateTime_ = std::chrono::steady_clock::now();

    initialized_ = true;
    NEXT_LOG_INFO("Performance monitor initialized with %d GPU timers", TIMER_COUNT);
    return true;
}

void PerformanceMonitor::BeginFrame(ID3D12GraphicsCommandList* commandList)
{
    if (!initialized_ || !commandList) {
        return;
    }

    // Start total frame timer
    gpuTimers_[TIMER_TOTAL].Start(commandList, 0);
}

void PerformanceMonitor::EndFrame(ID3D12GraphicsCommandList* commandList)
{
    if (!initialized_ || !commandList) {
        return;
    }

    // End total frame timer
    gpuTimers_[TIMER_TOTAL].End(commandList, 0);

    // Resolve and update frame time
    float frameTime = gpuTimers_[TIMER_TOTAL].Resolve(commandList, 0);
    stats_.frameTime = frameTime;

    // Track frame times for average calculation
    frameTimes_.push_back(frameTime);
    if (frameTimes_.size() > FRAME_TIME_SAMPLES) {
        frameTimes_.erase(frameTimes_.begin());
    }

    // Calculate average
    float totalTime = 0.0f;
    for (float t : frameTimes_) {
        totalTime += t;
    }
    stats_.avgFrameTime = totalTime / static_cast<float>(frameTimes_.size());

    // Min/max
    stats_.minFrameTime = *std::min_element(frameTimes_.begin(), frameTimes_.end());
    stats_.maxFrameTime = *std::max_element(frameTimes_.begin(), frameTimes_.end());

    // Calculate FPS
    stats_.fps = frameTime > 0.0f ? 1000.0f / frameTime : 0.0f;
    stats_.avgFps = stats_.avgFrameTime > 0.0f ? 1000.0f / stats_.avgFrameTime : 0.0f;

    // Print stats every 60 frames
    if (frameCounter_ % 60 == 0 && profilingEnabled_) {
        PrintStats();
    }

    frameCounter_++;
}

void PerformanceMonitor::StartTimer(const char* name, ID3D12GraphicsCommandList* commandList)
{
    if (!profilingEnabled_ || !device_ || !commandList) {
        return;
    }

    // Map name to timer section
    int timerIndex = -1;

    if (strcmp(name, "AO") == 0 || strcmp(name, "GTAO") == 0 || strcmp(name, "HBAO") == 0 || strcmp(name, "VXAO") == 0) {
        timerIndex = TIMER_AO;
    } else if (strcmp(name, "Probes") == 0 || strcmp(name, "LightProbes") == 0) {
        timerIndex = TIMER_PROBES;
    } else if (strcmp(name, "GI") == 0 || strcmp(name, "GlobalIllumination") == 0) {
        timerIndex = TIMER_GI;
    } else if (strcmp(name, "Bloom") == 0) {
        timerIndex = TIMER_BLOOM;
    } else if (strcmp(name, "TAA") == 0 || strcmp(name, "TemporalAA") == 0) {
        timerIndex = TIMER_TAA;
    } else if (strcmp(name, "ColorGrading") == 0 || strcmp(name, "PostProcess") == 0) {
        timerIndex = TIMER_COLOR_GRADING;
    }

    if (timerIndex >= 0 && timerIndex < TIMER_COUNT) {
        gpuTimers_[timerIndex].Start(commandList, 0);
    }
}

void PerformanceMonitor::EndTimer(const char* name, ID3D12GraphicsCommandList* commandList)
{
    if (!profilingEnabled_ || !device_ || !commandList) {
        return;
    }

    // Map name to timer section
    int timerIndex = -1;

    if (strcmp(name, "AO") == 0 || strcmp(name, "GTAO") == 0 || strcmp(name, "HBAO") == 0 || strcmp(name, "VXAO") == 0) {
        timerIndex = TIMER_AO;
    } else if (strcmp(name, "Probes") == 0 || strcmp(name, "LightProbes") == 0) {
        timerIndex = TIMER_PROBES;
    } else if (strcmp(name, "GI") == 0 || strcmp(name, "GlobalIllumination") == 0) {
        timerIndex = TIMER_GI;
    } else if (strcmp(name, "Bloom") == 0) {
        timerIndex = TIMER_BLOOM;
    } else if (strcmp(name, "TAA") == 0 || strcmp(name, "TemporalAA") == 0) {
        timerIndex = TIMER_TAA;
    } else if (strcmp(name, "ColorGrading") == 0 || strcmp(name, "PostProcess") == 0) {
        timerIndex = TIMER_COLOR_GRADING;
    }

    if (timerIndex >= 0 && timerIndex < TIMER_COUNT) {
        gpuTimers_[timerIndex].End(commandList, 0);

        // Resolve timing and update stats
        float timeMs = gpuTimers_[timerIndex].Resolve(commandList, 0);

        // Update appropriate stats section
        if (timerIndex == TIMER_AO) {
            stats_.ao.enabled = true;
            stats_.ao.aoTime = timeMs;
        } else if (timerIndex == TIMER_PROBES) {
            stats_.probes.enabled = true;
            stats_.probes.probeUpdateTime = timeMs;
        } else if (timerIndex == TIMER_GI) {
            stats_.gi.enabled = true;
            stats_.gi.giTime = timeMs;
        } else if (timerIndex == TIMER_BLOOM) {
            stats_.postProcessing.bloomEnabled = true;
            stats_.postProcessing.bloomTime = timeMs;
        } else if (timerIndex == TIMER_TAA) {
            stats_.postProcessing.taaEnabled = true;
            stats_.postProcessing.taaTime = timeMs;
        } else if (timerIndex == TIMER_COLOR_GRADING) {
            stats_.postProcessing.colorGradingTime = timeMs;
        }
    }
}

void PerformanceMonitor::UpdateStats(float deltaTime)
{
    // Update time-based statistics
    auto now = std::chrono::steady_clock::now();
    float timeSinceLastUpdate = std::chrono::duration<float>(now - lastUpdateTime_).count();

    // Update frame counter and average frame time
    stats_.frameCount = frameCounter_;
    stats_.fps = stats_.avgFrameTime > 0.0f ? 1000.0f / stats_.avgFrameTime : 0.0f;
}

void PerformanceMonitor::ResetStats()
{
    memset(&stats_, 0, sizeof(RenderingStats));
    stats_.minFrameTime = 999999.0f;
    frameTimes_.clear();
}

void PerformanceMonitor::PrintStats()
{
    NEXT_LOG_INFO("=== Rendering Statistics ===");
    NEXT_LOG_INFO("Frame: %u | Time: %.2f ms (avg: %.2f ms) | FPS: %.1f (avg: %.1f)",
                  stats_.frameCount, stats_.frameTime, stats_.avgFrameTime,
                  stats_.fps, stats_.avgFps);
    NEXT_LOG_INFO("Draw Calls: %u | Vertices: %u | Primitives: %u",
                  stats_.drawCalls, stats_.verticesDrawn, stats_.primitivesDrawn);

    if (stats_.ao.enabled) {
        NEXT_LOG_INFO("AO (%s): %.2f ms | Resolution: %ux%u | Quality: %.1f",
                      static_cast<uint32_t>(stats_.ao.aoType) == 0 ? "GTAO" : static_cast<uint32_t>(stats_.ao.aoType) == 1 ? "HBAO" :
                      static_cast<uint32_t>(stats_.ao.aoType) == 2 ? "VXAO" : "None",
                      stats_.ao.aoTime, stats_.ao.resolutionX, stats_.ao.resolutionY, stats_.ao.quality);
    }

    if (stats_.probes.enabled) {
        NEXT_LOG_INFO("Light Probes: %u active | %.2f ms",
                      stats_.probes.activeProbes, stats_.probes.probeUpdateTime);
    }

    if (stats_.gi.enabled) {
        NEXT_LOG_INFO("GI (%s): %.2f ms | Intensity: %.2f | Quality: %.1f",
                      static_cast<uint32_t>(stats_.gi.giTechnique) == 0 ? "Probes" : static_cast<uint32_t>(stats_.gi.giTechnique) == 1 ? "SSGI" :
                      static_cast<uint32_t>(stats_.gi.giTechnique) == 2 ? "Voxel" : static_cast<uint32_t>(stats_.gi.giTechnique) == 3 ? "Hybrid" : "None",
                      stats_.gi.giTime, stats_.gi.indirectIntensity, stats_.gi.quality);
    }

    if (stats_.postProcessing.bloomEnabled || stats_.postProcessing.taaEnabled) {
        NEXT_LOG_INFO("Post-Processing: Bloom=%.2f ms TAA=%.2f ms ColorGrading=%.2f ms",
                      stats_.postProcessing.bloomTime, stats_.postProcessing.taaTime,
                      stats_.postProcessing.colorGradingTime);
    }

    NEXT_LOG_INFO("=============================");
}

void PerformanceMonitor::Shutdown()
{
    for (int i = 0; i < TIMER_COUNT; ++i) {
        gpuTimers_[i].Shutdown();
    }

    initialized_ = false;
    NEXT_LOG_INFO("Performance monitor shutdown complete");
}

//=============================================================================
// GPU Memory Tracker Implementation
//=============================================================================

void GPUMemoryStats::RecordAllocation(const std::string& name, uint64_t size)
{
    allocations[name].size += size;
    allocations[name].count++;

    totalMemory += size;

    if (totalMemory > peakMemory) {
        peakMemory = totalMemory;
    }
}

void GPUMemoryStats::RecordDeallocation(const std::string& name, uint64_t size)
{
    auto it = allocations.find(name);
    if (it != allocations.end()) {
        it->second.size = (size > it->second.size) ? 0 : it->second.size - size;
        it->second.count = (it->second.count > 0) ? it->second.count - 1 : 0;
        totalMemory = (size > totalMemory) ? 0 : totalMemory - size;
    }
}

//=============================================================================
// StatsDisplay Implementation
//=============================================================================

StatsDisplay::StatsDisplay()
    : visible_(false)
    , updateFrequency_(30)
    , frameCount_(0)
{
}

void StatsDisplay::Update(const RenderingStats& stats)
{
    frameCount_++;

    if (!visible_ || (frameCount_ % updateFrequency_ != 0)) {
        return;
    }

    // Cache the stats for display
    stats_ = stats;
}

std::string StatsDisplay::GetFormattedStats() const
{
    std::ostringstream ss;

    ss << "=== Rendering Statistics ===\n";
    ss << "Frame: " << stats_.frameCount << "\n";
    ss << "Time: " << std::fixed << std::setprecision(2) << stats_.frameTime << " ms\n";
    ss << "Avg Time: " << std::setprecision(2) << stats_.avgFrameTime << " ms\n";
    ss << "FPS: " << std::setprecision(1) << stats_.fps << "\n";
    ss << "Avg FPS: " << stats_.avgFps << "\n";
    ss << "Draw Calls: " << stats_.drawCalls << "\n";

    if (stats_.ao.enabled) {
        ss << "\n--- Ambient Occlusion ---\n";
        ss << "Type: " << (static_cast<uint32_t>(stats_.ao.aoType) == 0 ? "GTAO" : static_cast<uint32_t>(stats_.ao.aoType) == 1 ? "HBAO" :
                     static_cast<uint32_t>(stats_.ao.aoType) == 2 ? "VXAO" : "None") << "\n";
        ss << "Time: " << std::setprecision(2) << stats_.ao.aoTime << " ms\n";
    }

    if (stats_.probes.enabled) {
        ss << "\n--- Light Probes ---\n";
        ss << "Active: " << stats_.probes.activeProbes << "\n";
        ss << "Update Time: " << std::setprecision(2) << stats_.probes.probeUpdateTime << " ms\n";
    }

    if (stats_.gi.enabled) {
        ss << "\n--- Global Illumination ---\n";
        ss << "Technique: " << (static_cast<uint32_t>(stats_.gi.giTechnique) == 0 ? "Probes" : static_cast<uint32_t>(stats_.gi.giTechnique) == 1 ? "SSGI" :
                     static_cast<uint32_t>(stats_.gi.giTechnique) == 2 ? "Voxel" : static_cast<uint32_t>(stats_.gi.giTechnique) == 3 ? "Hybrid" : "None") << "\n";
        ss << "Time: " << std::setprecision(2) << stats_.gi.giTime << " ms\n";
    }

    ss << "=============================";

    return ss.str();
}

} // namespace Next
