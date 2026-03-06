#pragma once

#include "next/runtime/entity.h"
#include "next/runtime/component_type.h"
#include "next/runtime/transform.h"
#include "next/runtime/component.h"
#include "next/runtime/system.h"
#include "next/runtime/query.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <deque>
#include <mutex>

namespace Next {

// Component array base class
class IComponentArray {
public:
    virtual ~IComponentArray() = default;
    virtual void RemoveEntity(Entity entity) = 0;
    virtual bool HasEntity(Entity entity) const = 0;
};

// Typed component array
template<typename T>
class ComponentArray : public IComponentArray {
public:
    void AddComponent(Entity entity, T component) {
        components_[entity] = component;
    }

    void RemoveEntity(Entity entity) override {
        components_.erase(entity);
    }

    T* GetComponent(Entity entity) {
        auto it = components_.find(entity);
        if (it != components_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    bool HasEntity(Entity entity) const override {
        return components_.find(entity) != components_.end();
    }

    const std::unordered_map<Entity, T, EntityHash>& GetAll() const {
        return components_;
    }

private:
    std::unordered_map<Entity, T, EntityHash> components_;
};

// Enhanced World class with full ECS support
class World {
public:
    World();
    ~World();

    // Entity management
    Entity CreateEntity();
    void DestroyEntity(Entity entity);
    bool IsEntityValid(Entity entity) const;

    // Component management (generic)
    template<typename T>
    T& AddComponent(Entity entity) {
        // Ensure component array exists
        ComponentTypeID typeID = ComponentType<T>::GetID();
        if (componentArrays_.find(typeID) == componentArrays_.end()) {
            componentArrays_[typeID] = std::make_unique<ComponentArray<T>>();
        }

        // Get typed array and add component
        auto* array = static_cast<ComponentArray<T>*>(componentArrays_[typeID].get());
        T component;
        array->AddComponent(entity, component);

        // Track entity components
        entityComponents_[entity].insert(typeID);

        // Notify systems
        NotifyComponentAdded(entity, typeID);

        return *array->GetComponent(entity);
    }

    template<typename T>
    T& AddComponent(Entity entity, const T& component) {
        // Ensure component array exists
        ComponentTypeID typeID = ComponentType<T>::GetID();
        if (componentArrays_.find(typeID) == componentArrays_.end()) {
            componentArrays_[typeID] = std::make_unique<ComponentArray<T>>();
        }

        // Get typed array and add component
        auto* array = static_cast<ComponentArray<T>*>(componentArrays_[typeID].get());
        array->AddComponent(entity, component);

        // Track entity components
        entityComponents_[entity].insert(typeID);

        // Notify systems
        NotifyComponentAdded(entity, typeID);

        return *array->GetComponent(entity);
    }

    template<typename T>
    T* GetComponent(Entity entity) {
        ComponentTypeID typeID = ComponentType<T>::GetID();
        auto it = componentArrays_.find(typeID);
        if (it != componentArrays_.end()) {
            auto* array = static_cast<ComponentArray<T>*>(it->second.get());
            return array->GetComponent(entity);
        }
        return nullptr;
    }

    template<typename T>
    const T* GetComponent(Entity entity) const {
        ComponentTypeID typeID = ComponentType<T>::GetID();
        auto it = componentArrays_.find(typeID);
        if (it != componentArrays_.end()) {
            auto* array = static_cast<ComponentArray<T>*>(it->second.get());
            return array->GetComponent(entity);
        }
        return nullptr;
    }

    template<typename T>
    bool HasComponent(Entity entity) const {
        ComponentTypeID typeID = ComponentType<T>::GetID();
        auto entityIt = entityComponents_.find(entity);
        if (entityIt != entityComponents_.end()) {
            return entityIt->second.find(typeID) != entityIt->second.end();
        }
        return false;
    }

    template<typename T>
    void RemoveComponent(Entity entity) {
        ComponentTypeID typeID = ComponentType<T>::GetID();
        auto it = componentArrays_.find(typeID);
        if (it != componentArrays_.end()) {
            it->second->RemoveEntity(entity);

            // Untrack entity components
            auto entityIt = entityComponents_.find(entity);
            if (entityIt != entityComponents_.end()) {
                entityIt->second.erase(typeID);
            }

            // Notify systems
            NotifyComponentRemoved(entity, typeID);
        }
    }

    // Query entities
    template<typename... Components>
    std::vector<Entity> QueryEntitiesWith() {
        std::vector<Entity> result;
        std::vector<ComponentTypeID> types = {ComponentType<Components>::GetID()...};

        for (const auto& [entity, componentSet] : entityComponents_) {
            bool hasAll = true;
            for (ComponentTypeID type : types) {
                if (componentSet.find(type) == componentSet.end()) {
                    hasAll = false;
                    break;
                }
            }
            if (hasAll) {
                result.push_back(entity);
            }
        }

        return result;
    }

    // Legacy Transform methods (for backward compatibility)
    TransformComponent* AddTransform(Entity entity);
    TransformComponent* GetTransform(Entity entity);
    void RemoveTransform(Entity entity);

    // System management
    void RegisterSystem(System* system);
    void UnregisterSystem(System* system);

    // World update
    void Update(float deltaTime);

    // Statistics
    size_t GetEntityCount() const { return entities_.size(); }
    std::vector<Entity> GetAllEntities() const;

    // Performance testing
    struct WorldStats {
        size_t entityCount;
        size_t totalComponents;
        size_t systemCount;
    };
    WorldStats GetStats() const;

private:
    void NotifyEntityCreated(Entity entity);
    void NotifyEntityDestroyed(Entity entity);
    void NotifyComponentAdded(Entity entity, ComponentTypeID type);
    void NotifyComponentRemoved(Entity entity, ComponentTypeID type);

    // Entity management
    struct EntityMetadata {
        uint16_t version;
        bool alive;
    };

    std::unordered_map<uint64_t, EntityMetadata> entityMetadata_;
    std::unordered_set<Entity, EntityHash> entities_;

    uint64_t nextEntityID_;
    std::deque<uint64_t> freeIDs_;

    // Component storage
    std::unordered_map<ComponentTypeID, std::unique_ptr<IComponentArray>> componentArrays_;
    std::unordered_map<Entity, std::unordered_set<ComponentTypeID>, EntityHash> entityComponents_;

    // Systems
    std::vector<System*> systems_;
};

} // namespace Next
