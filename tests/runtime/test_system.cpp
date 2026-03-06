#include "next/runtime/system.h"
#include "next/runtime/world.h"
#include "next/runtime/entity.h"
#include "next/runtime/component.h"
#include "next/foundation/logger.h"
#include <gtest/gtest.h>

namespace Next {
namespace testing {

using ::testing::Test;

// Custom test system
class TestSystem : public System {
public:
    int updateCount = 0;
    float totalDeltaTime = 0.0f;
    int entityCreatedCount = 0;
    int entityDestroyedCount = 0;
    int componentAddedCount = 0;
    int componentRemovedCount = 0;
    bool initialized = false;
    bool shutdownCalled = false;

    void Initialize() override {
        initialized = true;
    }

    void Update(float deltaTime) override {
        updateCount++;
        totalDeltaTime += deltaTime;
    }

    void Shutdown() override {
        shutdownCalled = true;
    }

    void OnEntityCreated(Entity entity) override {
        entityCreatedCount++;
    }

    void OnEntityDestroyed(Entity entity) override {
        entityDestroyedCount++;
    }

    void OnComponentAdded(Entity entity, ComponentTypeID type) override {
        componentAddedCount++;
    }

    void OnComponentRemoved(Entity entity, ComponentTypeID type) override {
        componentRemovedCount++;
    }

    const char* GetName() const override {
        return "TestSystem";
    }
};

class SystemTest : public Test {
protected:
    void SetUp() override {
        Logger::Initialize();
    }

    void TearDown() override {
        Logger::Shutdown();
    }

    World world_;
};

// Test system creation
TEST_F(SystemTest, SystemCreation) {
    TestSystem system;

    EXPECT_STREQ(system.GetName(), "TestSystem");
    EXPECT_TRUE(system.IsEnabled());
    EXPECT_EQ(system.updateCount, 0);
}

// Test system enable/disable
TEST_F(SystemTest, SystemEnableDisable) {
    TestSystem system;

    EXPECT_TRUE(system.IsEnabled());

    system.SetEnabled(false);
    EXPECT_FALSE(system.IsEnabled());

    system.SetEnabled(true);
    EXPECT_TRUE(system.IsEnabled());
}

// Test system update
TEST_F(SystemTest, SystemUpdate) {
    TestSystem system;

    system.Update(0.016f);
    system.Update(0.032f);
    system.Update(0.016f);

    EXPECT_EQ(system.updateCount, 3);
    EXPECT_FLOAT_EQ(system.totalDeltaTime, 0.064f);
}

// Test system lifecycle
TEST_F(SystemTest, SystemLifecycle) {
    TestSystem system;

    EXPECT_FALSE(system.initialized);
    EXPECT_FALSE(system.shutdownCalled);

    system.Initialize();
    EXPECT_TRUE(system.initialized);

    system.Update(0.016f);
    EXPECT_EQ(system.updateCount, 1);

    system.Shutdown();
    EXPECT_TRUE(system.shutdownCalled);
}

// Test system registration
TEST_F(SystemTest, SystemRegistration) {
    TestSystem system;

    world_.RegisterSystem(&system);

    // Verify system is registered by updating world
    world_.Update(0.016f);

    // System's Update should be called
    EXPECT_EQ(system.updateCount, 1);
}

// Test system unregistration
TEST_F(SystemTest, SystemUnregistration) {
    TestSystem system;

    world_.RegisterSystem(&system);
    world_.Update(0.016f);

    EXPECT_EQ(system.updateCount, 1);

    world_.UnregisterSystem(&system);
    world_.Update(0.016f);

    // Update count should not increase (system unregistered)
    EXPECT_EQ(system.updateCount, 1);
}

// Test system entity created callback
TEST_F(SystemTest, SystemEntityCreatedCallback) {
    TestSystem system;
    world_.RegisterSystem(&system);

    Entity entity = world_.CreateEntity();

    EXPECT_EQ(system.entityCreatedCount, 1);
}

// Test system entity destroyed callback
TEST_F(SystemTest, SystemEntityDestroyedCallback) {
    TestSystem system;
    world_.RegisterSystem(&system);

    Entity entity = world_.CreateEntity();
    EXPECT_EQ(system.entityCreatedCount, 1);

    world_.DestroyEntity(entity);
    EXPECT_EQ(system.entityDestroyedCount, 1);
}

// Test system component added callback
TEST_F(SystemTest, SystemComponentAddedCallback) {
    TestSystem system;
    world_.RegisterSystem(&system);

    Entity entity = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(entity);

    EXPECT_EQ(system.componentAddedCount, 1);
}

// Test system component removed callback
TEST_F(SystemTest, SystemComponentRemovedCallback) {
    TestSystem system;
    world_.RegisterSystem(&system);

    Entity entity = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(entity);
    EXPECT_EQ(system.componentAddedCount, 1);

    world_.RemoveComponent<TransformComponent>(entity);
    EXPECT_EQ(system.componentRemovedCount, 1);
}

// Test multiple systems
TEST_F(SystemTest, MultipleSystems) {
    TestSystem system1;
    TestSystem system2;
    TestSystem system3;

    world_.RegisterSystem(&system1);
    world_.RegisterSystem(&system2);
    world_.RegisterSystem(&system3);

    world_.Update(0.016f);

    EXPECT_EQ(system1.updateCount, 1);
    EXPECT_EQ(system2.updateCount, 1);
    EXPECT_EQ(system3.updateCount, 1);
}

// Test system receives all entity events
TEST_F(SystemTest, SystemReceivesAllEntityEvents) {
    TestSystem system;
    world_.RegisterSystem(&system);

    // Create and destroy multiple entities
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();
    Entity e3 = world_.CreateEntity();

    EXPECT_EQ(system.entityCreatedCount, 3);

    world_.DestroyEntity(e1);
    world_.DestroyEntity(e2);

    EXPECT_EQ(system.entityDestroyedCount, 2);

    // Add components to e3
    world_.AddComponent<TransformComponent>(e3);
    world_.AddComponent<NameComponent>(e3);

    EXPECT_EQ(system.componentAddedCount, 2);

    // Remove component
    world_.RemoveComponent<TransformComponent>(e3);

    EXPECT_EQ(system.componentRemovedCount, 1);
}

// Test disabled system doesn't update
TEST_F(SystemTest, DisabledSystemDoesntUpdate) {
    TestSystem system;
    system.SetEnabled(false);

    world_.RegisterSystem(&system);
    world_.Update(0.016f);

    // System should not be updated when disabled
    EXPECT_EQ(system.updateCount, 0);
}

// Test system callbacks still work for disabled system
TEST_F(SystemTest, DisabledSystemStillReceivesCallbacks) {
    TestSystem system;
    system.SetEnabled(false);

    world_.RegisterSystem(&system);

    Entity entity = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(entity);

    // Callbacks should still work even if system is disabled
    EXPECT_EQ(system.entityCreatedCount, 1);
    EXPECT_EQ(system.componentAddedCount, 1);
    EXPECT_EQ(system.updateCount, 0);  // But Update should not be called
}

// Test system world pointer
TEST_F(SystemTest, SystemWorldPointer) {
    TestSystem system;

    world_.RegisterSystem(&system);

    // System should have world_ set internally (but it's protected)
    // Just verify the system works correctly
    world_.Update(0.016f);
    EXPECT_EQ(system.updateCount, 1);
}

// Test multiple frame updates
TEST_F(SystemTest, MultipleFrameUpdates) {
    TestSystem system;
    world_.RegisterSystem(&system);

    const int frameCount = 100;
    const float deltaTime = 0.016f;

    for (int i = 0; i < frameCount; ++i) {
        world_.Update(deltaTime);
    }

    EXPECT_EQ(system.updateCount, frameCount);
    EXPECT_FLOAT_EQ(system.totalDeltaTime, frameCount * deltaTime);
}

// Test system order
TEST_F(SystemTest, SystemOrder) {
    struct OrderTestSystem : public System {
        std::vector<int>* order;
        int id;

        OrderTestSystem(std::vector<int>* o, int i) : order(o), id(i) {}

        void Update(float deltaTime) override {
            order->push_back(id);
        }
    };

    std::vector<int> executionOrder;
    OrderTestSystem system1(&executionOrder, 1);
    OrderTestSystem system2(&executionOrder, 2);
    OrderTestSystem system3(&executionOrder, 3);

    // Register in order 1, 2, 3
    world_.RegisterSystem(&system1);
    world_.RegisterSystem(&system2);
    world_.RegisterSystem(&system3);

    world_.Update(0.016f);

    // Systems should execute in registration order
    EXPECT_EQ(executionOrder.size(), 3u);
    EXPECT_EQ(executionOrder[0], 1);
    EXPECT_EQ(executionOrder[1], 2);
    EXPECT_EQ(executionOrder[2], 3);

    // Unregister systems before they go out of scope to prevent dangling pointers
    world_.UnregisterSystem(&system1);
    world_.UnregisterSystem(&system2);
    world_.UnregisterSystem(&system3);
}

} // namespace testing
} // namespace Next
