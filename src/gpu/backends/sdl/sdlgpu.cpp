#include "gpu/backends/sdl/sdlgpu.h"
#include <cstdint>

SDL_GPUTextureFormat toSDL(GpuTextureFormat fmt) {
    switch (fmt) {
        case GpuTextureFormat::Invalid:               return SDL_GPU_TEXTUREFORMAT_INVALID;
        case GpuTextureFormat::R8_Unorm:              return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        case GpuTextureFormat::R8G8_Unorm:            return SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
        case GpuTextureFormat::R8G8B8A8_Unorm:        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        case GpuTextureFormat::B8G8R8A8_Unorm:        return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        case GpuTextureFormat::R8G8B8A8_Unorm_SRGB:   return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
        case GpuTextureFormat::B8G8R8A8_Unorm_SRGB:   return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
        case GpuTextureFormat::R16_Float:             return SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
        case GpuTextureFormat::R16G16_Float:          return SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
        case GpuTextureFormat::R16G16B16A16_Float:    return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        case GpuTextureFormat::R32_Float:             return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        case GpuTextureFormat::R32G32_Float:          return SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
        case GpuTextureFormat::R32G32B32A32_Float:    return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
        case GpuTextureFormat::R10G10B10A2_Unorm:     return SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM;
        case GpuTextureFormat::B5G6R5_Unorm:          return SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM;
        case GpuTextureFormat::D16_Unorm:             return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
        case GpuTextureFormat::D24_Unorm:             return SDL_GPU_TEXTUREFORMAT_D24_UNORM;
        case GpuTextureFormat::D32_Float:             return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        case GpuTextureFormat::D24_Unorm_S8_Uint:     return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
        case GpuTextureFormat::D32_Float_S8_Uint:     return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
        case GpuTextureFormat::BC1_Unorm:             return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
        case GpuTextureFormat::BC2_Unorm:             return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
        case GpuTextureFormat::BC3_Unorm:             return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
        case GpuTextureFormat::BC4_Unorm:             return SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM;
        case GpuTextureFormat::BC5_Unorm:             return SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;
        case GpuTextureFormat::BC7_Unorm:             return SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;
        case GpuTextureFormat::ASTC_4x4_Unorm:        return SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM;
        default:                                      return SDL_GPU_TEXTUREFORMAT_INVALID;
    }
}

SDL_GPUSampleCount toSDL(GpuSampleCount count) {
    switch (count) {
        case GpuSampleCount::x1: return SDL_GPU_SAMPLECOUNT_1;
        case GpuSampleCount::x2: return SDL_GPU_SAMPLECOUNT_2;
        case GpuSampleCount::x4: return SDL_GPU_SAMPLECOUNT_4;
        case GpuSampleCount::x8: return SDL_GPU_SAMPLECOUNT_8;
        default:                 return SDL_GPU_SAMPLECOUNT_1;
    }
}

GpuTextureFormat fromSDL(SDL_GPUTextureFormat fmt) {
    switch (fmt) {
        case SDL_GPU_TEXTUREFORMAT_R8_UNORM:             return GpuTextureFormat::R8_Unorm;
        case SDL_GPU_TEXTUREFORMAT_R8G8_UNORM:           return GpuTextureFormat::R8G8_Unorm;
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:       return GpuTextureFormat::R8G8B8A8_Unorm;
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:       return GpuTextureFormat::B8G8R8A8_Unorm;
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:  return GpuTextureFormat::R8G8B8A8_Unorm_SRGB;
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:  return GpuTextureFormat::B8G8R8A8_Unorm_SRGB;
        case SDL_GPU_TEXTUREFORMAT_R16_FLOAT:             return GpuTextureFormat::R16_Float;
        case SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT:          return GpuTextureFormat::R16G16_Float;
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:    return GpuTextureFormat::R16G16B16A16_Float;
        case SDL_GPU_TEXTUREFORMAT_R32_FLOAT:             return GpuTextureFormat::R32_Float;
        case SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT:          return GpuTextureFormat::R32G32_Float;
        case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:    return GpuTextureFormat::R32G32B32A32_Float;
        case SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM:     return GpuTextureFormat::R10G10B10A2_Unorm;
        case SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM:          return GpuTextureFormat::B5G6R5_Unorm;
        case SDL_GPU_TEXTUREFORMAT_D16_UNORM:             return GpuTextureFormat::D16_Unorm;
        case SDL_GPU_TEXTUREFORMAT_D24_UNORM:             return GpuTextureFormat::D24_Unorm;
        case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:             return GpuTextureFormat::D32_Float;
        case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:     return GpuTextureFormat::D24_Unorm_S8_Uint;
        case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT:     return GpuTextureFormat::D32_Float_S8_Uint;
        case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM:        return GpuTextureFormat::BC7_Unorm;
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM:        return GpuTextureFormat::ASTC_4x4_Unorm;
        default:                                          return GpuTextureFormat::Invalid;
    }
}

SDL_GPUTextureUsageFlags texUsageToSDL(GpuTextureUsage usage) {
    SDL_GPUTextureUsageFlags flags = 0;
    if (usage & GpuTextureUsage::Sampler)            flags |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (usage & GpuTextureUsage::ColorTarget)        flags |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (usage & GpuTextureUsage::DepthStencilTarget) flags |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    if (usage & GpuTextureUsage::StorageRead)        flags |= SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ;
    if (usage & GpuTextureUsage::StorageWrite)       flags |= SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    if (usage & GpuTextureUsage::Transfer)           flags |= SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ
                                                            | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    return flags;
}

SDL_GPUBufferUsageFlags bufUsageToSDL(GpuBufferUsage usage) {
    SDL_GPUBufferUsageFlags flags = 0;
    if (usage & GpuBufferUsage::Vertex)       flags |= SDL_GPU_BUFFERUSAGE_VERTEX;
    if (usage & GpuBufferUsage::Index)        flags |= SDL_GPU_BUFFERUSAGE_INDEX;
    if (usage & GpuBufferUsage::StorageRead)  flags |= SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    if (usage & GpuBufferUsage::StorageWrite) flags |= SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    if (usage & GpuBufferUsage::Indirect)     flags |= SDL_GPU_BUFFERUSAGE_INDIRECT;
    return flags;
}

SDL_GPUTransferBufferUsage toSDL(GpuTransferUsage usage) {
    switch (usage) {
        case GpuTransferUsage::Upload:   return SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        case GpuTransferUsage::Download: return SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        default:                         return SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    }
}

SDL_GPUFilter toSDL(GpuFilter filter) {
    switch (filter) {
        case GpuFilter::Nearest: return SDL_GPU_FILTER_NEAREST;
        case GpuFilter::Linear:  return SDL_GPU_FILTER_LINEAR;
        default:                 return SDL_GPU_FILTER_NEAREST;
    }
}

SDL_GPUSamplerAddressMode toSDL(GpuSamplerAddressMode mode) {
    switch (mode) {
        case GpuSamplerAddressMode::Repeat:         return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        case GpuSamplerAddressMode::MirroredRepeat: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
        case GpuSamplerAddressMode::ClampToEdge:    return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        default:                                    return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    }
}

SDL_GPULoadOp toSDL(GpuLoadOp op) {
    switch (op) {
        case GpuLoadOp::Load:     return SDL_GPU_LOADOP_LOAD;
        case GpuLoadOp::Clear:    return SDL_GPU_LOADOP_CLEAR;
        case GpuLoadOp::DontCare: return SDL_GPU_LOADOP_DONT_CARE;
        default:                  return SDL_GPU_LOADOP_LOAD;
    }
}

SDL_GPUStoreOp toSDL(GpuStoreOp op) {
    switch (op) {
        case GpuStoreOp::Store:          return SDL_GPU_STOREOP_STORE;
        case GpuStoreOp::DontCare:       return SDL_GPU_STOREOP_DONT_CARE;
        case GpuStoreOp::Resolve:        return SDL_GPU_STOREOP_RESOLVE;
        case GpuStoreOp::ResolveAndStore: return SDL_GPU_STOREOP_RESOLVE_AND_STORE;
        default:                         return SDL_GPU_STOREOP_STORE;
    }
}

SDL_GPUBlendFactor toSDL(GpuBlendFactor factor) {
    switch (factor) {
        case GpuBlendFactor::Zero:                return SDL_GPU_BLENDFACTOR_ZERO;
        case GpuBlendFactor::One:                 return SDL_GPU_BLENDFACTOR_ONE;
        case GpuBlendFactor::SrcColor:            return SDL_GPU_BLENDFACTOR_SRC_COLOR;
        case GpuBlendFactor::OneMinusSrcColor:    return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
        case GpuBlendFactor::DstColor:            return SDL_GPU_BLENDFACTOR_DST_COLOR;
        case GpuBlendFactor::OneMinusDstColor:    return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
        case GpuBlendFactor::SrcAlpha:            return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        case GpuBlendFactor::OneMinusSrcAlpha:    return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        case GpuBlendFactor::DstAlpha:            return SDL_GPU_BLENDFACTOR_DST_ALPHA;
        case GpuBlendFactor::OneMinusDstAlpha:    return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
        case GpuBlendFactor::ConstantColor:       return SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
        case GpuBlendFactor::OneMinusConstantColor: return SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
        case GpuBlendFactor::SrcAlphaSaturate:    return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
        default:                                  return SDL_GPU_BLENDFACTOR_ZERO;
    }
}

SDL_GPUBlendOp toSDL(GpuBlendOp op) {
    switch (op) {
        case GpuBlendOp::Add:             return SDL_GPU_BLENDOP_ADD;
        case GpuBlendOp::Subtract:        return SDL_GPU_BLENDOP_SUBTRACT;
        case GpuBlendOp::ReverseSubtract: return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
        case GpuBlendOp::Min:             return SDL_GPU_BLENDOP_MIN;
        case GpuBlendOp::Max:             return SDL_GPU_BLENDOP_MAX;
        default:                          return SDL_GPU_BLENDOP_ADD;
    }
}

SDL_GPUVertexElementFormat toSDL(GpuVertexElementFormat fmt) {
    switch (fmt) {
        case GpuVertexElementFormat::Float:       return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
        case GpuVertexElementFormat::Float2:      return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        case GpuVertexElementFormat::Float3:      return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        case GpuVertexElementFormat::Float4:      return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        case GpuVertexElementFormat::Byte2:       return SDL_GPU_VERTEXELEMENTFORMAT_BYTE2;
        case GpuVertexElementFormat::Byte4:       return SDL_GPU_VERTEXELEMENTFORMAT_BYTE4;
        case GpuVertexElementFormat::UByte2:      return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2;
        case GpuVertexElementFormat::UByte4:      return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
        case GpuVertexElementFormat::Byte2Norm:   return SDL_GPU_VERTEXELEMENTFORMAT_BYTE2_NORM;
        case GpuVertexElementFormat::Byte4Norm:   return SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM;
        case GpuVertexElementFormat::UByte2Norm:  return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2_NORM;
        case GpuVertexElementFormat::UByte4Norm:  return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
        case GpuVertexElementFormat::Short2:      return SDL_GPU_VERTEXELEMENTFORMAT_SHORT2;
        case GpuVertexElementFormat::Short4:      return SDL_GPU_VERTEXELEMENTFORMAT_SHORT4;
        case GpuVertexElementFormat::Short2Norm:  return SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM;
        case GpuVertexElementFormat::Short4Norm:  return SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM;
        case GpuVertexElementFormat::Half2:       return SDL_GPU_VERTEXELEMENTFORMAT_HALF2;
        case GpuVertexElementFormat::Half4:       return SDL_GPU_VERTEXELEMENTFORMAT_HALF4;
        case GpuVertexElementFormat::Int:         return SDL_GPU_VERTEXELEMENTFORMAT_INT;
        case GpuVertexElementFormat::Int2:        return SDL_GPU_VERTEXELEMENTFORMAT_INT2;
        case GpuVertexElementFormat::Int4:        return SDL_GPU_VERTEXELEMENTFORMAT_INT4;
        case GpuVertexElementFormat::UInt:        return SDL_GPU_VERTEXELEMENTFORMAT_UINT;
        default:                                  return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    }
}

SDL_GPUFillMode toSDL(GpuFillMode mode) {
    return mode == GpuFillMode::Line ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
}

SDL_GPUCullMode toSDL(GpuCullMode mode) {
    switch (mode) {
        case GpuCullMode::None:  return SDL_GPU_CULLMODE_NONE;
        case GpuCullMode::Front: return SDL_GPU_CULLMODE_FRONT;
        case GpuCullMode::Back:  return SDL_GPU_CULLMODE_BACK;
        default:                 return SDL_GPU_CULLMODE_NONE;
    }
}

SDL_GPUFrontFace toSDL(GpuFrontFace face) {
    return face == GpuFrontFace::Clockwise
        ? SDL_GPU_FRONTFACE_CLOCKWISE
        : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
}

SDL_GPUSamplerCreateInfo toSDL(const GpuSamplerCreateInfo& info) {
    return {
        .min_filter    = toSDL(info.minFilter),
        .mag_filter    = toSDL(info.magFilter),
        .mipmap_mode   = (info.mipFilter == GpuFilter::Linear)
                            ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                            : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = toSDL(info.addressU),
        .address_mode_v = toSDL(info.addressV),
        .address_mode_w = toSDL(info.addressW),
        .mip_lod_bias      = info.mipLodBias,
        .max_anisotropy    = info.maxAniso,
        .min_lod           = info.minLod,
        .max_lod           = info.maxLod,
        .enable_anisotropy = info.maxAniso > 1.0f,   // SDL_GPU ignores max_anisotropy without this
    };
}

SDL_GPUTextureType toSDL(GpuTextureType t) {
    switch (t) {
        case GpuTextureType::Tex2DArray: return SDL_GPU_TEXTURETYPE_2D_ARRAY;
        case GpuTextureType::TexCube:    return SDL_GPU_TEXTURETYPE_CUBE;
        case GpuTextureType::Tex2D:      break;
    }
    return SDL_GPU_TEXTURETYPE_2D;
}

SDL_GPUTextureCreateInfo toSDL(const GpuTextureCreateInfo& info) {
    return {
        .type                  = toSDL(info.type),
        .format                = toSDL(info.format),
        .usage                 = texUsageToSDL(info.usage),
        .width                 = info.width,
        .height                = info.height,
        .layer_count_or_depth  = info.depthOrLayers,
        .num_levels            = info.numLevels,
        .sample_count          = toSDL(info.sampleCount),
    };
}

SDL_GPUBufferCreateInfo toSDL(const GpuBufferCreateInfo& info) {
    return {
        .usage = bufUsageToSDL(info.usage),
        .size  = info.size,
    };
}

SDL_GPUTransferBufferCreateInfo toSDL(const GpuTransferBufferCreateInfo& info) {
    return {
        .usage = toSDL(info.usage),
        .size  = info.size,
    };
}

SDL_GPUColorTargetBlendState toSDL(const GpuColorTargetBlendState& blend) {
    return {
        .src_color_blendfactor  = toSDL(blend.srcColorFactor),
        .dst_color_blendfactor  = toSDL(blend.dstColorFactor),
        .color_blend_op         = toSDL(blend.colorOp),
        .src_alpha_blendfactor  = toSDL(blend.srcAlphaFactor),
        .dst_alpha_blendfactor  = toSDL(blend.dstAlphaFactor),
        .alpha_blend_op         = toSDL(blend.alphaOp),
        .enable_blend           = blend.blendEnabled,
    };
}

