#pragma once

#include "next/runtime/entity.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace Next {

using EventCallback = std::function<void(const void*)>;

class EventBus {
public:
    using EventTypeID = uint32_t;

    static EventBus& GetInstance();

    template<typename T>
    static EventTypeID GetEventTypeID() {
        static EventTypeID id = nextEventTypeID_++;
        return id;
    }

    template<typename T>
    void Subscribe(EventTypeID eventID, EventCallback callback) {
        subscribers_[eventID].push_back(callback);
    }

    template<typename T>
    void Publish(const T& event) {
        EventTypeID eventID = GetEventTypeID<T>();
        auto it = subscribers_.find(eventID);
        if (it != subscribers_.end()) {
            for (const auto& callback : it->second) {
                callback(&event);
            }
        }
    }

private:
    EventBus() = default;

    static EventTypeID nextEventTypeID_;
    std::unordered_map<EventTypeID, std::vector<EventCallback>> subscribers_;
};

} // namespace Next
