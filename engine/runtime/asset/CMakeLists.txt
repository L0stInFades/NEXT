# Asset Runtime module for CP3

add_library(next_asset STATIC
    src/asset_handle.cpp
    src/asset_manager.cpp
    src/package_container.cpp
    src/asset_types.cpp
)

target_include_directories(next_asset
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(next_asset PUBLIC next_foundation next_jobsystem)
