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
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>

#include "assethandler/assethandler.h"

class Shaders {
public:

    static void Init() {
        get()._init();
    }

    static PhysFSFileData GetShader(const std::string &filename) { return get()._getShader(filename); }

private:

    void _init();

    PhysFSFileData _getShader(const std::string &filename);

    std::vector<uint32_t> _compileGLSLtoSPIRV(const std::string &source, EShLanguage shaderStage);

    std::string _convertSPIRVtoHLSL(const std::vector<uint32_t> &spirv);

    std::string _convertSPIRVtoMSL(const std::vector<uint32_t> &spirv);

    struct ResourceBuffer : public std::streambuf {
        ResourceBuffer(std::ifstream &ifs, uint32_t offset, uint32_t size);

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

    ResourcePack shaderCache;

public:
    Shaders(const Shaders &) = delete;

    static Shaders &get() {
        static Shaders instance;
        return instance;
    }

private:
    Shaders() : shaderCache("shader.cache", "") {
    };

    void _fillResources(TBuiltInResource *pResource);
};
