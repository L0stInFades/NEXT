#include "next/foundation/assert.h"
#include "next/foundation/logger.h"
#include <gtest/gtest.h>
#include <cstdlib>

namespace Next {
namespace testing {

using ::testing::Test;

class AssertTest : public Test {
protected:
    void SetUp() override {
        Logger::Initialize();
    }

    void TearDown() override {
        Logger::Shutdown();
    }
};

// Test that true assertions don't trigger
TEST_F(AssertTest, TrueAssertionPasses) {
    // In release build, NEXT_ASSERT is a no-op
    // In debug build, it should not trigger for true conditions
    NEXT_ASSERT(true, "This should not trigger");
    NEXT_ASSERT(1 == 1, "Math is working");
    NEXT_ASSERT(42 > 0, "Positive number");
    SUCCEED();
}

// Test NEXT_ALWAYS_ASSERT with true condition
TEST_F(AssertTest, AlwaysAssertTruePasses) {
    NEXT_ALWAYS_ASSERT(true, "This should not trigger");
    NEXT_ALWAYS_ASSERT(1 + 1 == 2, "Addition works");
    SUCCEED();
}

// Test pointer validation
TEST_F(AssertTest, PointerAssertions) {
    int value = 42;
    int* ptr = &value;

    NEXT_ASSERT(ptr != nullptr, "Pointer should not be null");
    NEXT_ASSERT(*ptr == 42, "Pointer value should be 42");

    SUCCEED();
}

// Test arithmetic assertions
TEST_F(AssertTest, ArithmeticAssertions) {
    int a = 10;
    int b = 20;

    NEXT_ASSERT(a + b == 30, "10 + 20 = 30");
    NEXT_ASSERT(b - a == 10, "20 - 10 = 10");
    NEXT_ASSERT(a * b == 200, "10 * 20 = 200");
    NEXT_ASSERT(b / a == 2, "20 / 10 = 2");

    SUCCEED();
}

// Test string assertions (using pointers)
TEST_F(AssertTest, StringAssertions) {
    const char* str = "Hello, World!";
    NEXT_ASSERT(str != nullptr, "String should not be null");
    NEXT_ASSERT(str[0] == 'H', "First character should be H");

    SUCCEED();
}

// Test array assertions
TEST_F(AssertTest, ArrayAssertions) {
    int arr[5] = {1, 2, 3, 4, 5};
    NEXT_ASSERT(arr[0] == 1, "First element is 1");
    NEXT_ASSERT(arr[4] == 5, "Last element is 5");
    NEXT_ASSERT(arr[2] == 3, "Middle element is 3");

    SUCCEED();
}

// Test compound conditions
TEST_F(AssertTest, CompoundConditions) {
    int x = 5;
    int y = 10;

    NEXT_ASSERT(x > 0 && y > 0, "Both should be positive");
    NEXT_ASSERT(x < y || x == y, "x should be less than or equal to y");

    SUCCEED();
}

// Test floating point comparisons
TEST_F(AssertTest, FloatAssertions) {
    float a = 1.0f;
    float b = 1.0f;

    NEXT_ASSERT(a == b, "Equal floats");
    NEXT_ASSERT(a > 0.0f, "Positive float");

    SUCCEED();
}

// Test multiple assertions in sequence
TEST_F(AssertTest, MultipleAssertions) {
    NEXT_ASSERT(true, "First");
    NEXT_ASSERT(true, "Second");
    NEXT_ASSERT(true, "Third");
    NEXT_ASSERT(true, "Fourth");
    NEXT_ASSERT(true, "Fifth");

    SUCCEED();
}

// Test NEXT_VERIFY macro (alias for NEXT_ALWAYS_ASSERT)
TEST_F(AssertTest, VerifyMacro) {
    NEXT_VERIFY(true, "Verify should work like always assert");
    NEXT_VERIFY(1 == 1, "Verify basic equality");

    SUCCEED();
}

} // namespace testing
} // namespace Next

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
