# n3ds_probe — standalone toolchain bring-up probe for the Luminoveau 3DS port.
# NOT wired into the engine build. Builds a .3dsx that answers the M0 questions:
#
#   1. Does the engine's pinned SDL3 build under devkitARM and open a window?
#   2. Do std::thread / std::mutex / std::condition_variable exist (gthreads)?
#   3. Does miniaudio compile and init with a null device?
#   4. Does std::filesystem work over romfs:/?
#   5. Can an SDL3 window and raw citro3d rendering coexist in one binary?
#
# Build (from tools/n3ds_probe, inside devkitPro msys2):
#   cmake -S . -B build -G "Unix Makefiles" \
#       -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j$(nproc)
# Then run build/n3ds_probe.3dsx in Azahar or via `3dslink build/n3ds_probe.3dsx`.
#
# Per-probe compile toggles (all default ON; turn one OFF to get past a failure
# and keep collecting the other answers):
#   -DPROBE_THREADS=OFF -DPROBE_MINIAUDIO=OFF -DPROBE_FILESYSTEM=OFF -DPROBE_C3D=OFF

cmake_minimum_required(VERSION 3.20)
project(n3ds_probe LANGUAGES C CXX)

if(NOT NINTENDO_3DS)
    message(FATAL_ERROR "n3ds_probe must be built with the devkitPro 3DS toolchain "
        "(-DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PROBE_THREADS    "Compile the std::thread probe"    ON)
option(PROBE_MINIAUDIO  "Compile the miniaudio probe"      ON)
option(PROBE_FILESYSTEM "Compile the std::filesystem probe" ON)
option(PROBE_C3D        "Compile the citro3d triangle probe" ON)

# ── SDL3, same pin + shape as the engine (cmake/DependencySetup.cmake) ────────
file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.5/CPM.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
    EXPECTED_HASH SHA256=c46b876ae3b9f994b4f05a4c15553e0485636862064f1fcc9d8b4f832086bc5d
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)

CPMAddPackage(
    NAME SDL3
    GITHUB_REPOSITORY libsdl-org/SDL
    GIT_TAG a962f40
    EXCLUDE_FROM_ALL YES
)

add_executable(n3ds_probe source/main.cpp)
target_link_libraries(n3ds_probe PRIVATE SDL3::SDL3-static)

foreach(_p THREADS MINIAUDIO FILESYSTEM C3D)
    if(PROBE_${_p})
        target_compile_definitions(n3ds_probe PRIVATE PROBE_${_p}=1)
    endif()
endforeach()

if(PROBE_MINIAUDIO)
    # The engine's exact miniaudio header (implementation compiled in main.cpp).
    target_include_directories(n3ds_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/extern)
endif()

if(PROBE_C3D)
    target_link_libraries(n3ds_probe PRIVATE citro3d)
    ctr_add_shader_library(probe_shaders source/vshader.v.pica)
    dkp_add_embedded_binary_library(probe_shbin probe_shaders)
    target_link_libraries(n3ds_probe PRIVATE probe_shbin)
endif()

ctr_generate_smdh(${CMAKE_CURRENT_BINARY_DIR}/n3ds_probe.smdh
    NAME "n3ds_probe"
    DESCRIPTION "Luminoveau 3DS toolchain probe"
    AUTHOR "Luminoveau")
ctr_create_3dsx(n3ds_probe
    SMDH  ${CMAKE_CURRENT_BINARY_DIR}/n3ds_probe.smdh
    ROMFS ${CMAKE_CURRENT_SOURCE_DIR}/romfs)
