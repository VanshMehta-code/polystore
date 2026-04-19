cmake_minimum_required(VERSION 3.20)
project(database CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -O3 -march=native)
include_directories(include)
file(GLOB_RECURSE SOURCES src/*.cpp)
add_executable(${PROJECT_NAME} ${SOURCES})
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/data)

# Copy your binary output there
add_custom_command(TARGET database POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy 
    $<TARGET_FILE:database> 
    ${CMAKE_SOURCE_DIR}/data/
)
