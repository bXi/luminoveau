#pragma once

#include "gpu/types.h"

// ─────────────────────────────────────────────────────────────────────────────
// GpuPresets — ready-made sampler and blend state descriptors.
//
// Pass directly to IGpu::createSampler() or into GpuGraphicsPipelineCreateInfo.
// All values are constexpr — zero runtime cost.
//
// Quick reference:
//   Samplers:    LinearClamp, LinearRepeat, NearestClamp, NearestRepeat
//   Blend:       AlphaBlend, SrcAlphaBlend, AdditiveBlend, PremultipliedAlpha, Opaque
// ─────────────────────────────────────────────────────────────────────────────

namespace GpuPresets {

// ── Samplers ─────────────────────────────────────────────────────────────────

// Smooth filtering, no tiling. Good for UI, sprites, render targets.
inline constexpr GpuSamplerCreateInfo LinearClamp = {
    .minFilter = GpuFilter::Linear,
    .magFilter = GpuFilter::Linear,
    .mipFilter = GpuFilter::Linear,
    .addressU  = GpuSamplerAddressMode::ClampToEdge,
    .addressV  = GpuSamplerAddressMode::ClampToEdge,
    .addressW  = GpuSamplerAddressMode::ClampToEdge,
};

// Smooth filtering, tiles at edges. Good for world textures and terrain.
inline constexpr GpuSamplerCreateInfo LinearRepeat = {
    .minFilter = GpuFilter::Linear,
    .magFilter = GpuFilter::Linear,
    .mipFilter = GpuFilter::Linear,
    .addressU  = GpuSamplerAddressMode::Repeat,
    .addressV  = GpuSamplerAddressMode::Repeat,
    .addressW  = GpuSamplerAddressMode::Repeat,
};

// Pixel-perfect, no tiling. Good for pixel art and tilemaps.
inline constexpr GpuSamplerCreateInfo NearestClamp = {
    .minFilter = GpuFilter::Nearest,
    .magFilter = GpuFilter::Nearest,
    .mipFilter = GpuFilter::Nearest,
    .addressU  = GpuSamplerAddressMode::ClampToEdge,
    .addressV  = GpuSamplerAddressMode::ClampToEdge,
    .addressW  = GpuSamplerAddressMode::ClampToEdge,
};

// Pixel-perfect, tiles at edges. Good for repeating pixel-art textures.
inline constexpr GpuSamplerCreateInfo NearestRepeat = {
    .minFilter = GpuFilter::Nearest,
    .magFilter = GpuFilter::Nearest,
    .mipFilter = GpuFilter::Nearest,
    .addressU  = GpuSamplerAddressMode::Repeat,
    .addressV  = GpuSamplerAddressMode::Repeat,
    .addressW  = GpuSamplerAddressMode::Repeat,
};

// ── Blend states ─────────────────────────────────────────────────────────────

// Standard alpha blending. The go-to for sprites and UI.
// result = src.rgb * src.a + dst.rgb * (1 - src.a)
inline constexpr GpuColorTargetBlendState AlphaBlend = {
    .blendEnabled   = true,
    .srcColorFactor = GpuBlendFactor::SrcAlpha,
    .dstColorFactor = GpuBlendFactor::OneMinusSrcAlpha,
    .colorOp        = GpuBlendOp::Add,
    .srcAlphaFactor = GpuBlendFactor::SrcAlpha,
    .dstAlphaFactor = GpuBlendFactor::OneMinusSrcAlpha,
    .alphaOp        = GpuBlendOp::Add,
};

// Alpha blend that never modifies the render target's alpha channel.
// output.a always equals dst.a, regardless of what is drawn.
// Use when rendering sprites/UI to an intermediate framebuffer with straight
// (non-premultiplied) alpha — prevents drawn content from corrupting the
// alpha that the final composite pass depends on.
inline constexpr GpuColorTargetBlendState AlphaBlendKeepDstAlpha = {
    .blendEnabled   = true,
    .srcColorFactor = GpuBlendFactor::SrcAlpha,
    .dstColorFactor = GpuBlendFactor::OneMinusSrcAlpha,
    .colorOp        = GpuBlendOp::Add,
    .srcAlphaFactor = GpuBlendFactor::DstAlpha,
    .dstAlphaFactor = GpuBlendFactor::OneMinusSrcAlpha,
    .alphaOp        = GpuBlendOp::Add,
};

// Alpha blend that accumulates alpha correctly when compositing layers into
// a render-to-texture target that will itself be blended later.
// result.a = src.a + dst.a * (1 - src.a)  (Porter-Duff "over" for alpha)
inline constexpr GpuColorTargetBlendState AlphaBlendPreserveAlpha = {
    .blendEnabled   = true,
    .srcColorFactor = GpuBlendFactor::SrcAlpha,
    .dstColorFactor = GpuBlendFactor::OneMinusSrcAlpha,
    .colorOp        = GpuBlendOp::Add,
    .srcAlphaFactor = GpuBlendFactor::One,
    .dstAlphaFactor = GpuBlendFactor::OneMinusSrcAlpha,
    .alphaOp        = GpuBlendOp::Add,
};

// Additive blending. Colours add together — good for fire, glow, particles.
// result = src.rgb * src.a + dst.rgb
inline constexpr GpuColorTargetBlendState AdditiveBlend = {
    .blendEnabled   = true,
    .srcColorFactor = GpuBlendFactor::SrcAlpha,
    .dstColorFactor = GpuBlendFactor::One,
    .colorOp        = GpuBlendOp::Add,
    .srcAlphaFactor = GpuBlendFactor::Zero,
    .dstAlphaFactor = GpuBlendFactor::One,
    .alphaOp        = GpuBlendOp::Add,
};

// For textures with pre-multiplied alpha (alpha already baked into RGB).
// result = src.rgb + dst.rgb * (1 - src.a)
inline constexpr GpuColorTargetBlendState PremultipliedAlpha = {
    .blendEnabled   = true,
    .srcColorFactor = GpuBlendFactor::One,
    .dstColorFactor = GpuBlendFactor::OneMinusSrcAlpha,
    .colorOp        = GpuBlendOp::Add,
    .srcAlphaFactor = GpuBlendFactor::One,
    .dstAlphaFactor = GpuBlendFactor::OneMinusSrcAlpha,
    .alphaOp        = GpuBlendOp::Add,
};

// No blending. Source overwrites destination completely. Good for opaque geometry.
inline constexpr GpuColorTargetBlendState Opaque = {
    .blendEnabled   = false,
    .srcColorFactor = GpuBlendFactor::One,
    .dstColorFactor = GpuBlendFactor::Zero,
    .colorOp        = GpuBlendOp::Add,
    .srcAlphaFactor = GpuBlendFactor::One,
    .dstAlphaFactor = GpuBlendFactor::Zero,
    .alphaOp        = GpuBlendOp::Add,
};

} // namespace GpuPresets
