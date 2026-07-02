#pragma once

#include <SDL3/SDL.h>
#include "gpu/types.h"

// ── Scalar / enum converters (engine → SDL) ────────────────────────────────
SDL_GPUTextureFormat       toSDL(GpuTextureFormat fmt);
SDL_GPUTextureType         toSDL(GpuTextureType t);
SDL_GPUSampleCount         toSDL(GpuSampleCount count);
SDL_GPUTextureUsageFlags   texUsageToSDL(GpuTextureUsage usage);
SDL_GPUBufferUsageFlags    bufUsageToSDL(GpuBufferUsage usage);
SDL_GPUTransferBufferUsage toSDL(GpuTransferUsage usage);
SDL_GPUFilter              toSDL(GpuFilter filter);
SDL_GPUSamplerAddressMode  toSDL(GpuSamplerAddressMode mode);
SDL_GPULoadOp              toSDL(GpuLoadOp op);
SDL_GPUStoreOp             toSDL(GpuStoreOp op);
SDL_GPUBlendFactor         toSDL(GpuBlendFactor factor);
SDL_GPUBlendOp             toSDL(GpuBlendOp op);
SDL_GPUVertexElementFormat toSDL(GpuVertexElementFormat fmt);
SDL_GPUFillMode            toSDL(GpuFillMode mode);
SDL_GPUCullMode            toSDL(GpuCullMode mode);
SDL_GPUFrontFace           toSDL(GpuFrontFace face);

// ── Scalar converter (SDL → engine) ───────────────────────────────────────
GpuTextureFormat fromSDL(SDL_GPUTextureFormat fmt);

// ── Struct converters (engine → SDL) ──────────────────────────────────────
SDL_GPUSamplerCreateInfo        toSDL(const GpuSamplerCreateInfo& info);
SDL_GPUTextureCreateInfo        toSDL(const GpuTextureCreateInfo& info);
SDL_GPUBufferCreateInfo         toSDL(const GpuBufferCreateInfo& info);
SDL_GPUTransferBufferCreateInfo toSDL(const GpuTransferBufferCreateInfo& info);
SDL_GPUColorTargetBlendState    toSDL(const GpuColorTargetBlendState& blend);

// Default rasterizer: fill, no culling, CCW front face.
// Used internally by SDL-level render pass code.
inline constexpr SDL_GPURasterizerState SDL_DefaultRasterizerState = {
    .fill_mode                  = SDL_GPU_FILLMODE_FILL,
    .cull_mode                  = SDL_GPU_CULLMODE_NONE,
    .front_face                 = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
    .depth_bias_constant_factor = 0.0f,
    .depth_bias_clamp           = 0.0f,
    .depth_bias_slope_factor    = 0.0f,
    .enable_depth_bias          = false,
    .enable_depth_clip          = false,
};
