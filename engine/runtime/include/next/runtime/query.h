#pragma once

#include "next/runtime/entity.h"
#include "next/runtime/component_type.h"
#include <vector>
#include <functional>
#include <type_traits>

namespace Next {

// Forward declarations
class World;

// Query class for efficient component iteration
class Query {
public:
    Query(World& world) : world_(world) {}

    // Query entities with all specified components
    template<typename... Components>
    Query& All() {
        // Build component mask
        AllTypes = {ComponentType<Components>::GetID()...};
        return *this;
    }

    // Execute query on matching entities
    template<typename Func>
    void ForEach(Func&& func) {
        // This will be implemented in World class
        // For now, placeholder
    }

private:
    World& world_;
    std::vector<ComponentTypeID> AllTypes;
    std::vector<ComponentTypeID> AnyTypes;
    std::vector<ComponentTypeID> NoneTypes;
};

} // namespace Next
