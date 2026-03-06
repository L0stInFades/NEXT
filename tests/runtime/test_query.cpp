#include "next/runtime/query.h"
#include "next/runtime/world.h"
#include "next/runtime/entity.h"
#include "next/runtime/component.h"
#include "next/foundation/logger.h"
#include <gtest/gtest.h>

namespace Next {
namespace testing {

using ::testing::Test;

class QueryTest : public Test {
protected:
    void SetUp() override {
        Logger::Initialize();
    }

    void TearDown() override {
        Logger::Shutdown();
    }

    World world_;
};

// Test query construction
TEST_F(QueryTest, QueryConstruction) {
    Query query(world_);

    SUCCEED();
}

// Test query all with single component type
TEST_F(QueryTest, QueryAllSingleComponent) {
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();
    Entity e3 = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<TransformComponent>(e2);
    // e3 has no TransformComponent

    Query query(world_);
    query.All<TransformComponent>();

    // Note: Query.ForEach is not implemented in the current version
    // This test verifies compilation and basic construction
    SUCCEED();
}

// Test query all with multiple component types
TEST_F(QueryTest, QueryAllMultipleComponents) {
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<NameComponent>(e1);

    world_.AddComponent<TransformComponent>(e2);
    // e2 has no NameComponent

    Query query(world_);
    query.All<TransformComponent, NameComponent>();

    SUCCEED();
}

// Test world query entities with as alternative
TEST_F(QueryTest, WorldQueryEntitiesWithAlternative) {
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();
    Entity e3 = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<NameComponent>(e1);

    world_.AddComponent<TransformComponent>(e2);
    world_.AddComponent<NameComponent>(e2);
    world_.AddComponent<HierarchyComponent>(e2);

    world_.AddComponent<TransformComponent>(e3);

    // Query entities with both TransformComponent and NameComponent
    auto results = world_.QueryEntitiesWith<TransformComponent, NameComponent>();

    EXPECT_EQ(results.size(), 2u);

    // e1 and e2 should be in results, e3 should not
    bool hasE1 = std::find(results.begin(), results.end(), e1) != results.end();
    bool hasE2 = std::find(results.begin(), results.end(), e2) != results.end();
    bool hasE3 = std::find(results.begin(), results.end(), e3) != results.end();

    EXPECT_TRUE(hasE1);
    EXPECT_TRUE(hasE2);
    EXPECT_FALSE(hasE3);
}

// Test query with no matches
TEST_F(QueryTest, QueryWithNoMatches) {
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<TransformComponent>(e2);

    // Query for entities with NameComponent (none exist)
    auto results = world_.QueryEntitiesWith<NameComponent>();

    EXPECT_EQ(results.size(), 0u);
}

// Test query with all entities matching
TEST_F(QueryTest, QueryAllMatch) {
    const size_t count = 10;

    for (size_t i = 0; i < count; ++i) {
        Entity entity = world_.CreateEntity();
        world_.AddComponent<TransformComponent>(entity);
    }

    auto results = world_.QueryEntitiesWith<TransformComponent>();

    EXPECT_EQ(results.size(), count);
}

// Test query after entity destruction
TEST_F(QueryTest, QueryAfterDestruction) {
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();
    Entity e3 = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<TransformComponent>(e2);
    world_.AddComponent<TransformComponent>(e3);

    // Destroy e2
    world_.DestroyEntity(e2);

    auto results = world_.QueryEntitiesWith<TransformComponent>();

    EXPECT_EQ(results.size(), 2u);

    bool hasE1 = std::find(results.begin(), results.end(), e1) != results.end();
    bool hasE2 = std::find(results.begin(), results.end(), e2) != results.end();
    bool hasE3 = std::find(results.begin(), results.end(), e3) != results.end();

    EXPECT_TRUE(hasE1);
    EXPECT_FALSE(hasE2);
    EXPECT_TRUE(hasE3);
}

// Test query after component removal
TEST_F(QueryTest, QueryAfterComponentRemoval) {
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();

    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<NameComponent>(e1);

    world_.AddComponent<TransformComponent>(e2);
    world_.AddComponent<NameComponent>(e2);

    // Remove NameComponent from e1
    world_.RemoveComponent<NameComponent>(e1);

    auto results = world_.QueryEntitiesWith<TransformComponent, NameComponent>();

    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], e2);
}

// Test complex query
TEST_F(QueryTest, ComplexQuery) {
    // Create entities with various component combinations
    Entity e1 = world_.CreateEntity();  // Transform only
    Entity e2 = world_.CreateEntity();  // Transform + Name
    Entity e3 = world_.CreateEntity();  // Transform + Name + Hierarchy
    Entity e4 = world_.CreateEntity();  // Name only
    Entity e5 = world_.CreateEntity();  // No components

    world_.AddComponent<TransformComponent>(e1);

    world_.AddComponent<TransformComponent>(e2);
    world_.AddComponent<NameComponent>(e2);

    world_.AddComponent<TransformComponent>(e3);
    world_.AddComponent<NameComponent>(e3);
    world_.AddComponent<HierarchyComponent>(e3);

    world_.AddComponent<NameComponent>(e4);

    // Query for entities with Transform and Name
    auto results = world_.QueryEntitiesWith<TransformComponent, NameComponent>();

    EXPECT_EQ(results.size(), 2u);

    bool hasE1 = std::find(results.begin(), results.end(), e1) != results.end();
    bool hasE2 = std::find(results.begin(), results.end(), e2) != results.end();
    bool hasE3 = std::find(results.begin(), results.end(), e3) != results.end();
    bool hasE4 = std::find(results.begin(), results.end(), e4) != results.end();
    bool hasE5 = std::find(results.begin(), results.end(), e5) != results.end();

    EXPECT_FALSE(hasE1);  // Missing NameComponent
    EXPECT_TRUE(hasE2);   // Has both
    EXPECT_TRUE(hasE3);   // Has both
    EXPECT_FALSE(hasE4);  // Missing TransformComponent
    EXPECT_FALSE(hasE5);  // Has no components
}

// Test query performance with many entities
TEST_F(QueryTest, QueryPerformance) {
    const size_t count = 1000;

    // Create 1000 entities
    for (size_t i = 0; i < count; ++i) {
        Entity entity = world_.CreateEntity();
        world_.AddComponent<TransformComponent>(entity);

        // Every 10th entity also has NameComponent
        if (i % 10 == 0) {
            world_.AddComponent<NameComponent>(entity);
        }
    }

    auto start = std::chrono::steady_clock::now();
    auto results = world_.QueryEntitiesWith<TransformComponent, NameComponent>();
    auto end = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Should find 100 entities (every 10th)
    EXPECT_EQ(results.size(), count / 10);

    // Query should be reasonably fast (< 10ms for 1000 entities)
    EXPECT_LT(duration.count(), 10000);
}

} // namespace testing
} // namespace Next
