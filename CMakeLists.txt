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
option(LUMINOVEAU_BUILD_RMLUI "Include RmlUi support" OFF)
option(LUMINOVEAU_ENABLE_LOGGING "Include logging support" ON)
option(LUMINOVEAU_ENABLE_WARNINGS "Enable all compiler warnings" OFF)
option(LUMINOVEAU_BUILD_TESTS "Build test suite" OFF)

# Setting build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Set output directories for built executables and libraries
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()
if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

# ============================================================================
# PLATFORM-SPECIFIC SETTINGS
# ============================================================================

# Android-specific fixes
if(ANDROID)
    # Remove incompatible linker flags that may be set by dependencies
    string(REPLACE "-fuse-ld=gold" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-fuse-ld=gold" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    string(REPLACE "-fuse-ld=gold" "" CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}")
    string(REPLACE "-fuse-ld=gold" "" CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE}")
    string(REPLACE "-fuse-ld=gold" "" CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}")
    
    # Android uses lld linker
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")
    
    message(STATUS "Android build detected - using lld linker")
endif()

# ============================================================================
# AGGRESSIVE OPTIMIZATION FLAGS
# ============================================================================

# Silence warnings from dependencies
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")

if(MSVC)
    # MSVC Release optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ob2 /Oi /Ot /GL /fp:fast")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /OPT:REF /OPT:ICF")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG /OPT:REF /OPT:ICF")
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG")
    
    # MSVC Debug optimizations (fast but debuggable)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od /Zi /RTC1")
    
    # MSVC RelWithDebInfo (optimized with debug info)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /O2 /Ob1 /Zi /fp:fast")
    
else()
    # GCC/Clang Release optimizations
    if(ANDROID)
        # Android cross-compilation - don't use march=native
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -ffast-math -funroll-loops")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -mtune=native -ffast-math -funroll-loops")
    endif()
    
    # Try to enable LTO if supported
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)
    if(LTO_SUPPORTED)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto")
        set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -flto")
        message(STATUS "LTO enabled for Release build")
    else()
        message(STATUS "LTO not supported, skipping: ${LTO_ERROR}")
    endif()
    
    # GCC/Clang Debug optimizations (O1 for speed, -g for debugging)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O1 -g")
    
    # GCC/Clang RelWithDebInfo
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3 -g -march=native -mtune=native -ffast-math")
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

# Compiler-specific options for Luminoveau library only
if(MSVC)
    target_compile_options(luminoveau PRIVATE
        /W4              # Enable high warning level for our code
        /wd4820          # Suppress padding warning
        /wd4514          # Suppress unreferenced inline function warning
        /wd5045          # Suppress Spectre mitigation warning
    )
else()
    target_compile_options(luminoveau PRIVATE
        -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-function
    )
endif()

# Platform-specific linking options
if(ANDROID)
    # Android-specific settings
    target_compile_definitions(luminoveau PUBLIC __ANDROID__)
    target_link_libraries(luminoveau PUBLIC log android)  # Android system libraries
elseif(UNIX AND NOT APPLE)
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

# Disable warnings for all dependencies
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_WARNING_CMP0077 OFF)
set(CMAKE_POLICY_WARNING_CMP0091 OFF)

# Including external CMake modules
include(cmake/AssetPreparation.cmake)
include(cmake/SteamDetection.cmake)
include(cmake/DependencySetup.cmake)
include(cmake/ImGuiIntegration.cmake)
include(cmake/RmlUiIntegration.cmake)
include(cmake/DownloadDXC.cmake)

# Android: Fix linker flags again after dependencies (they may override)
if(ANDROID)
    string(REPLACE "-fuse-ld=gold" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-fuse-ld=gold" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "-fuse-ld=lld")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
    endif()
    if(NOT CMAKE_SHARED_LINKER_FLAGS MATCHES "-fuse-ld=lld")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")
    endif()
endif()

# Make helper function available to consuming projects
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyRuntimeDLLs.cmake)

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
# Install CMake helper scripts
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyRuntimeDLLs.cmake"
    DESTINATION lib/cmake/Luminoveau
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