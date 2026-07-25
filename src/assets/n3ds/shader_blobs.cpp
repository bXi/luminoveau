// 3DS-backend placeholder definitions for the precompiled shader blob symbols
// (assets/shaders_generated.h). The 3DS build compiles no SPIRV/DXIL/WGSL blob
// .cpps (its shaders are picasso shbins), but shared code — particles.cpp's
// render-pass Init in particular — still references these symbols. Empty blobs
// make Citro3dGpuBackend::CreateShader return a null handle, so those paths fail
// softly at init instead of at link time.

#include "assets/shaders_generated.h"

// NOLINTBEGIN(readability-identifier-naming) — must match the generated header.
namespace Lumi {
namespace Shaders {

#define LUMI_EMPTY_BLOB(name)               \
    const uint8_t name[]    = { 0 };        \
    const size_t  name##_SIZE = 0

LUMI_EMPTY_BLOB(FULLSCREEN_QUAD_VERT);
LUMI_EMPTY_BLOB(FULLSCREEN_QUAD_FRAG);
LUMI_EMPTY_BLOB(MODEL3D_VERT);
LUMI_EMPTY_BLOB(MODEL3D_FRAG);
LUMI_EMPTY_BLOB(PARTICLES_POV_VERT);
LUMI_EMPTY_BLOB(PARTICLES_POV_FRAG);
LUMI_EMPTY_BLOB(PARTICLES_VERT);
LUMI_EMPTY_BLOB(PARTICLES_FRAG);
LUMI_EMPTY_BLOB(SHADOW_VERT);
LUMI_EMPTY_BLOB(SHADOW_FRAG);
LUMI_EMPTY_BLOB(SHADOWCUBE_VERT);
LUMI_EMPTY_BLOB(SHADOWCUBE_FRAG);
LUMI_EMPTY_BLOB(SPRITE_VERT);
LUMI_EMPTY_BLOB(SPRITE_FRAG);
LUMI_EMPTY_BLOB(PARTICLES_COMP);

#undef LUMI_EMPTY_BLOB

} // namespace Shaders
} // namespace Lumi
// NOLINTEND(readability-identifier-naming)
