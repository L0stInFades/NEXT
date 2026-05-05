add_library(next_renderer STATIC
    src/renderer.cpp
)

if(WIN32)
    target_sources(next_renderer PRIVATE
        src/render_graph.cpp
        src/dx12/device.cpp
        src/dx12/fence.cpp
        src/dx12/command_queue.cpp
        src/dx12/command_list.cpp
        src/dx12/swapchain.cpp
        src/dx12/descriptor_heap.cpp
        src/dx12/descriptor_allocator.cpp
        src/dx12/texture.cpp
        src/dx12/sampler.cpp
        src/dx12/material.cpp
        src/dx12/dx12_renderer.cpp
        src/dx12/shader.cpp
        src/dx12/root_signature.cpp
        src/dx12/pipeline_state.cpp
        src/dx12/buffer.cpp
        src/dx12/constant_buffer.cpp
        src/dx12/depth_buffer.cpp
        src/dx12/mesh_shader_pass.cpp
        src/dx12/taa.cpp
        src/dx12/post_processing.cpp
        src/dx12/debug_views.cpp
        src/dx12/ambient_occlusion.cpp
        src/dx12/light_probe.cpp
        src/dx12/global_illumination.cpp
        src/dx12/rendering_stats.cpp
        src/dx12/shader_compiler.cpp
    )
endif()

if(APPLE)
    target_sources(next_renderer PRIVATE
        src/metal/metal_renderer.mm
    )
    set_source_files_properties(src/metal/metal_renderer.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
endif()

target_include_directories(next_renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(next_renderer PUBLIC next_runtime next_platform d3d12 dxgi d3dcompiler)
elseif(APPLE)
    target_link_libraries(next_renderer PUBLIC
        next_runtime
        next_platform
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
    )
else()
    target_link_libraries(next_renderer PUBLIC next_runtime next_platform)
endif()
