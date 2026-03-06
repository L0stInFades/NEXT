#pragma once

#include <cstdint>
#include <string>
#include "next/profiler/profiler.h"

namespace Next {

class CpuScope {
public:
    explicit CpuScope(const char* name);
    ~CpuScope();

private:
    const char* name_;
    uint64_t startTimestamp_;
};

} // namespace Next

#define NEXT_CPU_SCOPE(name) Next::CpuScope _cpu_scope_##__LINE__(name)
