# Profiler module - CP1

add_library(next_profiler STATIC
    src/profiler.cpp
    src/stats.cpp
    src/cpu_scope.cpp
)

target_include_directories(next_profiler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(next_profiler
    PRIVATE
        next_foundation
)
