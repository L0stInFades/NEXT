#include "next/profiler/stats.h"
#include <algorithm>

namespace Next {

FrameStats::FrameStats() {
    Reset();
}

void FrameStats::AddFrameTime(double ms) {
    frameTimeSamples_.push_back(ms);
    if (frameTimeSamples_.size() > MAX_SAMPLES) {
        frameTimeSamples_.erase(frameTimeSamples_.begin());
    }
    UpdateStats(frameTime_, frameTimeSamples_);
}

void FrameStats::AddCpuTime(double ms) {
    cpuTimeSamples_.push_back(ms);
    if (cpuTimeSamples_.size() > MAX_SAMPLES) {
        cpuTimeSamples_.erase(cpuTimeSamples_.begin());
    }
    UpdateStats(cpuTime_, cpuTimeSamples_);
}

void FrameStats::Reset() {
    frameTimeSamples_.clear();
    cpuTimeSamples_.clear();
    frameTime_ = StatValue();
    cpuTime_ = StatValue();
}

void FrameStats::UpdateStats(StatValue& stat, const std::vector<double>& samples) {
    if (samples.empty()) return;
    
    stat.current = samples.back();
    stat.min = *std::min_element(samples.begin(), samples.end());
    stat.max = *std::max_element(samples.begin(), samples.end());
    
    double sum = 0.0;
    for (double value : samples) {
        sum += value;
    }
    stat.avg = sum / samples.size();
}

} // namespace Next
