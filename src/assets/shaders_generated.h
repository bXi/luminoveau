// Auto-generated shader header - DO NOT EDIT
//
// This file provides a unified interface to all compiled shaders.
// The actual backend (SPIR-V, DXIL, Metal) is determined at compile time
// by which .cpp files are included in the build.

#pragma once

#include <cstdint>
#include <cstddef>

namespace Luminoveau {
namespace Shaders {
    // FullscreenQuad Shaders
    extern const uint8_t FullscreenQuad_Vert[];
    extern const size_t FullscreenQuad_Vert_Size;
    extern const uint8_t FullscreenQuad_Frag[];
    extern const size_t FullscreenQuad_Frag_Size;
    // Model3d Shaders
    extern const uint8_t Model3d_Vert[];
    extern const size_t Model3d_Vert_Size;
    extern const uint8_t Model3d_Frag[];
    extern const size_t Model3d_Frag_Size;
    // ParticlesPov Shaders
    extern const uint8_t ParticlesPov_Vert[];
    extern const size_t ParticlesPov_Vert_Size;
    extern const uint8_t ParticlesPov_Frag[];
    extern const size_t ParticlesPov_Frag_Size;
    // Particles Shaders
    extern const uint8_t Particles_Vert[];
    extern const size_t Particles_Vert_Size;
    extern const uint8_t Particles_Frag[];
    extern const size_t Particles_Frag_Size;
    // Shadow Shaders
    extern const uint8_t Shadow_Vert[];
    extern const size_t Shadow_Vert_Size;
    extern const uint8_t Shadow_Frag[];
    extern const size_t Shadow_Frag_Size;
    // Shadowcube Shaders
    extern const uint8_t Shadowcube_Vert[];
    extern const size_t Shadowcube_Vert_Size;
    extern const uint8_t Shadowcube_Frag[];
    extern const size_t Shadowcube_Frag_Size;
    // Sprite Shaders
    extern const uint8_t Sprite_Vert[];
    extern const size_t Sprite_Vert_Size;
    extern const uint8_t Sprite_Frag[];
    extern const size_t Sprite_Frag_Size;
    // Particles Compute Shader (always SPIR-V - SDL_ShaderCross handles cross-compilation)
    extern const uint8_t Particles_Comp[];
    extern const size_t Particles_Comp_Size;
} // namespace Shaders
} // namespace Luminoveau