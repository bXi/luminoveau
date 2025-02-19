﻿cmake_minimum_required(VERSION 3.12)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project("luminoveau" VERSION 0.0.1 DESCRIPTION "Luminoveau")

option(ADD_IMGUI "Include ImGui" OFF)

# Add the content from the provided "engine.cmake" file
add_library("luminoveau" STATIC
    luminoveau.h

    audio/audiohandler.cpp
    audio/audiohandler.h

    enginestate/enginestate.h

    text/texthandler.cpp
    text/texthandler.h

    eventbus/eventbushandler.cpp
    eventbus/eventbushandler.h

    input/inputconstants.h
    input/inputdevice.cpp
    input/inputdevice.h
    input/inputhandler.cpp
    input/inputhandler.h

    draw/drawhandler.cpp
    draw/drawhandler.h

    settings/mini.h
    settings/settingshandler.cpp
    settings/settingshandler.h

    state/basestate.h
    state/state.h
    state/state.cpp

    assethandler/assethandler.cpp
    assethandler/assethandler.h
    assethandler/DroidSansMono.cpp
    assethandler/spritefrag.cpp
    assethandler/spritevert.cpp

    renderer/sdl_gpu_structs.cpp
    renderer/sdl_gpu_structs.h
    renderer/rendererhandler.cpp
    renderer/rendererhandler.h
    renderer/renderpass.h
    renderer/spriterenderpass.cpp
    renderer/spriterenderpass.h
    renderer/shaderrenderpass.cpp
    renderer/shaderrenderpass.h
    renderer/shaderhandler.cpp
    renderer/shaderhandler.h
    renderer/resourcepack.cpp

    utils/camera.h
    utils/colors.h
    utils/constants.h
    utils/easings.h
    utils/helpers.cpp
    utils/helpers.h
    utils/lerp.cpp
    utils/lerp.h
    utils/quadtree.h
    utils/rectangles.cpp
    utils/rectangles.h
    utils/vectors.cpp
    utils/vectors.h

    window/windowhandler.cpp
    window/windowhandler.h

)
target_sources("luminoveau" PRIVATE
    extern/miniaudio.cpp
    extern/SDL_stbimage.cpp
    extern/stb_image.cpp
    extern/stb_image_write.cpp
)

include(cmake/assetpacker.cmake)

include(cmake/checkforsteam.cmake)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/log/loghandler.cpp")
    target_sources("luminoveau" PRIVATE
        log/loghandler.cpp
        log/loghandler.h
    )
    message("[Luminoveau]: adding loghandler")

endif()

if (FORCE_ALL_WARNINGS)
    target_compile_options("luminoveau" PRIVATE -Wall)
else()
    if (MSVC)
        # Add compiler flags to suppress specific warnings
        add_compile_options(/wd4820 /wd4514 /wd5045)
    endif()
endif()


if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options("luminoveau" PRIVATE -O3)
endif()


target_compile_features("luminoveau" PRIVATE cxx_std_20)
if (UNIX)
    target_compile_options("luminoveau" PUBLIC -static-libstdc++ -static-libgcc)
    target_link_options("luminoveau" INTERFACE -static-libstdc++ -static-libgcc)
endif()

target_include_directories("luminoveau" PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/extern"
)

file(
    DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.5/CPM.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
    EXPECTED_HASH SHA256=c46b876ae3b9f994b4f05a4c15553e0485636862064f1fcc9d8b4f832086bc5d
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

include(cmake/getlibraries.cmake)
include(cmake/checkforimgui.cmake)
