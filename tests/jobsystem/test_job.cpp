#include "next/jobsystem/job.h"
#include "next/jobsystem/job_system.h"
#include "next/foundation/logger.h"
#include <gtest/gtest.h>

namespace Next {
namespace testing {

using ::testing::Test;

class JobHandleTest : public Test {
protected:
    void SetUp() override {
        Logger::Initialize();
        system_.Initialize(2);
    }

    void TearDown() override {
        system_.Shutdown();
        Logger::Shutdown();
    }

    JobSystem& system_ = JobSystem::Instance();
};

// Test default constructed handle is invalid
TEST_F(JobHandleTest, DefaultConstructed) {
    JobHandle handle;
    EXPECT_FALSE(handle.IsValid());
    EXPECT_FALSE(handle.IsCompleted());
}

// Test valid handle from job submission
TEST_F(JobHandleTest, ValidHandle) {
    JobHandle handle = system_.Submit([]() {
        // Do nothing
    });

    EXPECT_TRUE(handle.IsValid());
}

// Test completed status
TEST_F(JobHandleTest, CompletedStatus) {
    JobHandle handle = system_.Submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });

    // Immediately after submission, likely not completed yet
    // But could be if job finished quickly

    system_.Wait(handle);
    EXPECT_TRUE(handle.IsCompleted());
}

// Test reset
TEST_F(JobHandleTest, Reset) {
    JobHandle handle = system_.Submit([]() {});

    system_.Wait(handle);
    EXPECT_TRUE(handle.IsValid());
    EXPECT_TRUE(handle.IsCompleted());

    handle.Reset();
    EXPECT_FALSE(handle.IsValid());
    EXPECT_FALSE(handle.IsCompleted());
}

// Test handle copy
TEST_F(JobHandleTest, CopyHandle) {
    std::atomic<int> value{0};

    JobHandle handle1 = system_.Submit([&value]() {
        value.store(42);
    });

    JobHandle handle2 = handle1;  // Copy

    system_.Wait(handle1);

    EXPECT_TRUE(handle1.IsValid());
    EXPECT_TRUE(handle2.IsValid());
    EXPECT_TRUE(handle1.IsCompleted());
    EXPECT_TRUE(handle2.IsCompleted());
    EXPECT_EQ(value.load(), 42);
}

// Test handle move
TEST_F(JobHandleTest, MoveHandle) {
    std::atomic<int> value{0};

    JobHandle handle1 = system_.Submit([&value]() {
        value.store(42);
    });

    JobHandle handle2 = std::move(handle1);  // Move

    system_.Wait(handle2);

    EXPECT_FALSE(handle1.IsValid());  // Moved-from
    EXPECT_TRUE(handle2.IsValid());   // Moved-to
    EXPECT_TRUE(handle2.IsCompleted());
    EXPECT_EQ(value.load(), 42);
}

// Test multiple handles to same job
TEST_F(JobHandleTest, MultipleHandlesSameJob) {
    std::atomic<int> value{0};

    JobHandle handle1 = system_.Submit([&value]() {
        value.store(42);
    });

    JobHandle handle2 = handle1;  // Both point to same job

    // Both handles should work
    system_.Wait(handle1);
    EXPECT_TRUE(handle2.IsCompleted());

    EXPECT_EQ(value.load(), 42);
}

// Test priority enum values
TEST(JobHandle, PriorityEnumValues) {
    EXPECT_EQ(static_cast<int>(JobPriority::High), 0);
    EXPECT_EQ(static_cast<int>(JobPriority::Normal), 1);
    EXPECT_EQ(static_cast<int>(JobPriority::Low), 2);
}

// Test job task type
TEST(JobHandle, JobTaskType) {
    // JobTask is std::function<void()>
    // This test verifies it can be constructed from various callables

    // Lambda
    JobTask lambdaTask = []() {};

    // Function pointer
    void (*functionPtr)() = []() {};
    JobTask functionTask = functionPtr;

    // Functor
    struct Functor {
        void operator()() const {}
    };
    JobTask functorTask = Functor();

    // All should be valid (non-empty)
    EXPECT_TRUE(lambdaTask != nullptr);
    EXPECT_TRUE(functionTask != nullptr);
    EXPECT_TRUE(functorTask != nullptr);
}

} // namespace testing
} // namespace Next
