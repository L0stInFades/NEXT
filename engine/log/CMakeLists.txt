# 统一日志系统
cmake_minimum_required(VERSION 3.20)

project(next_log)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(LOG_SOURCES
    src/log.cpp
)

# 头文件
set(LOG_HEADERS
    include/next/log/log.h
)

# 创建静态库
add_library(next_log STATIC
    ${LOG_SOURCES}
    ${LOG_HEADERS}
)

# 设置包含目录
target_include_directories(next_log PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Windows特定设置
if(WIN32)
    target_compile_definitions(next_log PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    # 添加 UTF-8 编码支持
    target_compile_options(next_log PRIVATE /utf-8)
endif()

# 编译选项
if(MSVC)
    target_compile_options(next_log PRIVATE
        /W4
        /wd4996  # 禁用某些安全警告
    )
else()
    target_compile_options(next_log PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
    )
endif()

# Release优化 - 使用生成器表达式以支持多配置生成器
if(MSVC)
    target_compile_options(next_log PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
    target_link_options(next_log PRIVATE "$<$<CONFIG:Release>:/LTCG>")
else()
    target_compile_options(next_log PRIVATE "$<$<CONFIG:Release>:-O3 -march=native>")
endif()

# 安装
install(TARGETS next_log
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${LOG_HEADERS}
    DESTINATION include/next/log
)
