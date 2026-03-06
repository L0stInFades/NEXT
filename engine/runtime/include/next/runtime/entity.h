#pragma once

#include <cstdint>
#include <functional>

namespace Next {

// Entity ID with version to avoid reuse issues
struct Entity {
    uint64_t id : 48;      // Entity index
    uint64_t version : 16; // Version number

    Entity() : id(0), version(0) {}
    explicit Entity(uint64_t i, uint64_t v = 0) : id(i), version(v) {}

    bool IsValid() const { return id != 0 && version != 0; }

    bool operator==(const Entity& other) const {
        return id == other.id && version == other.version;
    }

    bool operator!=(const Entity& other) const {
        return !(*this == other);
    }

    operator uint64_t() const {
        return (uint64_t)version << 48 | id;
    }

    static Entity Invalid() { return Entity(); }
};

const Entity INVALID_ENTITY = Entity::Invalid();

// Entity hash for unordered_map
struct EntityHash {
    size_t operator()(const Entity& e) const {
        return std::hash<uint64_t>()(static_cast<uint64_t>(e));
    }
};

} // namespace Next
