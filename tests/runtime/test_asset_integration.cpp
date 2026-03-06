#include "next/runtime/world.h"
#include "next/runtime/entity.h"
#include "next/runtime/component.h"
#include "next/runtime/asset/asset_manager.h"
#include "next/runtime/asset/asset_types.h"
#include "next/foundation/logger.h"
#include <gtest/gtest.h>

namespace Next {
namespace testing {

using ::testing::Test;

// Integration test for Asset Pipeline + ECS
class AssetIntegrationTest : public Test {
protected:
    void SetUp() override {
        Logger::Initialize();

        // Initialize Asset Manager
        AssetManager::Instance().Initialize();
    }

    void TearDown() override {
        // Shutdown Asset Manager
        AssetManager::Instance().Shutdown();

        Logger::Shutdown();
    }

    World world_;
};

// Test entity with mesh renderer component
TEST_F(AssetIntegrationTest, EntityWithMeshRenderer) {
    // Create entity
    Entity entity = world_.CreateEntity();
    EXPECT_TRUE(world_.IsEntityValid(entity));

    // Add transform component
    auto& transform = world_.AddComponent<TransformComponent>(entity);
    transform.position[0] = 10.0f;
    transform.position[1] = 20.0f;
    transform.position[2] = 30.0f;

    // Add mesh renderer component
    auto& renderer = world_.AddComponent<MeshRendererComponent>(entity);

    // Set asset handles (using invalid handles for testing)
    renderer.mesh = MeshHandle();
    renderer.material = MaterialHandle();
    renderer.submeshIndex = 0;
    renderer.castShadows = true;
    renderer.receiveShadows = false;

    // Verify components were added
    EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));
    EXPECT_TRUE(world_.HasComponent<MeshRendererComponent>(entity));

    // Verify transform data
    auto* transformPtr = world_.GetComponent<TransformComponent>(entity);
    ASSERT_NE(transformPtr, nullptr);
    EXPECT_FLOAT_EQ(transformPtr->position[0], 10.0f);
    EXPECT_FLOAT_EQ(transformPtr->position[1], 20.0f);
    EXPECT_FLOAT_EQ(transformPtr->position[2], 30.0f);

    // Verify renderer data
    auto* rendererPtr = world_.GetComponent<MeshRendererComponent>(entity);
    ASSERT_NE(rendererPtr, nullptr);
    EXPECT_EQ(rendererPtr->submeshIndex, 0);
    EXPECT_TRUE(rendererPtr->castShadows);
    EXPECT_FALSE(rendererPtr->receiveShadows);
}

// Test query for renderable entities
TEST_F(AssetIntegrationTest, QueryRenderableEntities) {
    // Create renderable entity
    Entity e1 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<MeshRendererComponent>(e1);

    // Create non-renderable entity (only transform)
    Entity e2 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e2);

    // Create another renderable entity
    Entity e3 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e3);
    world_.AddComponent<MeshRendererComponent>(e3);

    // Create entity with only mesh renderer (no transform)
    Entity e4 = world_.CreateEntity();
    world_.AddComponent<MeshRendererComponent>(e4);

    // Query for renderable entities (transform + mesh renderer)
    auto renderables = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();

    // Should find exactly 2 entities (e1 and e3)
    EXPECT_EQ(renderables.size(), 2u);

    // Verify e1 is in results
    bool foundE1 = std::find(renderables.begin(), renderables.end(), e1) != renderables.end();
    EXPECT_TRUE(foundE1);

    // Verify e3 is in results
    bool foundE3 = std::find(renderables.begin(), renderables.end(), e3) != renderables.end();
    EXPECT_TRUE(foundE3);

    // Verify e2 and e4 are NOT in results
    bool foundE2 = std::find(renderables.begin(), renderables.end(), e2) != renderables.end();
    EXPECT_FALSE(foundE2);

    bool foundE4 = std::find(renderables.begin(), renderables.end(), e4) != renderables.end();
    EXPECT_FALSE(foundE4);
}

// Test entity hierarchy with assets
TEST_F(AssetIntegrationTest, EntityHierarchyWithAssets) {
    // Create parent entity with mesh
    Entity parent = world_.CreateEntity();
    world_.AddComponent<NameComponent>(parent, "Parent");
    auto& parentTransform = world_.AddComponent<TransformComponent>(parent);
    parentTransform.position[0] = 0.0f;
    parentTransform.position[1] = 0.0f;
    parentTransform.position[2] = 0.0f;

    auto& parentRenderer = world_.AddComponent<MeshRendererComponent>(parent);
    parentRenderer.submeshIndex = 0;

    // Create child entity with mesh
    Entity child = world_.CreateEntity();
    world_.AddComponent<NameComponent>(child, "Child");
    auto& childTransform = world_.AddComponent<TransformComponent>(child);
    childTransform.position[0] = 1.0f;
    childTransform.position[1] = 0.0f;
    childTransform.position[2] = 0.0f;
    childTransform.parent = parent;

    auto& childRenderer = world_.AddComponent<MeshRendererComponent>(child);
    childRenderer.submeshIndex = 1;

    // Verify hierarchy
    auto* childTransformPtr = world_.GetComponent<TransformComponent>(child);
    ASSERT_NE(childTransformPtr, nullptr);
    EXPECT_EQ(childTransformPtr->parent, parent);

    // Query all entities with transform and mesh renderer
    auto renderables = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();

    // Should find both parent and child
    EXPECT_EQ(renderables.size(), 2u);

    bool foundParent = std::find(renderables.begin(), renderables.end(), parent) != renderables.end();
    bool foundChild = std::find(renderables.begin(), renderables.end(), child) != renderables.end();

    EXPECT_TRUE(foundParent);
    EXPECT_TRUE(foundChild);
}

// Test component removal with asset references
TEST_F(AssetIntegrationTest, ComponentRemovalWithAssets) {
    // Create entity with mesh renderer
    Entity entity = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(entity);
    world_.AddComponent<MeshRendererComponent>(entity);

    // Verify components exist
    EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));
    EXPECT_TRUE(world_.HasComponent<MeshRendererComponent>(entity));

    // Remove mesh renderer component
    world_.RemoveComponent<MeshRendererComponent>(entity);

    // Verify mesh renderer was removed but transform remains
    EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));
    EXPECT_FALSE(world_.HasComponent<MeshRendererComponent>(entity));

    // Query for renderable entities should now be empty
    auto renderables = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
    EXPECT_EQ(renderables.size(), 0u);
}

// Test entity destruction with assets
TEST_F(AssetIntegrationTest, EntityDestructionWithAssets) {
    // Create entities with assets
    Entity e1 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<MeshRendererComponent>(e1);

    Entity e2 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e2);
    world_.AddComponent<MeshRendererComponent>(e2);

    // Verify both exist
    EXPECT_TRUE(world_.IsEntityValid(e1));
    EXPECT_TRUE(world_.IsEntityValid(e2));

    auto renderables = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
    EXPECT_EQ(renderables.size(), 2u);

    // Destroy one entity
    world_.DestroyEntity(e1);

    // Verify e1 is destroyed, e2 still exists
    EXPECT_FALSE(world_.IsEntityValid(e1));
    EXPECT_TRUE(world_.IsEntityValid(e2));

    // Query should now return only e2
    renderables = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
    EXPECT_EQ(renderables.size(), 1u);

    bool foundE2 = std::find(renderables.begin(), renderables.end(), e2) != renderables.end();
    EXPECT_TRUE(foundE2);
}

// Test multiple entities with same asset
TEST_F(AssetIntegrationTest, MultipleEntitiesWithSameAsset) {
    // Create multiple entities sharing the same mesh
    const int entityCount = 10;
    std::vector<Entity> entities;

    for (int i = 0; i < entityCount; ++i) {
        Entity e = world_.CreateEntity();
        world_.AddComponent<TransformComponent>(e);
        auto& renderer = world_.AddComponent<MeshRendererComponent>(e);

        // All entities reference the same mesh asset
        renderer.mesh = MeshHandle();
        renderer.submeshIndex = 0;

        // Position entities differently
        auto* transform = world_.GetComponent<TransformComponent>(e);
        transform->position[0] = static_cast<float>(i) * 2.0f;

        entities.push_back(e);
    }

    // Verify all entities created
    EXPECT_EQ(world_.GetEntityCount(), static_cast<size_t>(entityCount));

    // Query all renderable entities
    auto renderables = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
    EXPECT_EQ(renderables.size(), static_cast<size_t>(entityCount));

    // Verify each entity has unique transform
    for (int i = 0; i < entityCount; ++i) {
        auto* transform = world_.GetComponent<TransformComponent>(entities[i]);
        ASSERT_NE(transform, nullptr);
        EXPECT_FLOAT_EQ(transform->position[0], static_cast<float>(i) * 2.0f);
    }
}

// Test world update with renderable entities
TEST_F(AssetIntegrationTest, WorldUpdateWithRenderables) {
    // Create renderable entity
    Entity entity = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(entity);
    world_.AddComponent<MeshRendererComponent>(entity);

    // Register a test system
    class TestRenderSystem : public System {
    public:
        int updateCount = 0;
        std::vector<Entity> renderables;

        void Update(float deltaTime) override {
            updateCount++;
            // Query for renderable entities
            renderables = world_->QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
        }

        const char* GetName() const override { return "TestRenderSystem"; }
    };

    TestRenderSystem system;
    world_.RegisterSystem(&system);

    // Update world
    world_.Update(0.016f);

    // Verify system was updated
    EXPECT_EQ(system.updateCount, 1);

    // Verify system found renderable entities
    EXPECT_EQ(system.renderables.size(), 1u);
    EXPECT_EQ(system.renderables[0], entity);

    // Unregister system before world destruction
    world_.UnregisterSystem(&system);
}

} // namespace testing
} // namespace Next
