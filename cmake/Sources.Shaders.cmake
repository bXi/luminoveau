# Auto-generated shader sources - DO NOT EDIT
# Generated: 2026-02-07 21:23:11
# Available backends: metallib

# Set default GPU backend if not specified
if(NOT DEFINED LUMINOVEAU_GPU_BACKEND)
    set(LUMINOVEAU_GPU_BACKEND "SPIRV" CACHE STRING "GPU shader backend (SPIRV, DXIL, METALLIB)")
endif()

# Validate backend selection
if(NOT LUMINOVEAU_GPU_BACKEND MATCHES "^(SPIRV|DXIL|METALLIB)$")
    message(FATAL_ERROR "Invalid LUMINOVEAU_GPU_BACKEND: ${LUMINOVEAU_GPU_BACKEND}. Must be SPIRV, DXIL, or METALLIB")
endif()

message(STATUS "Luminoveau GPU Backend: ${LUMINOVEAU_GPU_BACKEND}")

# Select shader files based on backend
if(LUMINOVEAU_GPU_BACKEND STREQUAL "SPIRV")
    set(LUMINOVEAU_SHADER_SOURCES
        assethandler/shaders/fullscreen_quad_vert.spirv.cpp
        assethandler/shaders/fullscreen_quad_frag.spirv.cpp
        assethandler/shaders/model3d_vert.spirv.cpp
        assethandler/shaders/model3d_frag.spirv.cpp
        assethandler/shaders/sprite_vert.spirv.cpp
        assethandler/shaders/sprite_frag.spirv.cpp
    )
elseif(LUMINOVEAU_GPU_BACKEND STREQUAL "DXIL")
    set(LUMINOVEAU_SHADER_SOURCES

        assethandler/shaders/fullscreen_quad_vert.dxil.cpp
        assethandler/shaders/fullscreen_quad_frag.dxil.cpp
        assethandler/shaders/model3d_vert.dxil.cpp
        assethandler/shaders/model3d_frag.dxil.cpp
        assethandler/shaders/sprite_vert.dxil.cpp
        assethandler/shaders/sprite_frag.dxil.cpp
elseif(LUMINOVEAU_GPU_BACKEND STREQUAL "METALLIB")
    set(LUMINOVEAU_SHADER_SOURCES

        assethandler/shaders/fullscreen_quad_vert.metallib.cpp
        assethandler/shaders/fullscreen_quad_frag.metallib.cpp
        assethandler/shaders/model3d_vert.metallib.cpp
        assethandler/shaders/model3d_frag.metallib.cpp
        assethandler/shaders/sprite_vert.metallib.cpp
        assethandler/shaders/sprite_frag.metallib.cpp
    )
else()
    message(FATAL_ERROR "No shader files available for backend: ${LUMINOVEAU_GPU_BACKEND}")
endif()

# Group shader files in IDE
source_group("Generated\\Shaders" FILES ${LUMINOVEAU_SHADER_SOURCES})

# Define shader backend for C++ code
add_compile_definitions(LUMINOVEAU_SHADER_BACKEND_${LUMINOVEAU_GPU_BACKEND})