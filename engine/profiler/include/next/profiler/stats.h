#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Next {

struct StatValue {
    double min = 0.0;
    double max = 0.0;
    double avg = 0.0;
    double current = 0.0;
};

class FrameStats {
public:
    FrameStats();
    
    void AddFrameTime(double ms);
    void AddCpuTime(double ms);
    
    StatValue GetFrameTime() const { return frameTime_; }
    StatValue GetCpuTime() const { return cpuTime_; }
    
    void Reset();

private:
    static constexpr size_t MAX_SAMPLES = 120; // 2 seconds at 60fps
    
    std::vector<double> frameTimeSamples_;
    std::vector<double> cpuTimeSamples_;
    StatValue frameTime_;
    StatValue cpuTime_;
    
    void UpdateStats(StatValue& stat, const std::vector<double>& samples);
};

} // namespace Next
