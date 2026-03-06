#include "next/runtime/entity.h"
#include <gtest/gtest.h>

namespace Next {
namespace testing {

using ::testing::Test;

class EntityTest : public Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test default entity is invalid
TEST_F(EntityTest, DefaultConstructedIsInvalid) {
    Entity entity;
    EXPECT_FALSE(entity.IsValid());
    EXPECT_EQ(entity.id, 0u);
    EXPECT_EQ(entity.version, 0u);
}

// Test entity construction with ID
TEST_F(EntityTest, ConstructedWithID) {
    Entity entity(42, 1);
    EXPECT_TRUE(entity.IsValid());
    EXPECT_EQ(entity.id, 42u);
    EXPECT_EQ(entity.version, 1u);
}

// Test entity equality
TEST_F(EntityTest, Equality) {
    Entity e1(42, 1);
    Entity e2(42, 1);
    Entity e3(42, 2);
    Entity e4(43, 1);

    EXPECT_EQ(e1, e2);
    EXPECT_NE(e1, e3);
    EXPECT_NE(e1, e4);
}

// Test entity inequality
TEST_F(EntityTest, Inequality) {
    Entity e1(42, 1);
    Entity e2(42, 2);
    Entity e3(43, 1);

    EXPECT_NE(e1, e2);
    EXPECT_NE(e1, e3);
    EXPECT_NE(e2, e3);
}

// Test entity conversion to uint64_t
TEST_F(EntityTest, ConversionToUint64) {
    Entity entity(0x1234, 0xABCD);

    uint64_t value = static_cast<uint64_t>(entity);

    // Value should be version << 48 | id
    uint64_t expected = (0xABCDULL << 48) | 0x1234ULL;
    EXPECT_EQ(value, expected);
}

// Test invalid entity static method
TEST_F(EntityTest, InvalidStatic) {
    Entity entity = Entity::Invalid();

    EXPECT_FALSE(entity.IsValid());
    EXPECT_EQ(entity.id, 0u);
    EXPECT_EQ(entity.version, 0u);
}

// Test entity with zero ID
TEST_F(EntityTest, ZeroIDIsInvalid) {
    Entity entity(0, 1);
    EXPECT_FALSE(entity.IsValid());
}

// Test entity with zero version
TEST_F(EntityTest, ZeroVersionIsInvalid) {
    Entity entity(42, 0);
    EXPECT_FALSE(entity.IsValid());
}

// Test INVALID_ENTITY constant
TEST_F(EntityTest, InvalidEntityConstant) {
    EXPECT_EQ(INVALID_ENTITY.id, 0u);
    EXPECT_EQ(INVALID_ENTITY.version, 0u);
    EXPECT_FALSE(INVALID_ENTITY.IsValid());
}

// Test entity ID limit (48 bits)
TEST_F(EntityTest, EntityIDLimit) {
    const uint64_t maxID = (1ULL << 48) - 1;

    Entity entity(maxID, 1);

    EXPECT_EQ(entity.id, maxID);
    EXPECT_TRUE(entity.IsValid());
}

// Test entity version limit (16 bits)
TEST_F(EntityTest, EntityVersionLimit) {
    const uint64_t maxVersion = (1ULL << 16) - 1;

    Entity entity(42, maxVersion);

    EXPECT_EQ(entity.version, maxVersion);
    EXPECT_TRUE(entity.IsValid());
}

// Test entity hash
TEST_F(EntityTest, EntityHash) {
    EntityHash hash;

    Entity e1(42, 1);
    Entity e2(42, 1);
    Entity e3(42, 2);

    size_t h1 = hash(e1);
    size_t h2 = hash(e2);
    size_t h3 = hash(e3);

    // Same entities should have same hash
    EXPECT_EQ(h1, h2);

    // Different entities (different version) should have different hash
    EXPECT_NE(h1, h3);
}

// Test entity in unordered_map
TEST_F(EntityTest, EntityInUnorderedMap) {
    std::unordered_map<Entity, int, EntityHash> map;

    Entity e1(1, 1);
    Entity e2(2, 1);
    Entity e3(1, 1);  // Same as e1

    map[e1] = 100;
    map[e2] = 200;

    EXPECT_EQ(map[e1], 100);
    EXPECT_EQ(map[e2], 200);
    EXPECT_EQ(map[e3], 100);  // e3 is same as e1

    EXPECT_EQ(map.size(), 2u);
}

// Test multiple entities
TEST_F(EntityTest, MultipleEntities) {
    std::vector<Entity> entities;

    for (uint64_t i = 1; i <= 100; ++i) {
        entities.push_back(Entity(i, 1));
    }

    for (size_t i = 0; i < entities.size(); ++i) {
        EXPECT_TRUE(entities[i].IsValid());
        EXPECT_EQ(entities[i].id, i + 1);
        EXPECT_EQ(entities[i].version, 1u);
    }
}

// Test entity copy
TEST_F(EntityTest, EntityCopy) {
    Entity e1(42, 1);
    Entity e2 = e1;

    EXPECT_EQ(e1, e2);
    EXPECT_EQ(e1.id, e2.id);
    EXPECT_EQ(e1.version, e2.version);
}

// Test entity self-comparison
TEST_F(EntityTest, EntitySelfComparison) {
    Entity entity(42, 1);

    EXPECT_EQ(entity, entity);
    EXPECT_FALSE(entity != entity);
}

} // namespace testing
} // namespace Next
