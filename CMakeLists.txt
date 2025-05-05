# Setting minimum CMake version for modern features and compatibility
cmake_minimum_required(VERSION 3.20)

# Enforcing C++20 standard for modern C++ features
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Defining project metadata
project(Luminoveau
    VERSION 0.0.1
    DESCRIPTION "Luminoveau Game Engine Library"
    LANGUAGES CXX
)

# Configurable build options
option(LUMINOVEAU_BUILD_IMGUI "Include ImGui support" OFF)
option(LUMINOVEAU_ENABLE_LOGGING "Include logging support" ON)
option(LUMINOVEAU_ENABLE_WARNINGS "Enable all compiler warnings" OFF)
option(LUMINOVEAU_BUILD_TESTS "Build test suite" OFF)

# Setting build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Including source and header file lists
include(cmake/Sources.cmake)

# Adding the main library
add_library(luminoveau STATIC ${LUMINOVEAU_SOURCES})

# Conditionally adding logging support
if(LUMINOVEAU_ENABLE_LOGGING)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/log/loghandler.cpp")
        target_sources(luminoveau PRIVATE
            log/loghandler.cpp
            log/loghandler.h
        )
        message(STATUS "Luminoveau: Logging support enabled")
    else()
        message(WARNING "Logging support requested but loghandler files not found")
    endif()
endif()

# Setting include directories
target_include_directories(luminoveau PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/extern"
)

# Compiler-specific options
if(MSVC)
    target_compile_options(luminoveau PRIVATE
        /W4              # Enable high warning level
        /wd4820          # Suppress padding warning
        /wd4514          # Suppress unreferenced inline function warning
        /wd5045          # Suppress Spectre mitigation warning
    )
else()
    target_compile_options(luminoveau PRIVATE
        -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-function
    )
endif()

# Build-type specific optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(luminoveau PRIVATE -O3)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(luminoveau PRIVATE -g -O0)
endif()

# Platform-specific linking options
if(UNIX AND NOT APPLE)
    target_compile_options(luminoveau PUBLIC -static-libstdc++ -static-libgcc)
    target_link_options(luminoveau PUBLIC -static-libstdc++ -static-libgcc)
endif()

# Ensuring C++20 features
target_compile_features(luminoveau PUBLIC cxx_std_20)

# Dependency management with CPM
file(
    DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.5/CPM.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
    EXPECTED_HASH SHA256=c46b876ae3b9f994b4f05a4c15553e0485636862064f1fcc9d8b4f832086bc5d
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

# Including external CMake modules
include(cmake/AssetPreparation.cmake)
include(cmake/SteamDetection.cmake)
include(cmake/DependencySetup.cmake)
include(cmake/ImGuiIntegration.cmake)

# Optional test suite
if(LUMINOVEAU_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation rules
install(TARGETS luminoveau
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(FILES ${LUMINOVEAU_HEADERS}
    DESTINATION include/luminoveau
)

# Generating configuration file for find_package
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LuminoveauConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LuminoveauConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LuminoveauConfig.cmake"
    @ONLY
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LuminoveauConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LuminoveauConfigVersion.cmake"
    DESTINATION lib/cmake/Luminoveau
)