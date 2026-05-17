#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <streambuf>
#include <vector>

struct ResourceBuffer : public std::streambuf {
    ResourceBuffer() = default;
    ResourceBuffer(std::ifstream &ifs, uint32_t offset, uint32_t size);
    
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
    enum class eResourceType { File, ByteArray };

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
