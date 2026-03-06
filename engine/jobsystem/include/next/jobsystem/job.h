#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <atomic>

namespace Next {

namespace detail {
struct JobState;
}

/**
 * @brief Priority levels for job scheduling
 *
 * High: Executed before normal and low priority jobs
 * Normal: Default priority level
 * Low: Executed when no higher priority jobs are available
 */
enum class JobPriority : uint8_t {
    High = 0,
    Normal = 1,
    Low = 2
};

/**
 * @brief Task function type for jobs
 *
 * A callable object (function, lambda, etc.) that performs the job's work.
 */
using JobTask = std::function<void()>;

namespace detail {
struct JobState;
} // namespace detail

/**
 * @brief Handle to a submitted job
 *
 * Used to track, wait for, and cancel jobs submitted to the JobSystem.
 * Jobs are reference counted and automatically cleaned up when no longer needed.
 */
struct JobHandle {
    std::shared_ptr<detail::JobState> state;

    /**
     * @brief Check if this handle references a valid job
     * @return true if the handle is valid, false otherwise
     */
    bool IsValid() const { return static_cast<bool>(state); }

    /**
     * @brief Check if the job has completed execution
     * @return true if completed, false if still pending or running
     */
    bool IsCompleted() const;

    /**
     * @brief Release the job handle
     *
     * Does NOT cancel the job. Just releases this reference to it.
     */
    void Reset() { state.reset(); }
};

} // namespace Next
