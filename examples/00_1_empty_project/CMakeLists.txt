# Standalone build file for a copied-out Luminoveau project.
#
# This file is only used when you copy 00_1_empty_project somewhere on its own. Inside the engine
# repo the examples are built by examples/CMakeLists.txt instead, which ignores this file.
#
# Setup:
#   1. Copy this directory somewhere outside the engine repo and rename it.
#   2. Put the Luminoveau engine next to your main.cpp as a subfolder named "luminoveau"
#      (git clone or submodule), or pass -DLUMINOVEAU_DIR=/path/to/luminoveau when configuring.
#   3. cmake -B build && cmake --build build

cmake_minimum_required(VERSION 3.22)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(my_lumi_project CXX)
set(CMAKE_CXX_STANDARD 17)

# Where the Luminoveau engine source lives.
set(LUMINOVEAU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/luminoveau"
    CACHE PATH "Path to the Luminoveau engine source")

# The AppInit/AppIterate callback pattern is bridged to SDL3's main loop by the engine's
# lumi_main.cpp, which is only compiled into the executable when this is on.
set(LUMINOVEAU_USE_CALLBACKS ON)

add_executable(${PROJECT_NAME} main.cpp)

add_subdirectory(${LUMINOVEAU_DIR} luminoveau-build)
target_link_libraries(${PROJECT_NAME} PRIVATE luminoveau)
if(NOT EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++)
endif()

# Copy assets next to the binary, if this project has any.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets")
endif()
