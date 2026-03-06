# Song Dynasty Game - CP0 Demo

add_executable(song_demo
    src/main.cpp
    src/game.cpp
)

target_include_directories(song_demo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(song_demo PRIVATE
    next_platform
    next_foundation
    next_runtime
    next_renderer
    next_profiler
    next_jobsystem
    next_asset
    next_world
)
