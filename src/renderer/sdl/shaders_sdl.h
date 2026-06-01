// SDL-backend internal shader subsystem.
//
// Provides the SDL_ShaderCross-driven GLSL→SPIRV→native pipeline, the on-disk SPIRV
// metadata cache, and the asset-builder helpers used by SDL render passes. Only
// SDL-backend translation units should include this header.

#pragma once

#ifdef LUMINOVEAU_WEBGPU_BACKEND
#error "shaders_sdl.h must not be included from WebGPU-backend translation units"
#endif

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_cross.hpp>
#include <SDL3_shadercross/SDL_shadercross.h>

#include "renderer/shaders.h"
#include "assets/assethandler.h"
#include "assets/compute/computepipeline.h"
#include "file/resourcepack.h"

// Reflected layout metadata for a single SDL shader. Serialized into the shader cache
// alongside the cross-compiled bytecode so the binding/uniform layout survives restarts.
struct ShaderMetadata {
    std::string source_hash;
    std::vector<std::string> sampler_names;
    std::unordered_map<std::string, size_t> uniform_offsets;
    std::unordered_map<std::string, size_t> uniform_sizes;
    uint32_t num_samplers = 0;
    uint32_t num_uniform_buffers = 0;
    uint32_t num_storage_buffers = 0;
    uint32_t num_storage_textures = 0;
    SDL_GPUShaderFormat shader_format = SDL_GPU_SHADERFORMAT_SPIRV;

    std::string serialize() const;
    static ShaderMetadata deserialize(const std::string& data);
};

namespace Shaders {
    // Raw cached SPIRV bytecode for a named GLSL source file.
    PhysFSFileData GetShader(const std::string &filename);

    // Reflected metadata (binding counts, uniform layout, etc.) for the same.
    ShaderMetadata GetShaderMetadata(const std::string &filename);

    // The runtime-target shader format SDL_ShaderCross will cross-compile to.
    SDL_GPUShaderFormat GetShaderFormat(const std::string &filename);

    // SDL_GPU graphics shader from a cached GLSL source filename. Caller owns the shader.
    SDL_GPUShader* CreateGPUShader(SDL_GPUDevice* device, const std::string& filename, SDL_GPUShaderStage stage);

    // Full ShaderAsset (bytecode + reflection + GPU handle) for the same.
    ShaderAsset CreateShaderAsset(SDL_GPUDevice* device, const std::string& filename, SDL_GPUShaderStage stage);

    // Compute pipeline assembled from a .comp GLSL source file.
    ComputePipelineAsset CreateComputePipeline(SDL_GPUDevice* device, const std::string& filename);

    // Compute pipeline assembled directly from precompiled SPIRV bytes (engine-embedded shaders).
    ComputePipelineAsset CreateComputePipelineFromBytes(SDL_GPUDevice* device, const uint8_t* spirvBytes, size_t spirvSize);
}
