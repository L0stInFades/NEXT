add_library(next_platform STATIC
    src/window.cpp
    src/input.cpp
    src/platform.cpp
)

target_include_directories(next_platform
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_sources(next_platform PRIVATE
        src/windows/window_win32.cpp
        src/windows/input_win32.cpp
        src/windows/platform_win32.cpp
    )
    target_link_libraries(next_platform PUBLIC
        user32
        gdi32
        dbghelp
    )
endif()

if(APPLE)
    target_sources(next_platform PRIVATE
        src/apple/window_cocoa.mm
    )
    set_source_files_properties(src/apple/window_cocoa.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    target_link_libraries(next_platform PUBLIC
        "-framework Cocoa"
        "-framework QuartzCore"
    )
endif()
