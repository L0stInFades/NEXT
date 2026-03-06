add_library(next_jobsystem STATIC
    src/job_system.cpp
)

target_include_directories(next_jobsystem
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(next_jobsystem
    PUBLIC
        next_foundation
        next_profiler
)
