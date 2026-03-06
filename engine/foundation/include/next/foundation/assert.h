#pragma once

#include "next/foundation/logger.h"

namespace Next {

/**
 * @brief Internal assertion failure handler
 *
 * Called when an assertion fails. Logs the failure and triggers the debugger.
 *
 * @param condition The expression that failed (as a string)
 * @param file Source file where assertion occurred
 * @param line Line number where assertion occurred
 * @param message Optional custom message
 */
void AssertHandler(const char* condition, const char* file, int line, const char* message = nullptr);

} // namespace Next

/**
 * @brief Debug-only assertion macro
 *
 * Only active in debug builds. If condition is false, logs an error
 * and breaks into the debugger. Optimized out in release builds.
 *
 * @param condition Expression to check
 * @param msg Optional message to display on failure
 */
#ifdef _DEBUG
    #define NEXT_ASSERT(cond, msg) \
        do { \
            if (!(cond)) { \
                ::Next::AssertHandler(#cond, __FILE__, __LINE__, msg); \
                __debugbreak(); \
            } \
        } while(0)

    /**
     * @brief Always-enabled assertion macro
     *
     * Active in both debug and release builds. If condition is false,
     * logs an error and terminates the application.
     *
     * @param condition Expression to check
     * @param msg Optional message to display on failure
     */
    #define NEXT_ALWAYS_ASSERT(cond, msg) \
        do { \
            if (!(cond)) { \
                ::Next::AssertHandler(#cond, __FILE__, __LINE__, msg); \
                __debugbreak(); \
            } \
        } while(0)
#else
    #define NEXT_ASSERT(cond, msg) ((void)0)
    #define NEXT_ALWAYS_ASSERT(cond, msg) \
        do { \
            if (!(cond)) { \
                ::Next::AssertHandler(#cond, __FILE__, __LINE__, msg); \
                std::abort(); \
            } \
        } while(0)
#endif

/**
 * @brief Verification macro for critical operations
 *
 * Alias for NEXT_ALWAYS_ASSERT. Use this to verify the success of
 * critical operations like file I/O, memory allocation, etc.
 *
 * @param condition Expression to verify
 * @param msg Optional message to display on failure
 */
#define NEXT_VERIFY(cond, msg) NEXT_ALWAYS_ASSERT(cond, msg)
