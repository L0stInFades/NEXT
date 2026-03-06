cmake_minimum_required(VERSION 3.20)
project(NEXTEngine
    VERSION 0.1.0
    DESCRIPTION "NEXT Self-Hosted Engine - Song Dynasty Project"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform detection
if(WIN32)
    set(PLATFORM_WINDOWS ON)
    set(PLATFORM_NAME "Windows")
endif()

# Build options
option(BUILD_TOOLS "Build tools" ON)
option(BUILD_GAME "Build game" ON)
option(BUILD_TESTS "Build tests" ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Windows-specific settings
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # 添加 UTF-8 编码支持,解决中文注释编译问题
    add_compile_options(/utf-8)
endif()

# Engine modules
add_subdirectory(engine/log)                # 统一日志系统
add_subdirectory(engine/serialization)      # 序列化系统
add_subdirectory(engine/platform)
add_subdirectory(engine/foundation)
add_subdirectory(engine/runtime)
add_subdirectory(engine/renderer)
add_subdirectory(engine/profiler)
add_subdirectory(engine/jobsystem)
add_subdirectory(engine/game)

# Optional modules
add_subdirectory(engine/world)     # CP7: World Streaming
add_subdirectory(engine/script)    # CP8: Script System
add_subdirectory(engine/task)      # CP9: Task System
# add_subdirectory(engine/physics)
# add_subdirectory(engine/animation)
# add_subdirectory(engine/ai)
# add_subdirectory(engine/gameplay)

# Tools
if(BUILD_TOOLS)
    add_subdirectory(third_party/imgui)
    add_subdirectory(tools/assetc)
    add_subdirectory(tools/editor)
endif()

# Game
if(BUILD_GAME)
    add_subdirectory(game/song)
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    include(FetchContent)

    # Fetch Google Test
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    # For Windows: Prevent overriding parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
endif()
