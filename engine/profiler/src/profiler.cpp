#include "next/profiler/profiler.h"
#include "next/profiler/stats.h"
#include "next/foundation/logger.h"
#include <chrono>
#include <sstream>

namespace Next {

Profiler::Profiler() 
    : frequencyMs_(0.0)
    , isFrameActive_(false)
    , currentFrame_({0, 0.0, 0.0, 0, 0})
{
    frequencyMs_ = 0.000001;
}

Profiler::~Profiler() {
}

void Profiler::BeginFrame() {
    if (isFrameActive_) {
        NEXT_LOG_WARNING("Profiler: BeginFrame called while frame is active");
        return;
    }
    
    isFrameActive_ = true;
    currentFrame_.frameIndex++;
    currentFrame_.timestampBegin = GetCurrentTimestamp();
}

void Profiler::EndFrame() {
    if (!isFrameActive_) {
        NEXT_LOG_WARNING("Profiler: EndFrame called without active frame");
        return;
    }
    
    currentFrame_.timestampEnd = GetCurrentTimestamp();
    
    // Calculate frame time
    currentFrame_.frameTimeMs = TimestampToMs(
        currentFrame_.timestampEnd - currentFrame_.timestampBegin
    );
    
    // For now, CPU time equals frame time (will be refined with scopes)
    currentFrame_.cpuTimeMs = currentFrame_.frameTimeMs;
    
    // Store frame data for history
    frameHistory_.push_back(currentFrame_);
    if (frameHistory_.size() > 120) { // Keep last 120 frames
        frameHistory_.erase(frameHistory_.begin());
    }
    
    isFrameActive_ = false;
}

void Profiler::LogStats() {
    if (frameHistory_.empty()) {
        return;
    }
    
    // Calculate statistics
    FrameStats stats;
    for (const auto& frame : frameHistory_) {
        stats.AddFrameTime(frame.frameTimeMs);
        stats.AddCpuTime(frame.cpuTimeMs);
    }
    
    auto frameTime = stats.GetFrameTime();
    auto cpuTime = stats.GetCpuTime();
    
    std::stringstream ss;
    ss << "\n========== Frame Profiler Stats ==========\n";
    ss << "Frame Time: " << frameTime.current << " ms (Avg: " 
       << frameTime.avg << " ms, Min: " << frameTime.min 
       << " ms, Max: " << frameTime.max << " ms)\n";
    ss << "CPU Time: " << cpuTime.current << " ms (Avg: " 
       << cpuTime.avg << " ms, Min: " << cpuTime.min 
       << " ms, Max: " << cpuTime.max << " ms)\n";
    ss << "FPS: " << (1000.0 / frameTime.current) << " (Avg: " 
       << (1000.0 / frameTime.avg) << ")\n";
    ss << "==========================================\n";
    
    NEXT_LOG_INFO("%s", ss.str().c_str());
}

Profiler& Profiler::Instance() {
    static Profiler instance;
    return instance;
}

uint64_t Profiler::GetCurrentTimestamp() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

double Profiler::TimestampToMs(uint64_t timestamp) {
    return static_cast<double>(timestamp) * frequencyMs_;
}

} // namespace Next
