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

    steam/steamhandler.cpp
    steam/steamhandler.h


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
)

target_sources("luminoveau" PRIVATE
    extern/miniaudio.cpp
    extern/SDL_stbimage.cpp
    extern/stb_image.cpp
    extern/stb_image_write.cpp
    extern/SDL_gpu_shadercross.cpp
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
        #"C:/Users/user/Documents/Visual Studio 2022/Libraries/steam-sdk/redistributable_bin/win64/steam_api64.lib"
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
    NAME SDL3
    GIT_TAG 2786290
    GITHUB_REPOSITORY libsdl-org/SDL
)

if(SDL3_ADDED)
    target_include_directories("luminoveau" PUBLIC ${SDL3_SOURCE_DIR}/include)
endif()


set(BLEND2D_STATIC TRUE)
set(ASMJIT_EMBED TRUE)


CPMAddPackage(
    NAME asmjit
    GIT_TAG 6784722
    GITHUB_REPOSITORY asmjit/asmjit
)


CPMAddPackage(
    NAME blend2d
    GIT_TAG 44baf9e
    GITHUB_REPOSITORY blend2d/blend2d
)

if(blend2d_ADDED)
    target_include_directories("luminoveau" PUBLIC ${blend2d_SOURCE_DIR}/src)
endif()


if (ANDROID)
    target_link_libraries("luminoveau" PUBLIC
        SDL3::SDL3
        blend2d::blend2d
    )
else()
    target_link_libraries("luminoveau" PUBLIC
        SDL3::SDL3-static
        blend2d::blend2d
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
