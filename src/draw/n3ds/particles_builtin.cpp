// 3DS-backend builder for the built-in particle compute pipeline. The PICA200 has
// no compute; returning a null handle makes Particles::Init keep the queues empty
// and every per-frame particle dispatch no-op (particles.cpp null-checks the
// pipeline before use).

#include "draw/particles_builtin.h"

#include "core/log/log.h"

GpuComputePipelineHandle ParticlesBuiltin::_createComputePipeline() {
    LOG_WARNING("GPU particles are not supported on 3DS (no compute); particle systems will not simulate");
    return 0;
}
