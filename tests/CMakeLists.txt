# NEXT Engine Unit Tests
# Using Google Test framework

# Link against required engine libraries
set(TEST_LIBRARIES
    gtest
    gtest_main
    next_platform
    next_foundation
    next_runtime
    next_jobsystem
    next_profiler
)

# Platform-specific libraries
if(WIN32)
    list(APPEND TEST_LIBRARIES
        d3d12
        dxgi
        d3dcompiler
    )
endif()

# Foundation Tests
add_executable(test_foundation
    foundation/test_assert.cpp
    # foundation/test_logger.cpp  # Temporarily disabled due to access issues
)
target_link_libraries(test_foundation PRIVATE ${TEST_LIBRARIES})
add_test(NAME FoundationTest COMMAND test_foundation)

# JobSystem Tests
add_executable(test_jobsystem
    jobsystem/test_job_system.cpp
    jobsystem/test_job.cpp
)
target_link_libraries(test_jobsystem PRIVATE ${TEST_LIBRARIES})
add_test(NAME JobSystemTest COMMAND test_jobsystem)

# Runtime ECS Tests
add_executable(test_runtime
    runtime/test_world.cpp
    runtime/test_entity.cpp
    runtime/test_component.cpp
    runtime/test_query.cpp
    runtime/test_system.cpp
    runtime/test_asset_integration.cpp
    runtime/test_edge_cases.cpp
)
target_link_libraries(test_runtime PRIVATE ${TEST_LIBRARIES})
add_test(NAME RuntimeTest COMMAND test_runtime)

# Platform Tests
add_executable(test_platform
    platform/test_window.cpp
    platform/test_input.cpp
)
target_link_libraries(test_platform PRIVATE ${TEST_LIBRARIES})
add_test(NAME PlatformTest COMMAND test_platform)

# Math Tests
add_executable(test_math
    math/test_math.cpp
)
target_include_directories(test_math PRIVATE ${CMAKE_SOURCE_DIR}/engine/renderer/include)
target_link_libraries(test_math PRIVATE ${TEST_LIBRARIES})
add_test(NAME MathTest COMMAND test_math)

# World Streaming Tests (CP7)
add_executable(test_world_streaming
    world/test_streaming_manager.cpp
    world/test_interest_manager.cpp
)
target_link_libraries(test_world_streaming PRIVATE ${TEST_LIBRARIES} next_world)
add_test(NAME WorldStreamingTest COMMAND test_world_streaming)

# Task System Tests (CP9)
add_executable(test_task_system
    task/test_task_save_load.cpp
)
target_link_libraries(test_task_system PRIVATE ${TEST_LIBRARIES} next_task)
add_test(NAME TaskSystemTest COMMAND test_task_system)

# Script System Tests (CP8)
add_executable(test_script_system
    script/test_script_system.cpp
)
target_link_libraries(test_script_system PRIVATE ${TEST_LIBRARIES} next_script)
add_test(NAME ScriptSystemTest COMMAND test_script_system)

# Run all tests with CTest
set_target_properties(test_foundation test_jobsystem test_runtime test_platform test_math
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)

set_target_properties(test_world_streaming
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)

set_target_properties(test_task_system
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)

set_target_properties(test_script_system
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests
)
