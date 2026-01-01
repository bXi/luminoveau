#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <cstdint>

#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <spirv_cross.hpp>

#include <SDL3_shadercross/SDL_shadercross.h>

#include "assethandler/assethandler.h"

// Simple text-based shader metadata structure
struct ShaderMetadata {
    std::string source_hash;
    std::vector<std::string> sampler_names;
    std::unordered_map<std::string, size_t> uniform_offsets;
    std::unordered_map<std::string, size_t> uniform_sizes;
    uint32_t num_samplers = 0;
    uint32_t num_uniform_buffers = 0;
    uint32_t num_storage_buffers = 0;
    uint32_t num_storage_textures = 0;
    SDL_GPUShaderFormat shader_format = SDL_GPU_SHADERFORMAT_SPIRV; // Store the format!
    
    // Simple text serialization
    std::string serialize() const;
    static ShaderMetadata deserialize(const std::string& data);
};

class Shaders {
public:

    static void Init() {
        get()._init();
    }
    
    static void Quit() {
        get()._quit();
    }

    static PhysFSFileData GetShader(const std::string &filename) { return get()._getShader(filename); }
    
    static ShaderMetadata GetShaderMetadata(const std::string &filename) { return get()._getShaderMetadata(filename); }
    
    static SDL_GPUShaderFormat GetShaderFormat(const std::string &filename) { return get()._getShaderFormat(filename); }
    
    // Helper to create SDL_GPUShader with correct format
    static SDL_GPUShader* CreateGPUShader(SDL_GPUDevice* device, const std::string& filename, SDL_GPUShaderStage stage);
    
    // Helper to create complete ShaderAsset from filename
    static ShaderAsset CreateShaderAsset(SDL_GPUDevice* device, const std::string& filename, SDL_GPUShaderStage stage);

private:

    void _init();
    
    void _quit();

    PhysFSFileData _getShader(const std::string &filename);
    
    ShaderMetadata _getShaderMetadata(const std::string &filename);
    
    SDL_GPUShaderFormat _getShaderFormat(const std::string &filename);

    std::vector<uint32_t> _compileGLSLtoSPIRV(const std::string &source, EShLanguage shaderStage);
    
    ShaderMetadata _extractMetadataFromSPIRV(const std::vector<uint32_t> &spirv);
    
    std::string _computeSourceHash(const std::string &source);
    
    // Cache paths
    std::string _getCachePath(const std::string &filename, const std::string &extension);
    std::string _getMetadataPath(const std::string &filename);
    
    bool _loadCachedShader(const std::string &cachePath, std::vector<uint8_t> &outData);
    bool _loadCachedMetadata(const std::string &metadataPath, ShaderMetadata &outMetadata);
    
    void _saveCachedShader(const std::string &cachePath, const std::vector<uint8_t> &data);
    void _saveCachedMetadata(const std::string &metadataPath, const ShaderMetadata &metadata);

    struct ResourceBuffer : public std::streambuf {
        ResourceBuffer() = default;  // Default constructor for in-memory buffers
        ResourceBuffer(std::ifstream &ifs, uint32_t offset, uint32_t size);
        
        // Setup buffer pointers for in-memory data
        void SetupMemoryBuffer() {
            setg(
                reinterpret_cast<char*>(vMemory.data()),
                reinterpret_cast<char*>(vMemory.data()),
                reinterpret_cast<char*>(vMemory.data() + vMemory.size())
            );
        }

        std::vector<uint8_t> vMemory;
    };

    class ResourcePack : public std::streambuf {
    public:
        ResourcePack(std::string sFile, std::string sKey);

        ~ResourcePack();

        bool AddFile(const std::string &sFile);

        bool AddFile(const std::string &sFile, std::vector<unsigned char> bytes);

        bool HasFile(const std::string &sFile);

        bool LoadPack();

        bool SavePack();

        ResourceBuffer GetFileBuffer(const std::string &sFile);

        bool Loaded();

    private:
        enum class eResourceType {
            File,
            ByteArray
        };

        struct sResourceFile {
            uint32_t                   nSize;
            uint32_t                   nOffset;
            eResourceType              eType  = eResourceType::File;
            std::vector<unsigned char> aBytes = {};
        };
        std::string                          _fileName;
        std::string                          _key;
        std::map<std::string, sResourceFile> mapFiles;
        std::ifstream                        baseFile;

        std::vector<char> scramble(const std::vector<char> &data, const std::string &key);

        std::string makeposix(const std::string &path);
    };

    // Cache storage
    std::unordered_map<std::string, ShaderMetadata> metadataCache;
    std::unordered_map<std::string, PhysFSFileData> shaderDataCache;  // Cache loaded shader bytecode
    ResourcePack* shaderCache = nullptr;  // Runtime shader cache

public:
    Shaders(const Shaders &) = delete;

    static Shaders &get() {
        static Shaders instance;
        return instance;
    }

private:
    Shaders() = default;

    void _fillResources(TBuiltInResource *pResource);
};
