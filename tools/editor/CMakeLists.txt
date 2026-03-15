cmake_minimum_required(VERSION 3.20)

project(next_editor LANGUAGES CXX)

add_executable(next_editor
    src/main.cpp
)

target_compile_features(next_editor PRIVATE cxx_std_17)

target_link_libraries(next_editor PRIVATE
    imgui
    next_platform
    next_foundation
    next_renderer
    next_jobsystem
    next_asset
    next_runtime
    shell32
)

# Editor "Import" panel shells out to next_assetc.exe; ensure it's built alongside.
add_dependencies(next_editor next_assetc)

if (MSVC)
    # Keep VS debugging rooted at the repo for convenience; runtime asset lookup also works outside VS.
    set_property(TARGET next_editor PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
