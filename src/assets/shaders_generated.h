// Auto-generated shader header - DO NOT EDIT
//
// This file provides a unified interface to all compiled shaders.
// The actual backend (SPIR-V, DXIL, Metal) is determined at compile time
// by which .cpp files are included in the build.

#pragma once

#include <cstdint>
#include <cstddef>

namespace Lumi {
namespace Shaders {
    // FullscreenQuad Shaders
    extern const uint8_t FULLSCREEN_QUAD_VERT[];
    extern const size_t FULLSCREEN_QUAD_VERT_SIZE;
    extern const uint8_t FULLSCREEN_QUAD_FRAG[];
    extern const size_t FULLSCREEN_QUAD_FRAG_SIZE;
    // Model3d Shaders
    extern const uint8_t MODEL3D_VERT[];
    extern const size_t MODEL3D_VERT_SIZE;
    extern const uint8_t MODEL3D_FRAG[];
    extern const size_t MODEL3D_FRAG_SIZE;
    // ParticlesPov Shaders
    extern const uint8_t PARTICLES_POV_VERT[];
    extern const size_t PARTICLES_POV_VERT_SIZE;
    extern const uint8_t PARTICLES_POV_FRAG[];
    extern const size_t PARTICLES_POV_FRAG_SIZE;
    // Particles Shaders
    extern const uint8_t PARTICLES_VERT[];
    extern const size_t PARTICLES_VERT_SIZE;
    extern const uint8_t PARTICLES_FRAG[];
    extern const size_t PARTICLES_FRAG_SIZE;
    // Shadow Shaders
    extern const uint8_t SHADOW_VERT[];
    extern const size_t SHADOW_VERT_SIZE;
    extern const uint8_t SHADOW_FRAG[];
    extern const size_t SHADOW_FRAG_SIZE;
    // Shadowcube Shaders
    extern const uint8_t SHADOWCUBE_VERT[];
    extern const size_t SHADOWCUBE_VERT_SIZE;
    extern const uint8_t SHADOWCUBE_FRAG[];
    extern const size_t SHADOWCUBE_FRAG_SIZE;
    // Sprite Shaders
    extern const uint8_t SPRITE_VERT[];
    extern const size_t SPRITE_VERT_SIZE;
    extern const uint8_t SPRITE_FRAG[];
    extern const size_t SPRITE_FRAG_SIZE;
    // Particles Compute Shader (always SPIR-V - SDL_ShaderCross handles cross-compilation)
    extern const uint8_t PARTICLES_COMP[];
    extern const size_t PARTICLES_COMP_SIZE;
} // namespace Shaders
} // namespace Lumi