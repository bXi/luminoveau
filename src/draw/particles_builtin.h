// Per-backend factory for the built-in particle physics compute pipeline.
// Implementations live in draw/sdl/particles_builtin.cpp (precompiled SPIRV) and
// draw/webgpu/particles_builtin.cpp (embedded WGSL).

#pragma once

#include "gpu/types.h"

/// @cond INTERNAL
class ParticlesBuiltin {
public:
    // Creates the engine's default particle physics compute pipeline. Threadgroup
    // size is fixed at 64×1×1 on both backends.
    static GpuComputePipelineHandle CreateComputePipeline() { return Get()._createComputePipeline(); }

private:
    // Defined per-backend: draw/sdl/particles_builtin.cpp / draw/webgpu/particles_builtin.cpp.
    GpuComputePipelineHandle _createComputePipeline();

public:
    ParticlesBuiltin(const ParticlesBuiltin &) = delete;

    static ParticlesBuiltin &Get() {
        static ParticlesBuiltin instance;
        return instance;
    }

private:
    ParticlesBuiltin() = default;
};
/// @endcond
