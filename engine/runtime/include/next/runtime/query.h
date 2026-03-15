#pragma once

#include "next/runtime/world.h"
#include <type_traits>
#include <utility>

namespace Next {

template<typename... Components>
class QueryView {
public:
    explicit QueryView(World& world) : world_(world) {}

    template<typename Func>
    void ForEach(Func&& func) {
        auto entities = world_.QueryEntitiesWith<Components...>();
        for (Entity entity : entities) {
            Invoke(std::forward<Func>(func), entity);
        }
    }

private:
    template<typename T>
    struct AlwaysFalse : std::false_type {};

    template<typename Func>
    void Invoke(Func&& func, Entity entity) {
        if constexpr (std::is_invocable_v<Func&, Entity, Components&...>) {
            func(entity, *world_.GetComponent<Components>(entity)...);
        } else if constexpr (std::is_invocable_v<Func&, Components&...>) {
            func(*world_.GetComponent<Components>(entity)...);
        } else if constexpr (std::is_invocable_v<Func&, Entity>) {
            func(entity);
        } else {
            static_assert(AlwaysFalse<Func>::value,
                          "Query::ForEach callback must accept Entity, Components..., or Entity + Components...");
        }
    }

    World& world_;
};

class Query {
public:
    explicit Query(World& world) : world_(world) {}

    template<typename... Components>
    QueryView<Components...> All() {
        return QueryView<Components...>(world_);
    }

private:
    World& world_;
};

} // namespace Next
