cmake_minimum_required(VERSION 3.12)

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

    steam/steamhandler.cpp
    steam/steamhandler.h


    assethandler/assethandler.cpp
    assethandler/assethandler.h
    assethandler/DroidSansMono.cpp
    assethandler/spritefrag.cpp
    assethandler/spritevert.cpp

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

    window/renderpass.h

    window/spriterenderpass.cpp
    window/spriterenderpass.h

    window/textrenderpass.cpp
    window/textrenderpass.h


)

target_sources("luminoveau" PRIVATE
    extern/miniaudio.cpp
    extern/SDL_stbimage.cpp
    extern/stb_image.cpp
    extern/stb_image_write.cpp
)

target_sources("luminoveau" PRIVATE
    steam/steamhandler.cpp
)


if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/steam/sdk/public/steam/steam_api.h")
    find_package(Threads REQUIRED)

    target_include_directories("luminoveau" PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/steam/sdk/public/steam/"
    )

    target_link_libraries(${PROJECT_NAME}
        PRIVATE Threads::Threads
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/steam/sdk/redistributable_bin/win64/steam_api64.lib
    )

    add_custom_command(
    TARGET "luminoveau" POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/steam/sdk/redistributable_bin/win64/steam_api64.dll"
        ${CMAKE_BINARY_DIR}
    )
    message("[Luminoveau]: adding SteamWorks SDK")
endif()




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
target_compile_options("luminoveau" PUBLIC -static-libstdc++ -static-libgcc)
target_link_options("luminoveau" INTERFACE -static-libstdc++ -static-libgcc)


target_include_directories("luminoveau" PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/extern"
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
else()

    set(BUILD_SHARED_LIBS OFF)
    set(SDL_STATIC ON)
    set(SDL_SHARED OFF)
    set(INTERFACE_SDL3_SHARED OFF)
    set(SDL_VIDEO_DRIVER_X11_SUPPORTS_GENERIC_EVENTS ON)

endif()


CPMAddPackage(
    NAME glm
    GIT_TAG 33b4a62
    GITHUB_REPOSITORY g-truc/glm
)


if(glm_ADDED)
    target_include_directories("luminoveau" PUBLIC ${glm_SOURCE_DIR})
endif()

CPMAddPackage(
    NAME SPIRV-Cross
    GIT_TAG 6173e24
    GITHUB_REPOSITORY KhronosGroup/SPIRV-Cross
)

if(SPIRV-Cross_ADDED)
    target_include_directories("luminoveau" PUBLIC ${SPIRV-Cross_SOURCE_DIR})
endif()

CPMAddPackage(
    NAME SDL3
    GIT_TAG 3a1d76d
    GITHUB_REPOSITORY libsdl-org/SDL
)

if(SDL3_ADDED)
    target_include_directories("luminoveau" PUBLIC ${SDL3_SOURCE_DIR}/include)
endif()

CPMAddPackage(
    NAME harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    GIT_TAG 40ef6c0
)

if(NOT MSVC AND NOT ANDROID)
    CPMAddPackage(
        NAME freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG 59320b2
    )

    if (freetype_ADDED)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
endif()

CPMAddPackage(
    NAME SDL3_ttf
    GIT_TAG a670386
    GITHUB_REPOSITORY libsdl-org/SDL_ttf
)

if(SDL3_ttf_ADDED)
    target_include_directories("luminoveau" PUBLIC ${SDL3_ttf_SOURCE_DIR}/include)
endif()

if (ANDROID)
    target_link_libraries("luminoveau" PUBLIC
        SDL3::SDL3
        SDL3_ttf::SDL3_ttf
        spirv-cross-c
    )
else()
    target_link_libraries("luminoveau" PUBLIC
        SDL3::SDL3-static
        SDL3_ttf::SDL3_ttf
        spirv-cross-c
    )
endif()

if (ADD_IMGUI)
#    CPMAddPackage(
#        NAME Imgui
#        GIT_TAG 8cc6eee
#        GITHUB_REPOSITORY ocornut/imgui
#    )
    CPMAddPackage(
        NAME Imgui
        GIT_TAG 59c15cb
        GITHUB_REPOSITORY lasagnaphil/imgui
    )



    if(Imgui_ADDED)
        add_definitions(-DADD_IMGUI)
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
                window/imgui_impl_sdlgpu3.cpp
                window/imgui_impl_sdl3.cpp
                    window/imgui_impl_sdlgpu3_shadercross.h
            )
        else()
            target_sources("luminoveau" PRIVATE
                window/imgui_impl_sdlgpu3.cpp
                window/imgui_impl_sdl3.cpp
                    window/imgui_impl_sdlgpu3_shadercross.h
            )
        endif()
    endif()
endif()
