// SDL-backend implementation of the engine shader subsystem.
//
// Defines the Shaders singleton declared in renderer/shaders.h. The SPIRV toolchain
// (glslang transpile, spirv_cross reflection, SDL_shadercross) is private to this file;
// GPU objects are created through IGpu. WebGPU has its own lifecycle stubs in
// renderer/webgpu/shaders.cpp — this file is only compiled into the SDL build.

#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_cross.hpp>
#include <SDL3_shadercross/SDL_shadercross.h>
#include "assets/assethandler.h"
#include "file/resourcepack.h"
#include "renderer/shaders.h"
#include "renderer/renderer.h"
#include "assets/shader/shader.h"
#include "assets/compute/computepipeline.h"
#include "core/log/log.h"
#include "gpu/IGpu.h"

#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>

#include "picosha2.h"

// ── ShaderMetadata serialization ─────────────────────────────────────────────────
std::string ShaderMetadata::Serialize() const {
    std::ostringstream oss;
    oss << "source_hash=" << sourceHash << "\n";
    oss << "shader_format=" << static_cast<int>(shaderFormat) << "\n";
    oss << "num_samplers=" << numSamplers << "\n";
    oss << "num_uniform_buffers=" << numUniformBuffers << "\n";
    oss << "num_storage_buffers=" << numStorageBuffers << "\n";
    oss << "num_storage_textures=" << numStorageTextures << "\n";

    for (size_t i = 0; i < samplerNames.size(); ++i) {
        oss << "sampler_" << i << "=" << samplerNames[i] << "\n";
    }
    for (const auto &[name, offset] : uniformOffsets) {
        oss << "uniform_" << name << "_offset=" << offset << "\n";
        oss << "uniform_" << name << "_size=" << uniformSizes.at(name) << "\n";
    }
    return oss.str();
}

ShaderMetadata ShaderMetadata::Deserialize(const std::string &data) {
    ShaderMetadata     metadata;
    std::istringstream iss(data);
    std::string        line;
    while (std::getline(iss, line)) {
        size_t pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string key   = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        if (key == "source_hash") {
            metadata.sourceHash = value;
        } else if (key == "shader_format") {
            metadata.shaderFormat = static_cast<uint32_t>(std::stoi(value));
        } else if (key == "num_samplers") {
            metadata.numSamplers = std::stoul(value);
        } else if (key == "num_uniform_buffers") {
            metadata.numUniformBuffers = std::stoul(value);
        } else if (key == "num_storage_buffers") {
            metadata.numStorageBuffers = std::stoul(value);
        } else if (key == "num_storage_textures") {
            metadata.numStorageTextures = std::stoul(value);
        } else if (key.find("sampler_") == 0) {
            metadata.samplerNames.push_back(value);
        } else if (key.find("uniform_") == 0 && key.find("_offset") != std::string::npos) {
            size_t      uniformPos               = key.find("_offset");
            std::string uniformName              = key.substr(8, uniformPos - 8);
            metadata.uniformOffsets[uniformName] = std::stoul(value);
        } else if (key.find("uniform_") == 0 && key.find("_size") != std::string::npos) {
            size_t      uniformPos             = key.find("_size");
            std::string uniformName            = key.substr(8, uniformPos - 8);
            metadata.uniformSizes[uniformName] = std::stoul(value);
        }
    }
    return metadata;
}

// ── Shaders namespace implementation ─────────────────────────────────────────────

/// @cond INTERNAL

// Forward decls for file-local helpers (formerly private class methods).
static std::string           computeSourceHash(const std::string &source);
static std::string           getCachePath(const std::string &filename, const std::string &extension);
static std::string           getMetadataPath(const std::string &filename);
static ShaderMetadata        extractMetadataFromSPIRV(const std::vector<uint32_t> &spirv);
static std::vector<uint32_t> compileGLSLtoSPIRV(const std::string &source, EShLanguage shaderStage);
static void                  fillResources(TBuiltInResource *resource);
/// @endcond

// ── Entry-point name accessors (cross-backend public API) ────────────────────────
const char *Shaders::_getVertexEntryPoint() {
#if defined(LUMINOVEAU_SHADER_BACKEND_METALLIB)
    return "main0";
#else
    return "main";
#endif
}
const char *Shaders::_getFragmentEntryPoint() { return _getVertexEntryPoint(); }
const char *Shaders::_getComputeEntryPoint() { return _getVertexEntryPoint(); }

// ── Lifecycle ────────────────────────────────────────────────────────────────────
void Shaders::_init() {
    if (!SDL_ShaderCross_Init()) {
        LOG_CRITICAL("Failed to initialize SDL_shadercross: {}", SDL_GetError());
    }
    LOG_INFO("SDL_shadercross initialized successfully");

    _shaderCache = new ResourcePack(FileHandler::GetCacheDirectory() + "shader.cache",
        "luminoveau_shaders");
    if (!_shaderCache->Loaded()) {
        LOG_INFO("No existing shader cache found, will create on first save");
    } else {
        LOG_INFO("Successfully loaded existing shader cache from shader.cache");
    }
}

void Shaders::_quit() {
    if (_shaderCache) {
        LOG_INFO("Saving shader cache (cached {} shaders)...", _metadataCache.size());
        if (_shaderCache->SavePack()) {
            LOG_INFO("Shader cache saved successfully to shader.cache");
        } else {
            LOG_ERROR("Failed to save shader cache!");
        }
    }
    delete _shaderCache;
    _shaderCache = nullptr;

    SDL_ShaderCross_Quit();
    LOG_INFO("SDL_shadercross shut down");
}

/// @cond INTERNAL
// ── File-local cache + reflection helpers ────────────────────────────────────────
static std::string computeSourceHash(const std::string &source) {
    return picosha2::hash256_hex_string(source);
}

static std::string getCachePath(const std::string &filename, const std::string &extension) {
    std::string safeName = filename;
    std::replace(safeName.begin(), safeName.end(), '/', '_');
    std::replace(safeName.begin(), safeName.end(), '\\', '_');
    return safeName + extension;
}

static std::string getMetadataPath(const std::string &filename) {
    return getCachePath(filename, ".meta");
}

bool Shaders::_loadCachedShader(const std::string &cacheKey, std::vector<uint8_t> &outData) {
    if (!_shaderCache || !_shaderCache->HasFile(cacheKey))
        return false;
    try {
        auto buffer = _shaderCache->GetFileBuffer(cacheKey);
        outData.assign(buffer.memory.begin(), buffer.memory.end());
        return true;
    } catch (const std::exception &e) {
        LOG_ERROR("Failed to load cached shader {}: {}", cacheKey.c_str(), e.what());
        return false;
    }
}

bool Shaders::_loadCachedMetadata(const std::string &metadataKey, ShaderMetadata &outMetadata) {
    if (!_shaderCache || !_shaderCache->HasFile(metadataKey))
        return false;
    try {
        auto        buffer = _shaderCache->GetFileBuffer(metadataKey);
        std::string metadataStr(buffer.memory.begin(), buffer.memory.end());
        outMetadata = ShaderMetadata::Deserialize(metadataStr);
        return true;
    } catch (const std::exception &e) {
        LOG_ERROR("Failed to parse metadata from cache: {}", e.what());
        return false;
    }
}

void Shaders::_saveCachedShader(const std::string &cacheKey, const std::vector<uint8_t> &data) {
    if (_shaderCache) {
        _shaderCache->AddFile(cacheKey, data);
        if (_shaderCache->SavePack()) {
            LOG_INFO("Cache saved to shader.cache");
        } else {
            LOG_WARNING("Failed to save cache!");
        }
    } else {
        LOG_WARNING("Cannot cache shader {} - shaderCache is null!", cacheKey.c_str());
    }
}

void Shaders::_saveCachedMetadata(const std::string &metadataKey, const ShaderMetadata &metadata) {
    if (_shaderCache) {
        std::string          metadataStr = metadata.Serialize();
        std::vector<uint8_t> metadataBytes(metadataStr.begin(), metadataStr.end());
        _shaderCache->AddFile(metadataKey, metadataBytes);
        if (_shaderCache->SavePack()) {
            LOG_INFO("Cache saved to shader.cache");
        } else {
            LOG_WARNING("Failed to save cache!");
        }
    } else {
        LOG_WARNING("Cannot cache metadata {} - shaderCache is null!", metadataKey.c_str());
    }
}

static ShaderMetadata extractMetadataFromSPIRV(const std::vector<uint32_t> &spirv) {
    ShaderMetadata metadata;
    try {
        spirv_cross::Compiler compiler(spirv);
        auto                  resources = compiler.get_shader_resources();

        for (const auto &sampler : resources.sampled_images) {
            const std::string &samplerName = compiler.get_name(sampler.id);
            metadata.samplerNames.push_back(samplerName);
        }
        metadata.numSamplers = static_cast<uint32_t>(metadata.samplerNames.size());

        for (const auto &uniform : resources.uniform_buffers) {
            auto &bufferType = compiler.get_type(uniform.base_type_id);
            for (size_t i = 0; i < bufferType.member_types.size(); ++i) {
                const std::string &memberName       = compiler.get_member_name(uniform.base_type_id, i);
                size_t             memberSize       = compiler.get_declared_struct_member_size(bufferType, i);
                size_t             memberOffset     = compiler.type_struct_member_offset(bufferType, i);
                metadata.uniformOffsets[memberName] = memberOffset;
                metadata.uniformSizes[memberName]   = memberSize;
            }
        }
        metadata.numUniformBuffers  = static_cast<uint32_t>(resources.uniform_buffers.size());
        metadata.numStorageBuffers  = static_cast<uint32_t>(resources.storage_buffers.size());
        metadata.numStorageTextures = static_cast<uint32_t>(resources.storage_images.size());
    } catch (const std::exception &e) {
        LOG_ERROR("SPIRV reflection failed: {}", e.what());
    }
    return metadata;
}

// ── Public asset accessors ───────────────────────────────────────────────────────
PhysFSFileData Shaders::_getShader(const std::string &filename) {
    auto cacheIt = _shaderDataCache.find(filename);
    if (cacheIt != _shaderDataCache.end()) {
        return cacheIt->second;
    }

    PhysFSFileData filedata;

    EShLanguage shaderStage;
    if (filename.find(".vert") != std::string::npos) {
        shaderStage = EShLanguage::EShLangVertex;
    } else if (filename.find(".frag") != std::string::npos) {
        shaderStage = EShLanguage::EShLangFragment;
    } else if (filename.find(".comp") != std::string::npos) {
        shaderStage = EShLanguage::EShLangCompute;
    } else {
        LOG_CRITICAL("Could not determine shader stage from filename: {}", filename);
    }

    // Always cache SPIRV; cross-compile at runtime (SDL_shadercross doesn't expose DXIL extraction).
    auto                formats = SDL_GetGPUShaderFormats(Renderer::GetDevice());
    SDL_GPUShaderFormat runtimeFormat;
    std::string         formatExt = ".spv";

    const char *driver = SDL_GetGPUDeviceDriver(Renderer::GetDevice());
    if (strcmp(driver, "direct3d12") == 0 || strcmp(driver, "direct3d11") == 0) {
        runtimeFormat = (formats & SDL_GPU_SHADERFORMAT_DXIL) ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_DXBC;
    } else if (strcmp(driver, "metal") == 0) {
        runtimeFormat = SDL_GPU_SHADERFORMAT_METALLIB;
    } else {
        runtimeFormat = SDL_GPU_SHADERFORMAT_SPIRV;
    }

    std::string cachePath    = getCachePath(filename, formatExt);
    std::string metadataPath = getMetadataPath(filename);

    std::vector<uint8_t> cachedData;
    ShaderMetadata       cachedMetadata;

    if (_loadCachedShader(cachePath, cachedData) && _loadCachedMetadata(metadataPath, cachedMetadata)) {
        auto        sourceFile = FileHandler::GetFileFromPhysFS(filename);
        std::string source(static_cast<char *>(sourceFile.data), sourceFile.fileSize);
        std::string sourceHash = computeSourceHash(source);

        if (sourceHash == cachedMetadata.sourceHash) {
            LOG_INFO("Loaded cached shader: {}", filename.c_str());
            filedata.fileDataVector = std::move(cachedData);
            filedata.data           = filedata.fileDataVector.data();
            filedata.fileSize       = filedata.fileDataVector.size();

            // After copy into the map, fix the cache entry's data pointer so it points to
            // the map entry's own fileDataVector (not the local's, which will be destroyed).
            _shaderDataCache[filename]      = filedata;
            _shaderDataCache[filename].data = _shaderDataCache[filename].fileDataVector.data();
            _metadataCache[filename]        = cachedMetadata;
            return _shaderDataCache[filename];
        } else {
            LOG_INFO("Cache invalid for {} (source changed), recompiling", filename.c_str());
        }
    }

    LOG_INFO("Compiling shader: {}", filename.c_str());

    auto        sourceFile = FileHandler::GetFileFromPhysFS(filename);
    std::string source(static_cast<char *>(sourceFile.data), sourceFile.fileSize);

    auto spirvBlob = compileGLSLtoSPIRV(source, shaderStage);
    if (spirvBlob.empty()) {
        LOG_CRITICAL("failed to compile shader to SPIRV: {}", filename);
    }

    ShaderMetadata metadata = extractMetadataFromSPIRV(spirvBlob);
    metadata.sourceHash     = computeSourceHash(source);
    metadata.shaderFormat   = runtimeFormat;

    std::vector<uint8_t> spirvBytes(
        reinterpret_cast<const uint8_t *>(spirvBlob.data()),
        reinterpret_cast<const uint8_t *>(spirvBlob.data() + spirvBlob.size()));

    _saveCachedShader(cachePath, spirvBytes);
    _saveCachedMetadata(metadataPath, metadata);

    _metadataCache[filename] = metadata;

    LOG_INFO("Compiled and cached shader: {} ({} bytes)", filename.c_str(), spirvBytes.size());

    filedata.fileDataVector = std::move(spirvBytes);
    filedata.data           = filedata.fileDataVector.data();
    filedata.fileSize       = filedata.fileDataVector.size();

    _shaderDataCache[filename]      = filedata;
    _shaderDataCache[filename].data = _shaderDataCache[filename].fileDataVector.data();

    return _shaderDataCache[filename];
}

ShaderMetadata Shaders::_getShaderMetadata(const std::string &filename) {
    auto it = _metadataCache.find(filename);
    if (it != _metadataCache.end())
        return it->second;

    std::string    metadataPath = getMetadataPath(filename);
    ShaderMetadata metadata;
    if (_loadCachedMetadata(metadataPath, metadata)) {
        _metadataCache[filename] = metadata;
        return metadata;
    }

    // If not cached, compile the shader (which will generate metadata).
    _getShader(filename);

    it = _metadataCache.find(filename);
    if (it != _metadataCache.end())
        return it->second;

    LOG_WARNING("Could not get metadata for {}", filename.c_str());
    return ShaderMetadata();
}

uint32_t Shaders::_getShaderFormat(const std::string &filename) {
    return _getShaderMetadata(filename).shaderFormat;
}

GpuShaderHandle Shaders::_createGpuShader(const std::string &filename, GpuShaderStage stage) {
    PhysFSFileData shaderData = _getShader(filename);
    ShaderMetadata metadata   = _getShaderMetadata(filename);

    if (stage != GpuShaderStage::Vertex && stage != GpuShaderStage::Fragment) {
        LOG_ERROR("Unsupported shader stage");
        return 0;
    }

    // Asset shaders ship as SPIRV; the backend translates for the running device.
    GpuShaderCreateInfo info {};
    info.code                = static_cast<const uint8_t *>(shaderData.data);
    info.codeSize            = static_cast<size_t>(shaderData.fileSize);
    info.entrypoint          = "main";
    info.stage               = stage;
    info.samplerCount        = metadata.numSamplers;
    info.uniformBufferCount  = metadata.numUniformBuffers;
    info.storageBufferCount  = metadata.numStorageBuffers;
    info.storageTextureCount = metadata.numStorageTextures;

    GpuShaderHandle shader = Renderer::GetGpu().CreateShaderFromSPIRV(info);
    if (!shader)
        LOG_ERROR("Failed to create GPU shader for {}", filename.c_str());
    return shader;
}

ShaderAsset Shaders::_createShaderAsset(const std::string &filename, GpuShaderStage stage) {
    ShaderAsset    asset      = {};
    PhysFSFileData shaderData = _getShader(filename);
    ShaderMetadata metadata   = _getShaderMetadata(filename);

    asset.shaderFilename      = filename;
    asset.fileData            = shaderData.fileDataVector;
    asset.samplerCount        = metadata.numSamplers;
    asset.uniformBufferCount  = metadata.numUniformBuffers;
    asset.storageBufferCount  = metadata.numStorageBuffers;
    asset.storageTextureCount = metadata.numStorageTextures;

    // Copy reflected uniform layout so consumers (EffectHandler etc.) can read it the
    // same way on both backends — WebGPU populates these at shader-load time.
    asset.uniformOffsets = metadata.uniformOffsets;
    asset.uniformSizes   = metadata.uniformSizes;

    asset.gpuShader = _createGpuShader(filename, stage);

    LOG_INFO("Created ShaderAsset for {} (format={}, samplers={})",
        filename.c_str(), metadata.shaderFormat, asset.samplerCount);
    return asset;
}

ComputePipelineAsset Shaders::_createComputePipeline(const std::string &filename) {
    PhysFSFileData shaderData = _getShader(filename);
    if (!shaderData.data || shaderData.fileSize == 0) {
        LOG_ERROR("Failed to load compute shader: {}", filename);
        return {};
    }

    ComputePipelineAsset asset = _createComputePipelineFromBytes(
        static_cast<const uint8_t *>(shaderData.data), static_cast<size_t>(shaderData.fileSize));
    asset.filename = filename;

    if (!asset.pipeline) {
        LOG_ERROR("Failed to create compute pipeline {}", filename);
    } else {
        LOG_INFO("Created compute pipeline: {} (threads: {}x{}x{})",
            filename, asset.threadCountX, asset.threadCountY, asset.threadCountZ);
    }
    return asset;
}

ComputePipelineAsset Shaders::_createComputePipelineFromBytes(const uint8_t *spirvBytes, size_t spirvSize) {
    // The backend reflects the shader's resource layout while translating it.
    GpuComputeReflection     refl {};
    GpuComputePipelineHandle pipeline = Renderer::GetGpu().CreateComputePipelineFromSPIRV(spirvBytes, spirvSize, "main", &refl);

    ComputePipelineAsset asset;
    asset.filename                    = "<embedded>";
    asset.pipeline                    = pipeline;
    asset.threadCountX                = refl.threadCountX;
    asset.threadCountY                = refl.threadCountY;
    asset.threadCountZ                = refl.threadCountZ;
    asset.numSamplers                 = refl.samplerCount;
    asset.numReadonlyStorageTextures  = refl.readonlyStorageTextureCount;
    asset.numReadwriteStorageTextures = refl.readwriteStorageTextureCount;
    asset.numReadonlyStorageBuffers   = refl.readonlyStorageBufferCount;
    asset.numReadwriteStorageBuffers  = refl.readwriteStorageBufferCount;
    asset.numUniformBuffers           = refl.uniformBufferCount;

    if (!asset.pipeline)
        LOG_ERROR("Shaders::CreateComputePipelineFromBytes: failed to create pipeline");

    return asset;
}

// ── GLSL → SPIRV via glslang ─────────────────────────────────────────────────────
static std::vector<uint32_t> compileGLSLtoSPIRV(const std::string &source, EShLanguage shaderStage) {
    glslang::InitializeProcess();

    glslang::TShader shader(shaderStage);
    const char      *sourceCStr = source.c_str();
    shader.setStrings(&sourceCStr, 1);

    shader.setEnvInput(glslang::EShSourceGlsl, shaderStage, glslang::EShClientVulkan, 450);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_1);

    TBuiltInResource resources;
    fillResources(&resources);

    if (!shader.parse(&resources, 450, EProfile::ECoreProfile, false, true, EShMsgDefault)) {
        LOG_ERROR("GLSL parsing failed: {}", shader.getInfoLog());
        LOG_ERROR("Debug log: {}", shader.getInfoDebugLog());
        glslang::FinalizeProcess();
        return {};
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(EShMsgDefault)) {
        LOG_ERROR("Program linking failed: {}", program.getInfoLog());
        glslang::FinalizeProcess();
        return {};
    }

    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(shaderStage), spirv);
    glslang::FinalizeProcess();
    return spirv;
}

static void fillResources(TBuiltInResource *resource) {
    resource->maxLights                                 = 32;
    resource->maxClipPlanes                             = 6;
    resource->maxTextureUnits                           = 32;
    resource->maxTextureCoords                          = 32;
    resource->maxVertexAttribs                          = 64;
    resource->maxVertexUniformComponents                = 4096;
    resource->maxVaryingFloats                          = 64;
    resource->maxVertexTextureImageUnits                = 32;
    resource->maxCombinedTextureImageUnits              = 80;
    resource->maxTextureImageUnits                      = 32;
    resource->maxFragmentUniformComponents              = 4096;
    resource->maxDrawBuffers                            = 32;
    resource->maxVertexUniformVectors                   = 128;
    resource->maxVaryingVectors                         = 8;
    resource->maxFragmentUniformVectors                 = 16;
    resource->maxVertexOutputVectors                    = 16;
    resource->maxFragmentInputVectors                   = 15;
    resource->minProgramTexelOffset                     = -8;
    resource->maxProgramTexelOffset                     = 7;
    resource->maxClipDistances                          = 8;
    resource->maxComputeWorkGroupCountX                 = 65535;
    resource->maxComputeWorkGroupCountY                 = 65535;
    resource->maxComputeWorkGroupCountZ                 = 65535;
    resource->maxComputeWorkGroupSizeX                  = 1024;
    resource->maxComputeWorkGroupSizeY                  = 1024;
    resource->maxComputeWorkGroupSizeZ                  = 64;
    resource->maxComputeUniformComponents               = 1024;
    resource->maxComputeTextureImageUnits               = 16;
    resource->maxComputeImageUniforms                   = 8;
    resource->maxComputeAtomicCounters                  = 8;
    resource->maxComputeAtomicCounterBuffers            = 1;
    resource->maxVaryingComponents                      = 60;
    resource->maxVertexOutputComponents                 = 64;
    resource->maxGeometryInputComponents                = 64;
    resource->maxGeometryOutputComponents               = 128;
    resource->maxFragmentInputComponents                = 128;
    resource->maxImageUnits                             = 8;
    resource->maxCombinedImageUnitsAndFragmentOutputs   = 8;
    resource->maxCombinedShaderOutputResources          = 8;
    resource->maxImageSamples                           = 0;
    resource->maxVertexImageUniforms                    = 0;
    resource->maxTessControlImageUniforms               = 0;
    resource->maxTessEvaluationImageUniforms            = 0;
    resource->maxGeometryImageUniforms                  = 0;
    resource->maxFragmentImageUniforms                  = 8;
    resource->maxCombinedImageUniforms                  = 8;
    resource->maxGeometryTextureImageUnits              = 16;
    resource->maxGeometryOutputVertices                 = 256;
    resource->maxGeometryTotalOutputComponents          = 1024;
    resource->maxGeometryUniformComponents              = 1024;
    resource->maxGeometryVaryingComponents              = 64;
    resource->maxTessControlInputComponents             = 128;
    resource->maxTessControlOutputComponents            = 128;
    resource->maxTessControlTextureImageUnits           = 16;
    resource->maxTessControlUniformComponents           = 1024;
    resource->maxTessControlTotalOutputComponents       = 4096;
    resource->maxTessEvaluationInputComponents          = 128;
    resource->maxTessEvaluationOutputComponents         = 128;
    resource->maxTessEvaluationTextureImageUnits        = 16;
    resource->maxTessEvaluationUniformComponents        = 1024;
    resource->maxTessPatchComponents                    = 120;
    resource->maxPatchVertices                          = 32;
    resource->maxTessGenLevel                           = 64;
    resource->maxViewports                              = 16;
    resource->maxVertexAtomicCounters                   = 0;
    resource->maxTessControlAtomicCounters              = 0;
    resource->maxTessEvaluationAtomicCounters           = 0;
    resource->maxGeometryAtomicCounters                 = 0;
    resource->maxFragmentAtomicCounters                 = 8;
    resource->maxCombinedAtomicCounters                 = 8;
    resource->maxAtomicCounterBindings                  = 1;
    resource->maxVertexAtomicCounterBuffers             = 0;
    resource->maxTessControlAtomicCounterBuffers        = 0;
    resource->maxTessEvaluationAtomicCounterBuffers     = 0;
    resource->maxGeometryAtomicCounterBuffers           = 0;
    resource->maxFragmentAtomicCounterBuffers           = 1;
    resource->maxCombinedAtomicCounterBuffers           = 1;
    resource->maxAtomicCounterBufferSize                = 16384;
    resource->maxTransformFeedbackBuffers               = 4;
    resource->maxTransformFeedbackInterleavedComponents = 64;
    resource->maxCullDistances                          = 8;
    resource->maxCombinedClipAndCullDistances           = 8;
    resource->maxSamples                                = 4;
    resource->maxMeshOutputVerticesNV                   = 256;
    resource->maxMeshOutputPrimitivesNV                 = 512;
    resource->maxMeshWorkGroupSizeX_NV                  = 32;
    resource->maxMeshWorkGroupSizeY_NV                  = 1;
    resource->maxMeshWorkGroupSizeZ_NV                  = 1;
    resource->maxTaskWorkGroupSizeX_NV                  = 32;
    resource->maxTaskWorkGroupSizeY_NV                  = 1;
    resource->maxTaskWorkGroupSizeZ_NV                  = 1;
    resource->maxMeshViewCountNV                        = 4;

    resource->limits.nonInductiveForLoops                 = 1;
    resource->limits.whileLoops                           = 1;
    resource->limits.doWhileLoops                         = 1;
    resource->limits.generalUniformIndexing               = 1;
    resource->limits.generalAttributeMatrixVectorIndexing = 1;
    resource->limits.generalVaryingIndexing               = 1;
    resource->limits.generalSamplerIndexing               = 1;
    resource->limits.generalVariableIndexing              = 1;
    resource->limits.generalConstantMatrixVectorIndexing  = 1;
}
/// @endcond
