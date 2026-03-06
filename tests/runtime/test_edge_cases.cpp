#include "next/runtime/world.h"
#include "next/runtime/entity.h"
#include "next/runtime/component.h"
#include "next/foundation/logger.h"
#include <gtest/gtest.h>
#include <vector>

namespace Next {
namespace testing {

using ::testing::Test;

// Edge case tests for critical scenarios
class EdgeCaseTest : public Test {
protected:
    void SetUp() override {
        Logger::Initialize();
    }

    void TearDown() override {
        Logger::Shutdown();
    }

    World world_;
};

// Test entity destruction during iteration
TEST_F(EdgeCaseTest, EntityDestructionDuringIteration) {
    // Create multiple entities
    std::vector<Entity> entities;
    for (int i = 0; i < 10; ++i) {
        Entity e = world_.CreateEntity();
        world_.AddComponent<TransformComponent>(e);
        world_.AddComponent<NameComponent>(e, ("Entity_" + std::to_string(i)).c_str());
        entities.push_back(e);
    }

    EXPECT_EQ(world_.GetEntityCount(), 10u);

    // Iterate and destroy entities with odd indices
    // This tests that destruction during iteration doesn't crash
    auto allEntities = world_.GetAllEntities();
    for (size_t i = 0; i < allEntities.size(); ++i) {
        if (i % 2 == 1) {  // Destroy odd-indexed entities
            world_.DestroyEntity(allEntities[i]);
        }
    }

    // Verify remaining entities
    EXPECT_EQ(world_.GetEntityCount(), 5u);

    // Verify destroyed entities are invalid
    for (size_t i = 0; i < entities.size(); ++i) {
        if (i % 2 == 1) {
            EXPECT_FALSE(world_.IsEntityValid(entities[i]));
        } else {
            EXPECT_TRUE(world_.IsEntityValid(entities[i]));
        }
    }
}

// Test component removal during iteration
TEST_F(EdgeCaseTest, ComponentRemovalDuringIteration) {
    // Create entities with components
    std::vector<Entity> entities;
    for (int i = 0; i < 10; ++i) {
        Entity e = world_.CreateEntity();
        world_.AddComponent<TransformComponent>(e);
        world_.AddComponent<NameComponent>(e, ("Entity_" + std::to_string(i)).c_str());
        entities.push_back(e);
    }

    // Query entities with both components
    auto withBoth = world_.QueryEntitiesWith<TransformComponent, NameComponent>();
    EXPECT_EQ(withBoth.size(), 10u);

    // Remove NameComponent from entities with odd indices
    for (size_t i = 0; i < entities.size(); ++i) {
        if (i % 2 == 1) {
            world_.RemoveComponent<NameComponent>(entities[i]);
        }
    }

    // Query again - should only find entities with NameComponent
    withBoth = world_.QueryEntitiesWith<TransformComponent, NameComponent>();
    EXPECT_EQ(withBoth.size(), 5u);

    // Verify component states
    for (size_t i = 0; i < entities.size(); ++i) {
        bool hasTransform = world_.HasComponent<TransformComponent>(entities[i]);
        bool hasName = world_.HasComponent<NameComponent>(entities[i]);

        EXPECT_TRUE(hasTransform);  // Transform should always be present
        if (i % 2 == 1) {
            EXPECT_FALSE(hasName);  // Name removed from odd indices
        } else {
            EXPECT_TRUE(hasName);   // Name still present on even indices
        }
    }
}

// Test entity creation and destruction in same frame
TEST_F(EdgeCaseTest, CreateDestroyInSameFrame) {
    // Create initial entities
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();
    Entity e3 = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<TransformComponent>(e2);
    world_.AddComponent<TransformComponent>(e3);

    EXPECT_EQ(world_.GetEntityCount(), 3u);

    // Destroy e2
    world_.DestroyEntity(e2);
    EXPECT_EQ(world_.GetEntityCount(), 2u);

    // Create new entity
    Entity e4 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e4);
    EXPECT_EQ(world_.GetEntityCount(), 3u);

    // Verify e2 is invalid and e4 is valid
    EXPECT_FALSE(world_.IsEntityValid(e2));
    EXPECT_TRUE(world_.IsEntityValid(e4));

    // Verify e1 and e3 are still valid
    EXPECT_TRUE(world_.IsEntityValid(e1));
    EXPECT_TRUE(world_.IsEntityValid(e3));
}

// Test rapid entity creation and destruction
TEST_F(EdgeCaseTest, RapidCreateDestroy) {
    std::vector<Entity> entities;

    // Create 100 entities rapidly
    for (int i = 0; i < 100; ++i) {
        Entity e = world_.CreateEntity();
        world_.AddComponent<TransformComponent>(e);
        entities.push_back(e);
    }

    EXPECT_EQ(world_.GetEntityCount(), 100u);

    // Destroy all entities
    for (Entity e : entities) {
        world_.DestroyEntity(e);
    }

    EXPECT_EQ(world_.GetEntityCount(), 0u);

    // Verify all entities are invalid
    for (Entity e : entities) {
        EXPECT_FALSE(world_.IsEntityValid(e));
    }

    // Create 100 new entities (should reuse IDs)
    std::vector<Entity> newEntities;
    for (int i = 0; i < 100; ++i) {
        Entity e = world_.CreateEntity();
        world_.AddComponent<TransformComponent>(e);
        newEntities.push_back(e);
    }

    EXPECT_EQ(world_.GetEntityCount(), 100u);

    // Verify new entities are valid but have different versions
    for (size_t i = 0; i < entities.size(); ++i) {
        EXPECT_FALSE(world_.IsEntityValid(entities[i]));     // Old ones still invalid
        EXPECT_TRUE(world_.IsEntityValid(newEntities[i]));   // New ones valid
    }
}

// Test multiple components add/remove
TEST_F(EdgeCaseTest, MultipleComponentOperations) {
    Entity e = world_.CreateEntity();

    // Add multiple components
    world_.AddComponent<TransformComponent>(e);
    world_.AddComponent<NameComponent>(e, "TestEntity");
    world_.AddComponent<MeshRendererComponent>(e);

    EXPECT_TRUE(world_.HasComponent<TransformComponent>(e));
    EXPECT_TRUE(world_.HasComponent<NameComponent>(e));
    EXPECT_TRUE(world_.HasComponent<MeshRendererComponent>(e));

    // Remove middle component
    world_.RemoveComponent<NameComponent>(e);

    EXPECT_TRUE(world_.HasComponent<TransformComponent>(e));
    EXPECT_FALSE(world_.HasComponent<NameComponent>(e));
    EXPECT_TRUE(world_.HasComponent<MeshRendererComponent>(e));

    // Remove remaining components
    world_.RemoveComponent<TransformComponent>(e);
    world_.RemoveComponent<MeshRendererComponent>(e);

    EXPECT_FALSE(world_.HasComponent<TransformComponent>(e));
    EXPECT_FALSE(world_.HasComponent<NameComponent>(e));
    EXPECT_FALSE(world_.HasComponent<MeshRendererComponent>(e));
}

// Test query after multiple operations
TEST_F(EdgeCaseTest, QueryAfterMultipleOperations) {
    // Create entities with different component combinations
    Entity e1 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<NameComponent>(e1, "E1");

    Entity e2 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e2);
    world_.AddComponent<NameComponent>(e2, "E2");
    world_.AddComponent<MeshRendererComponent>(e2);

    Entity e3 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e3);
    world_.AddComponent<MeshRendererComponent>(e3);

    // Query for Transform + Name
    auto withTransformName = world_.QueryEntitiesWith<TransformComponent, NameComponent>();
    EXPECT_EQ(withTransformName.size(), 2u);

    // Query for Transform + MeshRenderer
    auto withTransformMesh = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
    EXPECT_EQ(withTransformMesh.size(), 2u);

    // Remove NameComponent from e2
    world_.RemoveComponent<NameComponent>(e2);

    // Query again
    withTransformName = world_.QueryEntitiesWith<TransformComponent, NameComponent>();
    EXPECT_EQ(withTransformName.size(), 1u);  // Only e1 now

    withTransformMesh = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
    EXPECT_EQ(withTransformMesh.size(), 2u);  // e2 and e3
}

// Test entity version increment on reuse
TEST_F(EdgeCaseTest, EntityVersionIncrementOnReuse) {
    // Create and destroy entity
    Entity e1 = world_.CreateEntity();
    uint64_t id1 = e1.id;
    uint16_t version1 = e1.version;

    world_.DestroyEntity(e1);

    // Create new entity (should reuse the ID)
    Entity e2 = world_.CreateEntity();
    uint64_t id2 = e2.id;
    uint16_t version2 = e2.version;

    // ID should be reused but version incremented
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(version2, version1 + 1);

    // Old entity should be invalid
    EXPECT_FALSE(world_.IsEntityValid(e1));

    // New entity should be valid
    EXPECT_TRUE(world_.IsEntityValid(e2));
}

// Test query with no matches
TEST_F(EdgeCaseTest, QueryWithNoMatches) {
    // Create entity with only Transform
    Entity e1 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e1);

    // Create entity with only Name
    Entity e2 = world_.CreateEntity();
    world_.AddComponent<NameComponent>(e2, "Test");

    // Query for Transform + Name (should return empty)
    auto result = world_.QueryEntitiesWith<TransformComponent, NameComponent>();
    EXPECT_EQ(result.size(), 0u);

    // Query for Transform + MeshRenderer (should also return empty)
    result = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
    EXPECT_EQ(result.size(), 0u);
}

// Test system with destroyed entities
TEST_F(EdgeCaseTest, SystemWithDestroyedEntities) {
    // Create entities
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();
    Entity e3 = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<TransformComponent>(e2);
    world_.AddComponent<TransformComponent>(e3);

    // Register system that tracks entities
    class TrackingSystem : public System {
    public:
        std::vector<Entity> seenEntities;

        void Update(float deltaTime) override {
            seenEntities = world_->QueryEntitiesWith<TransformComponent>();
        }

        const char* GetName() const override { return "TrackingSystem"; }
    };

    TrackingSystem system;
    world_.RegisterSystem(&system);

    // Update - should see all 3 entities
    world_.Update(0.016f);
    EXPECT_EQ(system.seenEntities.size(), 3u);

    // Destroy e2
    world_.DestroyEntity(e2);

    // Update again - should only see 2 entities
    world_.Update(0.016f);
    EXPECT_EQ(system.seenEntities.size(), 2u);

    bool foundE1 = std::find(system.seenEntities.begin(), system.seenEntities.end(), e1) != system.seenEntities.end();
    bool foundE3 = std::find(system.seenEntities.begin(), system.seenEntities.end(), e3) != system.seenEntities.end();

    EXPECT_TRUE(foundE1);
    EXPECT_TRUE(foundE3);

    // Cleanup
    world_.UnregisterSystem(&system);
}

} // namespace testing
} // namespace Next
