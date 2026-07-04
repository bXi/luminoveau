// Per-backend factory for the built-in particle physics compute pipeline.
// Implementations live in draw/sdl/particles_builtin.cpp (precompiled SPIRV) and
// draw/webgpu/particles_builtin.cpp (embedded WGSL).

#pragma once

#include "gpu/types.h"

/// @cond INTERNAL
namespace ParticlesBuiltin {
    // Creates the engine's default particle physics compute pipeline. Threadgroup
    // size is fixed at 64×1×1 on both backends.
    GpuComputePipelineHandle CreateComputePipeline();
}
/// @endcond
