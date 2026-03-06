#include "next/runtime/event_bus.h"
#include "next/foundation/assert.h"
#include <unordered_map>

namespace Next {

EventBus::EventTypeID EventBus::nextEventTypeID_ = 0;

EventBus& EventBus::GetInstance() {
    static EventBus instance;
    return instance;
}

} // namespace Next
