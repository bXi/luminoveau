#include "resourcepack.h"
#include "file/filehandler.h"
#include "log/loghandler.h"

#include <filesystem>
#include <utility>
#include <cstring>

ResourceBuffer::ResourceBuffer(std::ifstream &ifs, uint32_t offset, uint32_t size) {
    vMemory.resize(size);
    ifs.seekg(offset);
    ifs.read(reinterpret_cast<char *>(vMemory.data()), vMemory.size());
    setg(reinterpret_cast<char *>(vMemory.data()), reinterpret_cast<char *>(vMemory.data()), reinterpret_cast<char *>(vMemory.data() + size));
}

ResourcePack::ResourcePack(std::string sFile, std::string sKey) : _fileName(std::move(sFile)), _key(std::move(sKey)) {
    LoadPack();
}

ResourcePack::~ResourcePack() { baseFile.close(); }

bool ResourcePack::AddFile(const std::string &sFile) {
    const std::string file = makeposix(sFile);

    if (std::filesystem::exists(file)) {
        sResourceFile e{};
        e.nSize   = (uint32_t) std::filesystem::file_size(file);
        e.nOffset = 0;
        mapFiles[file] = e;
        return true;
    }
    return false;
}

bool ResourcePack::AddFile(const std::string &sFile, std::vector<unsigned char> bytes) {
    const std::string file = makeposix(sFile);

    sResourceFile e{};
    e.eType = eResourceType::ByteArray;
    e.nSize   = (uint32_t) bytes.size();
    e.nOffset = 0;
    e.aBytes = std::move(bytes);

    mapFiles[file] = e;
    return true;
}

bool ResourcePack::HasFile(const std::string &sFile) {
    const std::string file = makeposix(sFile);
    return (mapFiles.find(file) != mapFiles.end());
}

bool ResourcePack::LoadPack() {
    if (!FileHandler::FileExists(_fileName)) {
        return false;
    }

    std::vector<uint8_t> fileData = FileHandler::ReadBinaryFile(_fileName);
    if (fileData.empty()) {
        LOG_ERROR("ResourcePack file is empty or failed to read: {}", _fileName);
        return false;
    }

    size_t bufferPos = 0;

    auto readFromBuffer = [&](void* dest, size_t size) -> bool {
        if (bufferPos + size > fileData.size()) return false;
        std::memcpy(dest, fileData.data() + bufferPos, size);
        bufferPos += size;
        return true;
    };

    uint32_t nIndexSize = 0;
    if (!readFromBuffer(&nIndexSize, sizeof(uint32_t))) return false;

    std::vector<char> buffer(nIndexSize);
    for (uint32_t j = 0; j < nIndexSize; j++) {
        if (bufferPos >= fileData.size()) return false;
        buffer[j] = fileData[bufferPos++];
    }

    std::vector<char> decoded = scramble(buffer, _key);
    size_t            pos     = 0;
    auto              read    = [&decoded, &pos](char *dst, size_t size) {
        memcpy((void *) dst, (const void *) (decoded.data() + pos), size);
        pos += size;
    };

    auto get = [&read]() -> int {
        char c;
        read(&c, 1);
        return c;
    };

    uint32_t nMapEntries = 0;
    read((char *) &nMapEntries, sizeof(uint32_t));
    for (uint32_t i = 0; i < nMapEntries; i++) {
        uint32_t nFilePathSize = 0;
        read((char *) &nFilePathSize, sizeof(uint32_t));

        std::string   sFileName(nFilePathSize, ' ');
        for (uint32_t j = 0; j < nFilePathSize; j++)
            sFileName[j] = get();

        sResourceFile e{};
        read((char *) &e.nSize, sizeof(uint32_t));
        read((char *) &e.nOffset, sizeof(uint32_t));
        mapFiles[sFileName] = e;
    }

    baseFile.open(_fileName, std::ifstream::binary);
    if (!baseFile.is_open()) {
        LOG_ERROR("ResourcePack: failed to open base file for reading: {}", _fileName);
        return false;
    }

    return true;
}

bool ResourcePack::SavePack() {
    for (auto &[name, entry] : mapFiles) {
        if (entry.eType == eResourceType::File && baseFile.is_open() && entry.nOffset > 0) {
            entry.aBytes.resize(entry.nSize);
            baseFile.seekg(entry.nOffset);
            baseFile.read(reinterpret_cast<char*>(entry.aBytes.data()), entry.nSize);
            entry.eType = eResourceType::ByteArray;
        }
    }

    if (baseFile.is_open()) baseFile.close();

    std::ofstream ofs(_fileName, std::ofstream::binary);
    if (!ofs.is_open()) return false;

    uint32_t nIndexSize = 0;
    ofs.write((char *) &nIndexSize, sizeof(uint32_t));
    auto nMapSize = uint32_t(mapFiles.size());
    ofs.write((char *) &nMapSize, sizeof(uint32_t));
    for (auto &e: mapFiles) {
        size_t nPathSize = e.first.size();
        ofs.write((char *) &nPathSize, sizeof(uint32_t));
        ofs.write(e.first.c_str(), (std::streamsize) nPathSize);
        ofs.write((char *) &e.second.nSize, sizeof(uint32_t));
        ofs.write((char *) &e.second.nOffset, sizeof(uint32_t));
    }

    std::streampos offset = ofs.tellp();
    nIndexSize = (uint32_t) offset;
    for (auto &e: mapFiles) {
        e.second.nOffset = (uint32_t) offset;
        std::vector<uint8_t> vBuffer(e.second.nSize);

        if (e.second.eType == eResourceType::ByteArray) {
            vBuffer = e.second.aBytes;
        } else {
            std::ifstream i(e.first, std::ifstream::binary);
            i.read(reinterpret_cast<char*>(vBuffer.data()), e.second.nSize);
            i.close();
        }

        ofs.write((char *) vBuffer.data(), e.second.nSize);
        offset += e.second.nSize;
    }

    std::vector<char> stream;
    auto              write = [&stream](const char *data, size_t size) {
        size_t sizeNow = stream.size();
        stream.resize(sizeNow + size);
        memcpy(stream.data() + sizeNow, data, size);
    };

    write((char *) &nMapSize, sizeof(uint32_t));
    for (auto &e: mapFiles) {
        size_t nPathSize = e.first.size();
        write((char *) &nPathSize, sizeof(uint32_t));
        write(e.first.c_str(), nPathSize);
        write((char *) &e.second.nSize, sizeof(uint32_t));
        write((char *) &e.second.nOffset, sizeof(uint32_t));
    }
    std::vector<char> sIndexString    = scramble(stream, _key);
    auto              nIndexStringLen = uint32_t(sIndexString.size());
    ofs.seekp(0, std::ios::beg);
    ofs.write((char *) &nIndexStringLen, sizeof(uint32_t));
    ofs.write(sIndexString.data(), nIndexStringLen);
    ofs.close();

    if (baseFile.is_open()) baseFile.close();
    baseFile.open(_fileName, std::ifstream::binary);

    return true;
}

ResourceBuffer ResourcePack::GetFileBuffer(const std::string &sFile) {
    auto& entry = mapFiles[sFile];
    
    if (entry.eType == eResourceType::ByteArray && !entry.aBytes.empty()) {
        ResourceBuffer buffer;
        buffer.vMemory = entry.aBytes;
        buffer.SetupMemoryBuffer();
        return buffer;
    }
    
    return {baseFile, entry.nOffset, entry.nSize};
}

bool ResourcePack::Loaded() { return baseFile.is_open(); }

std::vector<char> ResourcePack::scramble(const std::vector<char> &data, const std::string &key) {
    if (key.empty()) return data;
    std::vector<char> o;
    size_t c = 0;
    for (auto s: data) o.push_back(s ^ key[(c++) % key.size()]);
    return o;
}

std::string ResourcePack::makeposix(const std::string &path) {
    std::string o;
    for (auto s: path) o += std::string(1, s == '\\' ? '/' : s);
    return o;
}
