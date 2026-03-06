#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace Next {

class Profiler {
public:
    Profiler();
    ~Profiler();

    // Frame management
    void BeginFrame();
    void EndFrame();

    // Statistics logging
    void LogStats();

    // Get singleton instance
    static Profiler& Instance();

private:
    struct FrameData {
        uint64_t frameIndex;
        double frameTimeMs;
        double cpuTimeMs;
        uint64_t timestampBegin;
        uint64_t timestampEnd;
    };

    std::vector<FrameData> frameHistory_;
    FrameData currentFrame_;
    double frequencyMs_;
    bool isFrameActive_;

    uint64_t GetCurrentTimestamp();
    double TimestampToMs(uint64_t timestamp);
};

} // namespace Next
