cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(MultiAxisMotion)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 添加头文件搜索路径 (当前目录及 Eigen 头文件)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 收集所有的源文件
set(SOURCES
    main.cpp
    axis.cpp
    general_6s.cpp
    probe_detect_tasks.cpp
    motion_control.cpp
    calibration_task.cpp
)

# 生成可执行文件
add_executable(MultiAxisMotion ${SOURCES})

# 链接静态库 libethercat.a 和系统线程库、实时库
target_link_libraries(MultiAxisMotion
    ${CMAKE_CURRENT_SOURCE_DIR}/libethercat.a
    pthread
    rt
)
