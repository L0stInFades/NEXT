#pragma once

#include <cstdint>

namespace Next {

// Platform initialization and shutdown
bool PlatformInitialize();
void PlatformShutdown();

// Platform info
const char* GetPlatformName();

// Time
double GetTimeInSeconds();

// Thread sleep
void SleepMs(uint32_t milliseconds);

} // namespace Next
