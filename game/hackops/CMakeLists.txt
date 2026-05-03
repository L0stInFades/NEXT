add_executable(hackops_demo
    src/main.cpp
)

target_compile_features(hackops_demo PRIVATE cxx_std_17)

target_link_libraries(hackops_demo PRIVATE
    next_ops
)

