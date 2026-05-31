cmake_minimum_required(VERSION 3.10)

project(RoboXORT CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Source
file(GLOB SOURCES "src/*.cpp")

# Executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Include
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/eigen
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/ethercat
)

# Link
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/ethercat/libethercat.a
    pthread
    rt
)

# Copy Config
file(GLOB CONFIG_FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/*")
foreach(CONFIG_FILE IN LISTS CONFIG_FILES)
    get_filename_component(FILE_NAME "${CONFIG_FILE}" NAME)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CONFIG_FILE}"
        "${CMAKE_CURRENT_BINARY_DIR}/${FILE_NAME}"
        COMMENT "Copying ${FILE_NAME} to build directory..."
    )
endforeach()
