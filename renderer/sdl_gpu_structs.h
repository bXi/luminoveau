#pragma once

#include <SDL3/SDL.h>

namespace GPUstructs {

    extern SDL_GPUColorTargetBlendState defaultBlendState;

    extern SDL_GPURasterizerState defaultRasterizerState;

    extern SDL_GPUSamplerCreateInfo linearSamplerCreateInfo;
    extern SDL_GPUSamplerCreateInfo nearestSamplerCreateInfo;
}
