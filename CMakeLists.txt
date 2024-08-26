cmake_minimum_required(VERSION 3.12)

project("luminoveau" VERSION 0.0.1 DESCRIPTION "Luminoveau")

option(ADD_IMGUI "Include ImGui" OFF)

# Add the content from the provided "engine.cmake" file
add_library("luminoveau" STATIC
    luminoveau.h

    audio/audiohandler.cpp
    audio/audiohandler.h

    configuration/configuration.h

    text/texthandler.cpp
    text/texthandler.h

    eventbus/eventbushandler.cpp
    eventbus/eventbushandler.h

    input/inputconstants.h
    input/inputdevice.cpp
    input/inputdevice.h
    input/inputhandler.cpp
    input/inputhandler.h

    render2d/render2dhandler.cpp
    render2d/render2dhandler.h

    settings/mini.h
    settings/settingshandler.cpp
    settings/settingshandler.h

    state/basestate.h
    state/state.h
    state/state.cpp

    assethandler/assethandler.cpp
    assethandler/assethandler.h
    assethandler/DroidSansMono.cpp

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

    render2d/SDL2_gfxPrimitives.c
    render2d/SDL2_gfxPrimitives.h
    render2d/SDL2_gfxPrimitives_font.h
    render2d/SDL2_rotozoom.h
    render2d/SDL2_rotozoom.c
)

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

target_include_directories("luminoveau" PUBLIC
    "${PROJECT_SOURCE_DIR}"
)

file(
    DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.3/CPM.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
    EXPECTED_HASH SHA256=cc155ce02e7945e7b8967ddfaff0b050e958a723ef7aad3766d368940cb15494
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

set(SDL_VIDEO_DRIVER_X11_SUPPORTS_GENERIC_EVENTS ON)

#remove AV1 support in SDL3Image because it requires perl and nasm which just breaks in MSVC
set(SDL3IMAGE_AVIF_SAVE OFF)
set(SDL3IMAGE_AVIF OFF)

if (ANDROID) #for android we want to build shared libs

    set(BUILD_SHARED_LIBS ON)
    set(SDL_STATIC OFF)
    set(SDL_SHARED ON)
    set(INTERFACE_SDL3_SHARED ON)

    set(SDL3IMAGE_BUILD_SHARED_LIBS ON)

else()

    set(BUILD_SHARED_LIBS OFF)
    set(SDL_STATIC ON)
    set(SDL_SHARED OFF)
    set(INTERFACE_SDL3_SHARED OFF)
    set(SDL_VIDEO_DRIVER_X11_SUPPORTS_GENERIC_EVENTS ON)

    set(SDL3IMAGE_BUILD_SHARED_LIBS OFF)

endif()

CPMAddPackage(
    NAME SDL3
    GIT_TAG f5f44de
    GITHUB_REPOSITORY libsdl-org/SDL
)

if(SDL3_ADDED)
    target_include_directories("luminoveau" PUBLIC ${SDL3_SOURCE_DIR}/include)
endif()

CPMAddPackage(
    NAME SDL3_image
    GIT_TAG 8abc07d
    GITHUB_REPOSITORY libsdl-org/SDL_image
)

if(SDL3_image_ADDED)
    target_include_directories("luminoveau" PUBLIC ${SDL3_image_SOURCE_DIR}/include)
endif()


CPMAddPackage(
    NAME harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    GIT_TAG ea430c1
)

if(NOT MSVC AND NOT ANDROID)
    CPMAddPackage(
        NAME freetype
        GIT_REPOSITORY https://github.com/aseprite/freetype2.git
        GIT_TAG 9a2d6d9
    )

    if (freetype_ADDED)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
endif()

CPMAddPackage(
    NAME SDL3_ttf
    GIT_TAG 5b6171c
    GITHUB_REPOSITORY libsdl-org/SDL_ttf
)

if(SDL3_ttf_ADDED)
    target_include_directories("luminoveau" PUBLIC ${SDL3_ttf_SOURCE_DIR}/include)
endif()


if (ANDROID)
    target_link_libraries("luminoveau" PUBLIC
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
    )
else()
    target_link_libraries("luminoveau" PUBLIC
        SDL3::SDL3-static
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
    )
endif()

if (ADD_IMGUI)
    CPMAddPackage(
        NAME Imgui
        GIT_TAG 8cc6eee
        GITHUB_REPOSITORY ocornut/imgui
    )

    if(Imgui_ADDED)
        target_include_directories("luminoveau" PUBLIC ${Imgui_SOURCE_DIR})
        target_sources("luminoveau" PUBLIC
            ${Imgui_SOURCE_DIR}/imgui.cpp
            ${Imgui_SOURCE_DIR}/imgui_demo.cpp
            ${Imgui_SOURCE_DIR}/imgui_draw.cpp
            ${Imgui_SOURCE_DIR}/imgui_tables.cpp
            ${Imgui_SOURCE_DIR}/imgui_widgets.cpp
        )

        if (WIN32)
            target_sources("luminoveau" PRIVATE
                window/imgui_impl_win32.cpp
                window/imgui_impl_sdlrenderer3.cpp
                window/imgui_impl_sdl3.cpp
            )
        else()
            target_sources("luminoveau" PRIVATE
                window/imgui_impl_sdlrenderer3.cpp
                window/imgui_impl_sdl3.cpp
            )
        endif()
    endif()
endif()
