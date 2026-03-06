#include "next/streaming/debug_visualization.h"
#include "next/log/log.h"
#include <algorithm>
#include <sstream>
#include <limits>
#include <cfloat>

namespace Next {
namespace Streaming {

// ===== Debug Visualization System Implementation =====

DebugVisualizationSystem::DebugVisualizationSystem()
    : currentFrame_(0)
    , initialized_(false)
{
}

DebugVisualizationSystem::~DebugVisualizationSystem() {
    Shutdown();
}

bool DebugVisualizationSystem::Initialize(const DebugVisualizationConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARN() << "DebugVisualizationSystem already initialized";
        return true;
    }

    config_ = config;

    NEXT_LOG_INFO() << "DebugVisualizationSystem initialized (CP7: World Streaming)";
    NEXT_LOG_INFO() << "  Visualization mode: " << static_cast<uint32_t>(config_.mode);
    NEXT_LOG_INFO() << "  Enabled: " << (config_.enabled ? "yes" : "no");

    initialized_ = true;
    return true;
}

void DebugVisualizationSystem::Update(float deltaTime, const StreamingManager* streamingManager) {
    if (!initialized_ || !config_.enabled) {
        return;
    }

    currentFrame_++;

    // Update debug elements
    UpdateDebugElements(deltaTime);

    // Update heatmap
    if (config_.enableHeatmap) {
        UpdateHeatmapDecay();
    }
}

void DebugVisualizationSystem::Render(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    if (!initialized_ || !config_.enabled) {
        return;
    }

    // Render based on visualization mode
    switch (config_.mode) {
        case VisualizationMode::LoadState:
            RenderLoadStateMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::Priority:
            RenderPriorityMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::MemoryUsage:
            RenderMemoryUsageMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::LOD:
            RenderLODMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::Interest:
            RenderInterestMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::Prediction:
            RenderPredictionMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::IO:
            RenderIOMode(viewMatrix, projectionMatrix);
            break;

        case VisualizationMode::Heatmap:
            RenderHeatmapMode(viewMatrix, projectionMatrix);
            break;

        default:
            break;
    }

    // Render debug elements
    RenderDebugLines(viewMatrix, projectionMatrix);
    RenderDebugBoxes(viewMatrix, projectionMatrix);
    RenderDebugText(viewMatrix, projectionMatrix);
}

void DebugVisualizationSystem::SetVisualizationMode(VisualizationMode mode) {
    config_.mode = mode;
}

void DebugVisualizationSystem::DrawLine(const Vec3& start, const Vec3& end, const Vec3& color, float lifetime) {
    DebugLine line;
    line.start = start;
    line.end = end;
    line.color = color;
    line.lifetime = lifetime;

    lines_.push_back(line);
}

void DebugVisualizationSystem::DrawBox(const Vec3& boundsMin, const Vec3& boundsMax, const Vec3& color, float alpha, float lifetime) {
    DebugBox box;
    box.boundsMin = boundsMin;
    box.boundsMax = boundsMax;
    box.color = color;
    box.alpha = alpha;
    box.lifetime = lifetime;

    boxes_.push_back(box);
}

void DebugVisualizationSystem::DrawText(const Vec3& position, const std::string& text, const Vec3& color, float size, float lifetime) {
    DebugText debugText;
    debugText.position = position;
    debugText.text = text;
    debugText.color = color;
    debugText.size = size;
    debugText.lifetime = lifetime;

    texts_.push_back(debugText);
}

void DebugVisualizationSystem::ClearDebugElements() {
    lines_.clear();
    boxes_.clear();
    texts_.clear();
}

void DebugVisualizationSystem::ClearTemporaryElements() {
    // Remove elements with lifetime > 0
    lines_.erase(
        std::remove_if(lines_.begin(), lines_.end(),
            [](const DebugLine& line) { return line.lifetime > 0.0f; }),
        lines_.end()
    );

    boxes_.erase(
        std::remove_if(boxes_.begin(), boxes_.end(),
            [](const DebugBox& box) { return box.lifetime > 0.0f; }),
        boxes_.end()
    );

    texts_.erase(
        std::remove_if(texts_.begin(), texts_.end(),
            [](const DebugText& text) { return text.lifetime > 0.0f; }),
        texts_.end()
    );
}

void DebugVisualizationSystem::UpdateCellVisualization(const CellCoord& coord, const CellVisualizationData& data) {
    cellData_[coord] = data;
}

CellVisualizationData* DebugVisualizationSystem::GetCellVisualization(const CellCoord& coord) {
    auto it = cellData_.find(coord);
    if (it != cellData_.end()) {
        return &it->second;
    }
    return nullptr;
}

void DebugVisualizationSystem::DrawStatisticsOverlay(const StreamingStatistics& stats) {
    // TODO: Implement statistics overlay rendering
    NEXT_LOG_WARN() << "Statistics overlay rendering not implemented";
}

void DebugVisualizationSystem::DrawPerformanceMetrics(const IOStatistics& ioStats) {
    // TODO: Implement performance metrics rendering
    NEXT_LOG_WARN() << "Performance metrics rendering not implemented";
}

void DebugVisualizationSystem::UpdateHeatmap(const Vec3& position, float intensity) {
    // TODO: Implement heatmap update
    NEXT_LOG_WARN() << "Heatmap update not fully implemented";
}

Vec3 DebugVisualizationSystem::GetHeatmapColor(const Vec3& position) const {
    // TODO: Implement heatmap color lookup
    return Vec3(0.0f, 0.0f, 0.0f);
}

void DebugVisualizationSystem::CaptureVisualization(const std::string& filename) {
    // TODO: Implement screenshot capture
    NEXT_LOG_WARN() << "Screenshot capture not implemented";
}

void DebugVisualizationSystem::Shutdown() {
    if (!initialized_) {
        return;
    }

    ClearDebugElements();
    cellData_.clear();
    heatmap_.clear();

    initialized_ = false;
    NEXT_LOG_INFO() << "DebugVisualizationSystem shutdown complete";
}

// ===== Private Methods =====

void DebugVisualizationSystem::RenderLoadStateMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    for (const auto& [coord, data] : cellData_) {
        Vec3 color = GetColorForLoadState(data.loadState);
        DrawCellBorder(coord, color, 0.5f);

        if (config_.showCellText) {
            std::ostringstream oss;
            oss << "[" << coord.x << "," << coord.z << "]";

            const char* stateName = "";
            switch (data.loadState) {
                case CellLoadState::Unloaded: stateName = "Unloaded"; break;
                case CellLoadState::Queued: stateName = "Queued"; break;
                case CellLoadState::Loading: stateName = "Loading"; break;
                case CellLoadState::Loaded: stateName = "Loaded"; break;
                case CellLoadState::Unloading: stateName = "Unloading"; break;
                case CellLoadState::Error: stateName = "Error"; break;
                default: stateName = "Unknown"; break;
            }

            DrawText(data.worldPosition, oss.str() + " " + stateName);
        }
    }
}

void DebugVisualizationSystem::RenderPriorityMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    // TODO: Implement priority mode rendering
    NEXT_LOG_WARN() << "Priority mode rendering not fully implemented";
}

void DebugVisualizationSystem::RenderMemoryUsageMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    // TODO: Implement memory usage mode rendering
    NEXT_LOG_WARN() << "Memory usage mode rendering not fully implemented";
}

void DebugVisualizationSystem::RenderLODMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    // TODO: Implement LOD mode rendering
    NEXT_LOG_WARN() << "LOD mode rendering not fully implemented";
}

void DebugVisualizationSystem::RenderInterestMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    // TODO: Implement interest mode rendering
    NEXT_LOG_WARN() << "Interest mode rendering not fully implemented";
}

void DebugVisualizationSystem::RenderPredictionMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    // TODO: Implement prediction mode rendering
    NEXT_LOG_WARN() << "Prediction mode rendering not fully implemented";
}

void DebugVisualizationSystem::RenderIOMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    // TODO: Implement IO mode rendering
    NEXT_LOG_WARN() << "IO mode rendering not fully implemented";
}

void DebugVisualizationSystem::RenderHeatmapMode(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    // TODO: Implement heatmap mode rendering
    NEXT_LOG_WARN() << "Heatmap mode rendering not fully implemented";
}

Vec3 DebugVisualizationSystem::GetColorForLoadState(CellLoadState state) const {
    switch (state) {
        case CellLoadState::Unloaded: return config_.unloadedColor;
        case CellLoadState::Queued: return config_.queuedColor;
        case CellLoadState::Loading: return config_.loadingColor;
        case CellLoadState::Loaded: return config_.loadedColor;
        case CellLoadState::Unloading: return config_.unloadingColor;
        case CellLoadState::Error: return config_.errorColor;
        default: return Vec3(1.0f, 1.0f, 1.0f);
    }
}

Vec3 DebugVisualizationSystem::GetColorForPriority(float priority, float minPriority, float maxPriority) const {
    float t = (priority - minPriority) / (maxPriority - minPriority + 0.0001f);
    return config_.lowPriorityColor * (1.0f - t) + config_.highPriorityColor * t;
}

Vec3 DebugVisualizationSystem::GetColorForMemoryUsage(uint64_t usage, uint64_t maxUsage) const {
    float t = static_cast<float>(usage) / static_cast<float>(maxUsage + 1);
    return Vec3(t, 1.0f - t, 0.0f);  // Red to green gradient
}

Vec3 DebugVisualizationSystem::GetColorForLOD(uint32_t lod) const {
    // Color gradient from high detail (green) to low detail (red)
    float t = std::min(lod / 4.0f, 1.0f);
    return Vec3(t, 1.0f - t, 0.0f);
}

void DebugVisualizationSystem::DrawCellBorder(const CellCoord& coord, const Vec3& color, float alpha) {
    // TODO: Implement cell border rendering
    NEXT_LOG_WARN() << "Cell border rendering not fully implemented";
}

void DebugVisualizationSystem::DrawCellText(const CellCoord& coord, const std::string& text) {
    // TODO: Implement cell text rendering
    NEXT_LOG_WARN() << "Cell text rendering not fully implemented";
}

void DebugVisualizationSystem::UpdateHeatmapDecay() {
    for (auto& [coord, cell] : heatmap_) {
        cell.intensity *= config_.heatmapDecay;
    }

    // Remove low-intensity cells
    for (auto it = heatmap_.begin(); it != heatmap_.end();) {
        if (it->second.intensity < 0.01f) {
            it = heatmap_.erase(it);
        } else {
            ++it;
        }
    }
}

void DebugVisualizationSystem::UpdateDebugElements(float deltaTime) {
    // Update temporary elements
    for (auto& line : lines_) {
        if (line.lifetime > 0.0f) {
            line.lifetime -= deltaTime;
        }
    }

    for (auto& box : boxes_) {
        if (box.lifetime > 0.0f) {
            box.lifetime -= deltaTime;
        }
    }

    for (auto& text : texts_) {
        if (text.lifetime > 0.0f) {
            text.lifetime -= deltaTime;
        }
    }

    RemoveExpiredElements();
}

void DebugVisualizationSystem::RemoveExpiredElements() {
    lines_.erase(
        std::remove_if(lines_.begin(), lines_.end(),
            [](const DebugLine& line) { return line.lifetime < 0.0f; }),
        lines_.end()
    );

    boxes_.erase(
        std::remove_if(boxes_.begin(), boxes_.end(),
            [](const DebugBox& box) { return box.lifetime < 0.0f; }),
        boxes_.end()
    );

    texts_.erase(
        std::remove_if(texts_.begin(), texts_.end(),
            [](const DebugText& text) { return text.lifetime < 0.0f; }),
        texts_.end()
    );
}

void DebugVisualizationSystem::RenderDebugLines(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    // TODO: Implement debug line rendering
}

void DebugVisualizationSystem::RenderDebugBoxes(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    // TODO: Implement debug box rendering
}

void DebugVisualizationSystem::RenderDebugText(const Mat4& viewMatrix, const Mat4& projectionMatrix) {
    // TODO: Implement debug text rendering
}

// ===== Streaming Profiler Implementation =====

StreamingProfiler::StreamingProfiler()
    : maxSamples_(1024)
    , initialized_(false)
{
}

StreamingProfiler::~StreamingProfiler() {
    Shutdown();
}

bool StreamingProfiler::Initialize(uint32_t maxSamples) {
    if (initialized_) {
        return true;
    }

    maxSamples_ = maxSamples;
    initialized_ = true;

    return true;
}

void StreamingProfiler::BeginEvent(const std::string& name) {
    // TODO: Implement event timing
}

void StreamingProfiler::EndEvent(const std::string& name) {
    // TODO: Implement event timing
}

void StreamingProfiler::RecordMetric(const std::string& name, float value) {
    auto it = metrics_.find(name);
    if (it == metrics_.end()) {
        MetricData metric;
        metric.name = name;
        metric.average = value;
        metric.minValue = value;
        metric.maxValue = value;
        metric.values.push_back(value);

        metrics_[name] = metric;
    } else {
        it->second.values.push_back(value);
        if (it->second.values.size() > maxSamples_) {
            it->second.values.erase(it->second.values.begin());
        }

        // Update statistics
        float sum = 0.0f;
        float minVal = FLT_MAX;
        float maxVal = -FLT_MAX;

        for (float val : it->second.values) {
            sum += val;
            minVal = std::min(minVal, val);
            maxVal = std::max(maxVal, val);
        }

        it->second.average = sum / it->second.values.size();
        it->second.minValue = minVal;
        it->second.maxValue = maxVal;
    }
}

float StreamingProfiler::GetAverageEventTime(const std::string& name) const {
    auto it = events_.find(name);
    if (it != events_.end() && !it->second.empty()) {
        float sum = 0.0f;
        for (const auto& event : it->second) {
            sum += event.duration;
        }
        return sum / it->second.size();
    }
    return 0.0f;
}

float StreamingProfiler::GetMaxEventTime(const std::string& name) const {
    auto it = events_.find(name);
    if (it != events_.end() && !it->second.empty()) {
        float maxTime = 0.0f;
        for (const auto& event : it->second) {
            maxTime = std::max(maxTime, event.duration);
        }
        return maxTime;
    }
    return 0.0f;
}

float StreamingProfiler::GetMinEventTime(const std::string& name) const {
    auto it = events_.find(name);
    if (it != events_.end() && !it->second.empty()) {
        float minTime = FLT_MAX;
        for (const auto& event : it->second) {
            minTime = std::min(minTime, event.duration);
        }
        return minTime;
    }
    return 0.0f;
}

uint32_t StreamingProfiler::GetEventCount(const std::string& name) const {
    auto it = events_.find(name);
    if (it != events_.end()) {
        return static_cast<uint32_t>(it->second.size());
    }
    return 0;
}

void StreamingProfiler::ExportToCSV(const std::string& filename) {
    // TODO: Implement CSV export
    NEXT_LOG_WARN() << "CSV export not implemented";
}

std::string StreamingProfiler::GenerateReport() const {
    std::ostringstream oss;
    oss << "Streaming Profiler Report\n";
    oss << "========================\n\n";

    for (const auto& [name, metric] : metrics_) {
        oss << metric.name << ":\n";
        oss << "  Average: " << metric.average << "\n";
        oss << "  Min: " << metric.minValue << "\n";
        oss << "  Max: " << metric.maxValue << "\n\n";
    }

    return oss.str();
}

void StreamingProfiler::Reset() {
    events_.clear();
    metrics_.clear();
}

void StreamingProfiler::Shutdown() {
    Reset();
    initialized_ = false;
}

} // namespace Streaming
} // namespace Next
