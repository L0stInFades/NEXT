#include "next/profiler/cpu_scope.h"
#include "next/foundation/logger.h"
#include <windows.h>

namespace Next {

CpuScope::CpuScope(const char* name) : name_(name) {
    startTimestamp_ = GetTickCount64();
}

CpuScope::~CpuScope() {
    uint64_t endTimestamp = GetTickCount64();
    double elapsedMs = static_cast<double>(endTimestamp - startTimestamp_);
    
    NEXT_LOG_INFO("[CPU Scope] %s: %.2f ms", name_, elapsedMs);
}

} // namespace Next
