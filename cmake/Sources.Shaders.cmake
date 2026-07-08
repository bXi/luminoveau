# Auto-generated shader sources - DO NOT EDIT
# Available backends: wgsl, spirv, dxil

# Set default GPU backend if not specified
if(NOT DEFINED LUMINOVEAU_GPU_BACKEND)
    set(LUMINOVEAU_GPU_BACKEND "SPIRV" CACHE STRING "GPU shader backend (SPIRV, DXIL, METALLIB, WGSL)")
endif()

# Validate backend selection
if(NOT LUMINOVEAU_GPU_BACKEND MATCHES "^(SPIRV|DXIL|METALLIB|WGSL)$")
    message(FATAL_ERROR "Invalid LUMINOVEAU_GPU_BACKEND: ${LUMINOVEAU_GPU_BACKEND}. Must be SPIRV, DXIL, METALLIB, or WGSL")
endif()

lumi_done("GPU Backend: ${LUMINOVEAU_GPU_BACKEND}")

# Select shader files based on backend
if(LUMINOVEAU_GPU_BACKEND STREQUAL "SPIRV")
    set(LUMINOVEAU_SHADER_SOURCES
        src/assets/shaders/fullscreen_quad_vert.spirv.cpp
        src/assets/shaders/fullscreen_quad_frag.spirv.cpp
        src/assets/shaders/model3d_vert.spirv.cpp
        src/assets/shaders/model3d_frag.spirv.cpp
        src/assets/shaders/particles_pov_vert.spirv.cpp
        src/assets/shaders/particles_pov_frag.spirv.cpp
        src/assets/shaders/particles_vert.spirv.cpp
        src/assets/shaders/particles_frag.spirv.cpp
        src/assets/shaders/shadow_vert.spirv.cpp
        src/assets/shaders/shadow_frag.spirv.cpp
        src/assets/shaders/shadowcube_vert.spirv.cpp
        src/assets/shaders/shadowcube_frag.spirv.cpp
        src/assets/shaders/sprite_vert.spirv.cpp
        src/assets/shaders/sprite_frag.spirv.cpp
    )
elseif(LUMINOVEAU_GPU_BACKEND STREQUAL "DXIL")
    set(LUMINOVEAU_SHADER_SOURCES

        src/assets/shaders/fullscreen_quad_vert.dxil.cpp
        src/assets/shaders/fullscreen_quad_frag.dxil.cpp
        src/assets/shaders/model3d_vert.dxil.cpp
        src/assets/shaders/model3d_frag.dxil.cpp
        src/assets/shaders/particles_pov_vert.dxil.cpp
        src/assets/shaders/particles_pov_frag.dxil.cpp
        src/assets/shaders/particles_vert.dxil.cpp
        src/assets/shaders/particles_frag.dxil.cpp
        src/assets/shaders/shadow_vert.dxil.cpp
        src/assets/shaders/shadow_frag.dxil.cpp
        src/assets/shaders/shadowcube_vert.dxil.cpp
        src/assets/shaders/shadowcube_frag.dxil.cpp
        src/assets/shaders/sprite_vert.dxil.cpp
        src/assets/shaders/sprite_frag.dxil.cpp
    )
elseif(LUMINOVEAU_GPU_BACKEND STREQUAL "METALLIB")
    set(LUMINOVEAU_SHADER_SOURCES

        src/assets/shaders/fullscreen_quad_vert.metallib.cpp
        src/assets/shaders/fullscreen_quad_frag.metallib.cpp
        src/assets/shaders/model3d_vert.metallib.cpp
        src/assets/shaders/model3d_frag.metallib.cpp
        src/assets/shaders/particles_pov_vert.metallib.cpp
        src/assets/shaders/particles_pov_frag.metallib.cpp
        src/assets/shaders/particles_vert.metallib.cpp
        src/assets/shaders/particles_frag.metallib.cpp
        src/assets/shaders/shadow_vert.metallib.cpp
        src/assets/shaders/shadow_frag.metallib.cpp
        src/assets/shaders/shadowcube_vert.metallib.cpp
        src/assets/shaders/shadowcube_frag.metallib.cpp
        src/assets/shaders/sprite_vert.metallib.cpp
        src/assets/shaders/sprite_frag.metallib.cpp
    )
elseif(LUMINOVEAU_GPU_BACKEND STREQUAL "WGSL")
    set(LUMINOVEAU_SHADER_SOURCES

        src/assets/shaders/fullscreen_quad_vert.wgsl.cpp
        src/assets/shaders/fullscreen_quad_frag.wgsl.cpp
        src/assets/shaders/model3d_vert.wgsl.cpp
        src/assets/shaders/model3d_frag.wgsl.cpp
        src/assets/shaders/particles_pov_vert.wgsl.cpp
        src/assets/shaders/particles_pov_frag.wgsl.cpp
        src/assets/shaders/particles_vert.wgsl.cpp
        src/assets/shaders/particles_frag.wgsl.cpp
        src/assets/shaders/shadow_vert.wgsl.cpp
        src/assets/shaders/shadow_frag.wgsl.cpp
        src/assets/shaders/shadowcube_vert.wgsl.cpp
        src/assets/shaders/shadowcube_frag.wgsl.cpp
        src/assets/shaders/sprite_vert.wgsl.cpp
        src/assets/shaders/sprite_frag.wgsl.cpp
    )
else()
    message(FATAL_ERROR "No shader files available for backend: ${LUMINOVEAU_GPU_BACKEND}")
endif()

# Compute shaders are always SPIR-V: SDL_ShaderCross handles cross-compilation at runtime
set(LUMINOVEAU_COMPUTE_SHADER_SOURCES
    src/assets/shaders/particles_comp.spirv.cpp
)

list(APPEND LUMINOVEAU_SHADER_SOURCES ${LUMINOVEAU_COMPUTE_SHADER_SOURCES})

# Group shader files in IDE
source_group("Generated\\Shaders" FILES ${LUMINOVEAU_SHADER_SOURCES})

# Define shader backend for C++ code
add_compile_definitions(LUMINOVEAU_SHADER_BACKEND_${LUMINOVEAU_GPU_BACKEND})