#include "next/profiler/cpu_scope.h"
#include "next/foundation/logger.h"
#include <chrono>

namespace {

uint64_t CurrentTimeMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}

namespace Next {

CpuScope::CpuScope(const char* name) : name_(name) {
    startTimestamp_ = CurrentTimeMs();
}

CpuScope::~CpuScope() {
    uint64_t endTimestamp = CurrentTimeMs();
    double elapsedMs = static_cast<double>(endTimestamp - startTimestamp_);
    
    NEXT_LOG_INFO("[CPU Scope] %s: %.2f ms", name_, elapsedMs);
}

} // namespace Next
