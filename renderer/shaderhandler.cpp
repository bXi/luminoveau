#include "shaderhandler.h"
#include "rendererhandler.h"
#include "../assettypes/shader.h"
#include "../log/loghandler.h"

#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>

// Simple text serialization for ShaderMetadata
std::string ShaderMetadata::serialize() const {
    std::ostringstream oss;
    
    oss << "source_hash=" << source_hash << "\n";
    oss << "shader_format=" << static_cast<int>(shader_format) << "\n";
    oss << "num_samplers=" << num_samplers << "\n";
    oss << "num_uniform_buffers=" << num_uniform_buffers << "\n";
    oss << "num_storage_buffers=" << num_storage_buffers << "\n";
    oss << "num_storage_textures=" << num_storage_textures << "\n";
    
    // Write sampler names
    for (size_t i = 0; i < sampler_names.size(); ++i) {
        oss << "sampler_" << i << "=" << sampler_names[i] << "\n";
    }
    
    // Write uniforms
    for (const auto& [name, offset] : uniform_offsets) {
        oss << "uniform_" << name << "_offset=" << offset << "\n";
        oss << "uniform_" << name << "_size=" << uniform_sizes.at(name) << "\n";
    }
    
    return oss.str();
}

ShaderMetadata ShaderMetadata::deserialize(const std::string& data) {
    ShaderMetadata metadata;
    std::istringstream iss(data);
    std::string line;
    
    while (std::getline(iss, line)) {
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        
        if (key == "source_hash") {
            metadata.source_hash = value;
        } else if (key == "shader_format") {
            metadata.shader_format = static_cast<SDL_GPUShaderFormat>(std::stoi(value));
        } else if (key == "num_samplers") {
            metadata.num_samplers = std::stoul(value);
        } else if (key == "num_uniform_buffers") {
            metadata.num_uniform_buffers = std::stoul(value);
        } else if (key == "num_storage_buffers") {
            metadata.num_storage_buffers = std::stoul(value);
        } else if (key == "num_storage_textures") {
            metadata.num_storage_textures = std::stoul(value);
        } else if (key.find("sampler_") == 0) {
            metadata.sampler_names.push_back(value);
        } else if (key.find("uniform_") == 0 && key.find("_offset") != std::string::npos) {
            size_t uniform_pos = key.find("_offset");
            std::string uniform_name = key.substr(8, uniform_pos - 8); // Skip "uniform_"
            metadata.uniform_offsets[uniform_name] = std::stoul(value);
        } else if (key.find("uniform_") == 0 && key.find("_size") != std::string::npos) {
            size_t uniform_pos = key.find("_size");
            std::string uniform_name = key.substr(8, uniform_pos - 8); // Skip "uniform_"
            metadata.uniform_sizes[uniform_name] = std::stoul(value);
        }
    }
    
    return metadata;
}

void Shaders::_init() {
    // Initialize SDL_shadercross
    if (!SDL_ShaderCross_Init()) {
        LOG_CRITICAL("Failed to initialize SDL_shadercross: {}", SDL_GetError());
    }
    LOG_INFO("SDL_shadercross initialized successfully");
    
    // Load shader cache resource pack
    shaderCache = new ResourcePack("shader.cache", "luminoveau_shaders");
    if (!shaderCache->Loaded()) {
        LOG_INFO("No existing shader cache found, will create on first save");
    } else {
        LOG_INFO("Successfully loaded existing shader cache from shader.cache");
    }
}

void Shaders::_quit() {
    // Save shader cache before shutting down
    if (shaderCache) {
        LOG_INFO("Saving shader cache (cached {} shaders)...", metadataCache.size());
        if (shaderCache->SavePack()) {
            LOG_INFO("Shader cache saved successfully to shader.cache");
        } else {
            LOG_ERROR("Failed to save shader cache!");
        }
    }
    delete shaderCache;
    shaderCache = nullptr;
    
    SDL_ShaderCross_Quit();
    LOG_INFO("SDL_shadercross shut down");
}

std::string Shaders::_computeSourceHash(const std::string &source) {
    // Simple hash using std::hash
    std::hash<std::string> hasher;
    size_t hash = hasher(source);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

std::string Shaders::_getCachePath(const std::string &filename, const std::string &extension) {
    // Just return the key for resource pack lookup
    std::string safeName = filename;
    std::replace(safeName.begin(), safeName.end(), '/', '_');
    std::replace(safeName.begin(), safeName.end(), '\\', '_');
    return safeName + extension;
}

std::string Shaders::_getMetadataPath(const std::string &filename) {
    return _getCachePath(filename, ".meta");
}

bool Shaders::_loadCachedShader(const std::string &cacheKey, std::vector<uint8_t> &outData) {
    if (!shaderCache || !shaderCache->HasFile(cacheKey)) {
        return false;
    }
    
    try {
        auto buffer = shaderCache->GetFileBuffer(cacheKey);
        outData.assign(buffer.vMemory.begin(), buffer.vMemory.end());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load cached shader {}: {}", cacheKey.c_str(), e.what());
        return false;
    }
}

bool Shaders::_loadCachedMetadata(const std::string &metadataKey, ShaderMetadata &outMetadata) {
    if (!shaderCache || !shaderCache->HasFile(metadataKey)) {
        return false;
    }
    
    try {
        auto buffer = shaderCache->GetFileBuffer(metadataKey);
        std::string metadataStr(buffer.vMemory.begin(), buffer.vMemory.end());
        outMetadata = ShaderMetadata::deserialize(metadataStr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse metadata from cache: {}", e.what());
        return false;
    }
}

void Shaders::_saveCachedShader(const std::string &cacheKey, const std::vector<uint8_t> &data) {
    if (shaderCache) {
        shaderCache->AddFile(cacheKey, data);
        // Save immediately - don't wait for shutdown
        if (shaderCache->SavePack()) {
            LOG_INFO("Cache saved to shader.cache");
        } else {
            LOG_WARNING("Failed to save cache!");
        }
    } else {
        LOG_WARNING("Cannot cache shader {} - shaderCache is null!", cacheKey.c_str());
    }
}

void Shaders::_saveCachedMetadata(const std::string &metadataKey, const ShaderMetadata &metadata) {
    if (shaderCache) {
        std::string metadataStr = metadata.serialize();
        std::vector<uint8_t> metadataBytes(metadataStr.begin(), metadataStr.end());
        shaderCache->AddFile(metadataKey, metadataBytes);
        // Save immediately - don't wait for shutdown
        if (shaderCache->SavePack()) {
            LOG_INFO("Cache saved to shader.cache");
        } else {
            LOG_WARNING("Failed to save cache!");
        }
    } else {
        LOG_WARNING("Cannot cache metadata {} - shaderCache is null!", metadataKey.c_str());
    }
}

ShaderMetadata Shaders::_extractMetadataFromSPIRV(const std::vector<uint32_t> &spirv) {
    ShaderMetadata metadata;
    
    try {
        spirv_cross::Compiler compiler(spirv);
        auto resources = compiler.get_shader_resources();
        
        // Extract sampler names
        for (const auto &sampler : resources.sampled_images) {
            const std::string &samplerName = compiler.get_name(sampler.id);
            metadata.sampler_names.push_back(samplerName);
        }
        metadata.num_samplers = static_cast<uint32_t>(metadata.sampler_names.size());
        
        // Extract uniform buffer info
        for (const auto &uniform : resources.uniform_buffers) {
            auto &bufferType = compiler.get_type(uniform.base_type_id);
            
            for (size_t i = 0; i < bufferType.member_types.size(); ++i) {
                const std::string &memberName = compiler.get_member_name(uniform.base_type_id, i);
                size_t memberSize = compiler.get_declared_struct_member_size(bufferType, i);
                size_t memberOffset = compiler.type_struct_member_offset(bufferType, i);
                
                metadata.uniform_offsets[memberName] = memberOffset;
                metadata.uniform_sizes[memberName] = memberSize;
            }
        }
        metadata.num_uniform_buffers = static_cast<uint32_t>(resources.uniform_buffers.size());
        
        // Count storage buffers and textures
        metadata.num_storage_buffers = static_cast<uint32_t>(resources.storage_buffers.size());
        metadata.num_storage_textures = static_cast<uint32_t>(resources.storage_images.size());
        
    } catch (const std::exception &e) {
        LOG_ERROR("SPIRV reflection failed: {}", e.what());
    }
    
    return metadata;
}

PhysFSFileData Shaders::_getShader(const std::string &filename) {
    // Check in-memory cache first (for multiple calls in same frame)
    auto cacheIt = shaderDataCache.find(filename);
    if (cacheIt != shaderDataCache.end()) {
        return cacheIt->second;
    }
    
    PhysFSFileData filedata;
    
    // Determine shader stage from filename
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
    
    // We cache SPIRV and cross-compile at runtime
    // (SDL_shadercross doesn't expose DXIL bytecode extraction)
    auto formats = SDL_GetGPUShaderFormats(Renderer::GetDevice());
    SDL_GPUShaderFormat runtimeFormat;
    std::string formatExt = ".spv";  // Always cache SPIRV
    
    const char* driver = SDL_GetGPUDeviceDriver(Renderer::GetDevice());
    
    if (strcmp(driver, "direct3d12") == 0 || strcmp(driver, "direct3d11") == 0) {
        runtimeFormat = (formats & SDL_GPU_SHADERFORMAT_DXIL) ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_DXBC;
    } else if (strcmp(driver, "metal") == 0) {
        runtimeFormat = SDL_GPU_SHADERFORMAT_METALLIB;
    } else {
        runtimeFormat = SDL_GPU_SHADERFORMAT_SPIRV;
    }
    
    // Try to load from cache
    std::string cachePath = _getCachePath(filename, formatExt);
    std::string metadataPath = _getMetadataPath(filename);
    
    std::vector<uint8_t> cachedData;
    ShaderMetadata cachedMetadata;
    
    if (_loadCachedShader(cachePath, cachedData) && _loadCachedMetadata(metadataPath, cachedMetadata)) {
        // Load source to verify hash
        auto sourceFile = FileHandler::GetFileFromPhysFS(filename);
        std::string source(static_cast<char*>(sourceFile.data), sourceFile.fileSize);
        std::string sourceHash = _computeSourceHash(source);
        
        if (sourceHash == cachedMetadata.source_hash) {
            LOG_INFO("Loaded cached shader: {}", filename.c_str());
            filedata.fileDataVector = std::move(cachedData);
            filedata.data = filedata.fileDataVector.data();
            filedata.fileSize = filedata.fileDataVector.size();
            
            // Store in memory cache and metadata cache
            shaderDataCache[filename] = filedata;
            metadataCache[filename] = cachedMetadata;
            
            return filedata;
        } else {
            LOG_INFO("Cache invalid for {} (source changed), recompiling", filename.c_str());
        }
    }
    
    // Cache miss or invalid - compile from source
    LOG_INFO("Compiling shader: {}", filename.c_str());
    
    // Load GLSL source
    auto sourceFile = FileHandler::GetFileFromPhysFS(filename);
    std::string source(static_cast<char*>(sourceFile.data), sourceFile.fileSize);
    
    // Compile GLSL -> SPIRV
    auto spirvBlob = _compileGLSLtoSPIRV(source, shaderStage);
    if (spirvBlob.empty()) {
        LOG_CRITICAL("failed to compile shader to SPIRV: {}", filename);
    }
    
    // Extract metadata from SPIRV
    ShaderMetadata metadata = _extractMetadataFromSPIRV(spirvBlob);
    metadata.source_hash = _computeSourceHash(source);
    metadata.shader_format = runtimeFormat;
    
    // Cache SPIRV bytecode
    std::vector<uint8_t> spirvBytes(
        reinterpret_cast<const uint8_t*>(spirvBlob.data()),
        reinterpret_cast<const uint8_t*>(spirvBlob.data() + spirvBlob.size())
    );
    
    // Save to cache
    _saveCachedShader(cachePath, spirvBytes);
    _saveCachedMetadata(metadataPath, metadata);
    
    // Store metadata in memory
    metadataCache[filename] = metadata;
    
    LOG_INFO("Compiled and cached shader: {} ({} bytes)", filename.c_str(), spirvBytes.size());
    
    filedata.fileDataVector = std::move(spirvBytes);
    filedata.data = filedata.fileDataVector.data();
    filedata.fileSize = filedata.fileDataVector.size();
    
    // Store in memory cache
    shaderDataCache[filename] = filedata;
    
    return filedata;
}

ShaderMetadata Shaders::_getShaderMetadata(const std::string &filename) {
    // Check memory cache first
    auto it = metadataCache.find(filename);
    if (it != metadataCache.end()) {
        return it->second;
    }
    
    // Try to load from disk cache
    std::string metadataPath = _getMetadataPath(filename);
    ShaderMetadata metadata;
    
    if (_loadCachedMetadata(metadataPath, metadata)) {
        metadataCache[filename] = metadata;
        return metadata;
    }
    
    // If not cached, compile the shader (which will generate metadata)
    _getShader(filename);
    
    // Metadata should now be in cache
    it = metadataCache.find(filename);
    if (it != metadataCache.end()) {
        return it->second;
    }
    
    // Fallback: return empty metadata
    LOG_WARNING("Could not get metadata for {}", filename.c_str());
    return ShaderMetadata();
}

SDL_GPUShaderFormat Shaders::_getShaderFormat(const std::string &filename) {
    // Get metadata which includes the format
    ShaderMetadata metadata = _getShaderMetadata(filename);
    return metadata.shader_format;
}

SDL_GPUShader* Shaders::CreateGPUShader(SDL_GPUDevice* device, const std::string& filename, SDL_GPUShaderStage stage) {
    // Load SPIRV shader data from cache
    PhysFSFileData shaderData = GetShader(filename);
    ShaderMetadata metadata = GetShaderMetadata(filename);
    
    // Convert SDL_GPUShaderStage to SDL_ShaderCross_ShaderStage
    SDL_ShaderCross_ShaderStage crossStage;
    if (stage == SDL_GPU_SHADERSTAGE_VERTEX) {
        crossStage = SDL_SHADERCROSS_SHADERSTAGE_VERTEX;
    } else if (stage == SDL_GPU_SHADERSTAGE_FRAGMENT) {
        crossStage = SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
    } else {
        LOG_ERROR("Unsupported shader stage");
        return nullptr;
    }
    
    // Create SPIRV info struct
    SDL_ShaderCross_SPIRV_Info spirvInfo = {
        .bytecode = static_cast<const Uint8*>(shaderData.data),
        .bytecode_size = static_cast<size_t>(shaderData.fileSize),
        .entrypoint = "main",
        .shader_stage = crossStage,
        .props = 0
    };
    
    // Create resource info struct from metadata
    SDL_ShaderCross_GraphicsShaderResourceInfo resourceInfo = {
        .num_samplers = metadata.num_samplers,
        .num_storage_textures = metadata.num_storage_textures,
        .num_storage_buffers = metadata.num_storage_buffers,
        .num_uniform_buffers = metadata.num_uniform_buffers
    };
    
    // Cross-compile SPIRV to backend format and create shader
    SDL_GPUShader* shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
        device,
        &spirvInfo,
        &resourceInfo,
        0
    );
    
    if (!shader) {
        LOG_ERROR("Failed to create GPU shader for {}: {}", filename.c_str(), SDL_GetError());
    }
    
    return shader;
}

ShaderAsset Shaders::CreateShaderAsset(SDL_GPUDevice* device, const std::string& filename, SDL_GPUShaderStage stage) {
    ShaderAsset asset = {};
    
    // Load shader data and metadata
    PhysFSFileData shaderData = GetShader(filename);
    ShaderMetadata metadata = GetShaderMetadata(filename);
    
    // Store filename and file data
    asset.shaderFilename = filename;
    asset.fileData = shaderData.fileDataVector;
    
    // Store resource counts
    asset.samplerCount = metadata.num_samplers;
    asset.uniformBufferCount = metadata.num_uniform_buffers;
    asset.storageBufferCount = metadata.num_storage_buffers;
    asset.storageTextureCount = metadata.num_storage_textures;
    
    // Create the GPU shader with correct format
    asset.shader = CreateGPUShader(device, filename, stage);
    
    LOG_INFO("Created ShaderAsset for {} (format={}, samplers={})",
            filename.c_str(), metadata.shader_format, asset.samplerCount);
    
    return asset;
}

std::vector<uint32_t> Shaders::_compileGLSLtoSPIRV(const std::string& source, EShLanguage shaderStage) {
    glslang::InitializeProcess();

    glslang::TShader shader(shaderStage);
    const char* sourceCStr = source.c_str();
    shader.setStrings(&sourceCStr, 1);

    shader.setEnvInput(glslang::EShSourceGlsl, shaderStage, glslang::EShClientVulkan, 450);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_1);

    TBuiltInResource resources;
    _fillResources(&resources);

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

void Shaders::_fillResources(TBuiltInResource *resource) {
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
