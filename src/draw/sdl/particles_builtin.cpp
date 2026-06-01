// SDL-backend builder for the built-in particle compute pipeline.
// Loads the precompiled SPIRV blob produced by the engine's shader compile step;
// SDL_ShaderCross handles runtime cross-compilation to the platform's native format.

#include "draw/particles_builtin.h"

#include "core/log/log.h"
#include "gpu/IGpu.h"
#include "renderer/renderer.h"
#include "assets/shaders_generated.h"

GpuComputePipelineHandle ParticlesBuiltin::CreateComputePipeline() {
    GpuComputePipelineCreateInfo info;
    info.code                        = Luminoveau::Shaders::Particles_Comp;
    info.codeSize                    = Luminoveau::Shaders::Particles_Comp_Size;
    info.entrypoint                  = "main";
    info.threadCountX                = 64;
    info.threadCountY                = 1;
    info.threadCountZ                = 1;
    info.readonlyStorageBufferCount  = 2;  // systems, colliders
    info.readwriteStorageBufferCount = 1;  // particles
    info.uniformBufferCount          = 1;
    GpuComputePipelineHandle ph = Renderer::GetGpu().createComputePipeline(info);
    if (!ph) {
        LOG_WARNING("Particles: built-in compute pipeline creation FAILED");
    }
    return ph;
}
