cmake_minimum_required(VERSION 3.12)

project("bixxy-engine" VERSION 0.0.1 DESCRIPTION "Bixxy-Engine")

# Add the content from the provided "engine.cmake" file
add_library("bixxy-engine" STATIC
        audio/audiohandler.cpp
        audio/audiohandler.h
        configuration/configuration.h
        fonts/fonthandler.cpp
        fonts/fonthandler.h
        input/inputhandler.cpp
        input/inputhandler.h
        log/loghandler.cpp
        log/loghandler.h
        render2d/render2dhandler.cpp
        render2d/render2dhandler.h
        settings/mini.h
        settings/settingshandler.cpp
        settings/settingshandler.h
        state/basestate.h
        state/state.h
        texture/texturehandler.cpp
        texture/texturehandler.h
        utils/camera.h
        utils/colors.h
        utils/constants.h
        utils/easings.h
        utils/helpers.cpp
        utils/helpers.h
        utils/lerp.cpp
        utils/lerp.h
        utils/quadtree.h
        utils/rectangles.h
        utils/vectors.h
        window/windowhandler.cpp
        window/windowhandler.h
)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)

target_compile_options("bixxy-engine" PRIVATE -Wall)
target_compile_features("bixxy-engine" PRIVATE cxx_std_23)

target_include_directories("bixxy-engine" PUBLIC
        "${PROJECT_SOURCE_DIR}"
)


file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.3/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
        EXPECTED_HASH SHA256=cc155ce02e7945e7b8967ddfaff0b050e958a723ef7aad3766d368940cb15494
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)


set(BUILD_SHARED_LIBS OFF)
set(SDL_STATIC ON)
set(SDL_SHARED OFF)
set(INTERFACE_SDL3_SHARED OFF)
CPMAddPackage(
  NAME SDL3
  GIT_TAG a4c6b38
  GITHUB_REPOSITORY libsdl-org/SDL
)

if(SDL3_ADDED)
  target_include_directories("bixxy-engine" PUBLIC ${SDL3_SOURCE_DIR}/include)
endif()


set(SDL3IMAGE_BUILD_SHARED_LIBS OFF)

CPMAddPackage(
  NAME SDL3_image
  GIT_TAG b3fcae2
  GITHUB_REPOSITORY libsdl-org/SDL_image
)

if(SDL3_image_ADDED)
  target_include_directories("bixxy-engine" PUBLIC ${SDL3_image_SOURCE_DIR}/include)
endif()



CPMAddPackage(
  NAME freetype
  GIT_REPOSITORY https://github.com/aseprite/freetype2.git
  GIT_TAG e8ebfe9
)

if (freetype_ADDED)
  add_library(Freetype::Freetype ALIAS freetype)
endif()

set(SDL3IMAGE_BUILD_SHARED_LIBS OFF)

CPMAddPackage(
  NAME SDL3_ttf
  GIT_TAG 3958dc2
  GITHUB_REPOSITORY libsdl-org/SDL_ttf
)

if(SDL3_ttf_ADDED)
  target_include_directories("bixxy-engine" PUBLIC ${SDL3_ttf_SOURCE_DIR}/include)
endif()






target_link_libraries("bixxy-engine" PUBLIC
        SDL3::SDL3-static
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
)