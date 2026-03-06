cmake_minimum_required(VERSION 3.20)

# Dear ImGui (docking branch) as a static library.
# Kept minimal: core + Win32/DX12 backends only.
add_library(imgui STATIC)

target_sources(imgui PRIVATE
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    backends/imgui_impl_win32.cpp
    backends/imgui_impl_dx12.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

if (WIN32)
    target_link_libraries(imgui PUBLIC d3d12 dxgi imm32)
endif()
