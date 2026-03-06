#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace Next {

using ComponentTypeID = uint32_t;

// Base class for all component type info
struct ComponentTypeBase {
    static std::atomic<ComponentTypeID> nextTypeID;
};

template<typename T>
struct ComponentType : public ComponentTypeBase {
    static ComponentTypeID GetID() {
        static ComponentTypeID id = nextTypeID.fetch_add(1, std::memory_order_relaxed);
        return id;
    }
};

} // namespace Next
