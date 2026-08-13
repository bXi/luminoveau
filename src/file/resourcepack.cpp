#include "resourcepack.h"
#include "file/filehandler.h"
#include "core/log/log.h"

#include <filesystem>
#include <utility>
#include <cstring>

ResourceBuffer::ResourceBuffer(std::ifstream &ifs, uint32_t offset, uint32_t size) {
    memory.resize(size);
    ifs.seekg(offset);
    ifs.read(reinterpret_cast<char *>(memory.data()), memory.size());
    setg(reinterpret_cast<char *>(memory.data()), reinterpret_cast<char *>(memory.data()), reinterpret_cast<char *>(memory.data() + size));
}

ResourcePack::ResourcePack(std::string fileName, std::string key)
    : _fileName(std::move(fileName))
    , _key(std::move(key)) {
    LoadPack();
}

ResourcePack::~ResourcePack() { _baseFile.close(); }

bool ResourcePack::AddFile(const std::string &fileName) {
    const std::string file = _makePosix(fileName);

    if (std::filesystem::exists(file)) {
        ResourceFile e {};
        e.size          = (uint32_t)std::filesystem::file_size(file);
        e.offset        = 0;
        _mapFiles[file] = e;
        return true;
    }
    return false;
}

bool ResourcePack::AddFile(const std::string &fileName, std::vector<unsigned char> bytes) {
    const std::string file = _makePosix(fileName);

    ResourceFile e {};
    e.type   = ResourceType::ByteArray;
    e.size   = (uint32_t)bytes.size();
    e.offset = 0;
    e.bytes  = std::move(bytes);

    _mapFiles[file] = e;
    return true;
}

bool ResourcePack::HasFile(const std::string &fileName) {
    const std::string file = _makePosix(fileName);
    return (_mapFiles.find(file) != _mapFiles.end());
}

bool ResourcePack::LoadPack() {
    _mapFiles.clear();

    if (!FileHandler::FileExists(_fileName)) {
        return false;
    }

    std::vector<uint8_t> fileData = FileHandler::ReadBinaryFile(_fileName);
    if (fileData.empty()) {
        LOG_ERROR("ResourcePack file is empty or failed to read: {}", _fileName);
        return false;
    }

    // On any parse failure below: drop whatever we've built, discard the bad file
    // so a corrupt/truncated pack doesn't linger and get misread again, and let the
    // caller fall back to rebuilding the cache from scratch.
    auto fail = [&](const char *reason) -> bool {
        LOG_ERROR("ResourcePack: {} in {}, discarding corrupt pack", reason, _fileName);
        _mapFiles.clear();
        std::error_code ec;
        std::filesystem::remove(_fileName, ec);
        return false;
    };

    size_t bufferPos = 0;

    auto readFromBuffer = [&](void *dest, size_t size) -> bool {
        if (bufferPos + size > fileData.size())
            return false;
        std::memcpy(dest, fileData.data() + bufferPos, size);
        bufferPos += size;
        return true;
    };

    uint32_t magic = 0, version = 0;
    if (!readFromBuffer(&magic, sizeof(uint32_t)) || magic != kMagic)
        return fail("bad or missing magic header");
    if (!readFromBuffer(&version, sizeof(uint32_t)) || version != kVersion)
        return fail("unsupported pack version");

    uint32_t indexSize = 0;
    if (!readFromBuffer(&indexSize, sizeof(uint32_t)))
        return fail("truncated index size");

    if (indexSize > fileData.size() - bufferPos)
        return fail("index size exceeds file size");

    std::vector<char> buffer(indexSize);
    for (uint32_t j = 0; j < indexSize; j++) {
        if (bufferPos >= fileData.size())
            return fail("truncated index");
        buffer[j] = fileData[bufferPos++];
    }

    std::vector<char> decoded = _scramble(buffer, _key);
    size_t             pos    = 0;

    // Every field read out of the decoded index is bounds-checked against decoded.size();
    // a corrupt/foreign file decodes to garbage lengths otherwise, which previously caused
    // huge allocations / near-infinite loops / OOB reads that looked like a hang on startup.
    auto read = [&decoded, &pos](void *dst, size_t size) -> bool {
        if (pos + size > decoded.size())
            return false;
        memcpy(dst, decoded.data() + pos, size);
        pos += size;
        return true;
    };

    uint32_t mapEntries = 0;
    if (!read(&mapEntries, sizeof(uint32_t)))
        return fail("truncated map entry count");

    std::map<std::string, ResourceFile> parsed;
    for (uint32_t i = 0; i < mapEntries; i++) {
        uint32_t filePathSize = 0;
        if (!read(&filePathSize, sizeof(uint32_t)))
            return fail("truncated file path size");
        if (filePathSize > decoded.size() - pos)
            return fail("file path size exceeds index size");

        std::string entryName(filePathSize, ' ');
        if (filePathSize > 0 && !read(entryName.data(), filePathSize))
            return fail("truncated file path");

        ResourceFile e {};
        if (!read(&e.size, sizeof(uint32_t)) || !read(&e.offset, sizeof(uint32_t)))
            return fail("truncated file entry");
        parsed[entryName] = e;
    }

    _baseFile.open(_fileName, std::ifstream::binary);
    if (!_baseFile.is_open()) {
        LOG_ERROR("ResourcePack: failed to open base file for reading: {}", _fileName);
        return false;
    }

    _mapFiles = std::move(parsed);
    return true;
}

bool ResourcePack::SavePack() {
    for (auto &[name, entry] : _mapFiles) {
        if (entry.type == ResourceType::File && _baseFile.is_open() && entry.offset > 0) {
            entry.bytes.resize(entry.size);
            _baseFile.seekg(entry.offset);
            _baseFile.read(reinterpret_cast<char *>(entry.bytes.data()), entry.size);
            entry.type = ResourceType::ByteArray;
        }
    }

    if (_baseFile.is_open())
        _baseFile.close();

    // Write to a temp file and rename over the real one so a crash/kill mid-write
    // never leaves a half-written pack behind for LoadPack to choke on next launch.
    std::string   tmpFileName = _fileName + ".tmp";
    std::ofstream ofs(tmpFileName, std::ofstream::binary);
    if (!ofs.is_open())
        return false;

    ofs.write((const char *)&kMagic, sizeof(uint32_t));
    ofs.write((const char *)&kVersion, sizeof(uint32_t));

    uint32_t indexSize = 0;
    ofs.write((char *)&indexSize, sizeof(uint32_t));
    auto mapSize = uint32_t(_mapFiles.size());
    ofs.write((char *)&mapSize, sizeof(uint32_t));
    for (auto &e : _mapFiles) {
        size_t pathSize = e.first.size();
        ofs.write((char *)&pathSize, sizeof(uint32_t));
        ofs.write(e.first.c_str(), (std::streamsize)pathSize);
        ofs.write((char *)&e.second.size, sizeof(uint32_t));
        ofs.write((char *)&e.second.offset, sizeof(uint32_t));
    }

    std::streampos offset = ofs.tellp();
    indexSize             = (uint32_t)offset;
    for (auto &e : _mapFiles) {
        e.second.offset = (uint32_t)offset;
        std::vector<uint8_t> fileBuffer(e.second.size);

        if (e.second.type == ResourceType::ByteArray) {
            fileBuffer = e.second.bytes;
        } else {
            std::ifstream i(e.first, std::ifstream::binary);
            i.read(reinterpret_cast<char *>(fileBuffer.data()), e.second.size);
            i.close();
        }

        ofs.write((char *)fileBuffer.data(), e.second.size);
        offset += e.second.size;
    }

    std::vector<char> stream;
    auto              write = [&stream](const char *data, size_t size) {
        size_t sizeNow = stream.size();
        stream.resize(sizeNow + size);
        memcpy(stream.data() + sizeNow, data, size);
    };

    write((char *)&mapSize, sizeof(uint32_t));
    for (auto &e : _mapFiles) {
        size_t pathSize = e.first.size();
        write((char *)&pathSize, sizeof(uint32_t));
        write(e.first.c_str(), pathSize);
        write((char *)&e.second.size, sizeof(uint32_t));
        write((char *)&e.second.offset, sizeof(uint32_t));
    }
    std::vector<char> indexString    = _scramble(stream, _key);
    auto              indexStringLen = uint32_t(indexString.size());
    ofs.seekp(sizeof(uint32_t) * 2, std::ios::beg); // past magic + version, onto the indexSize slot
    ofs.write((char *)&indexStringLen, sizeof(uint32_t));
    ofs.write(indexString.data(), indexStringLen);
    ofs.close();

    if (_baseFile.is_open())
        _baseFile.close();

    std::error_code ec;
    std::filesystem::rename(tmpFileName, _fileName, ec);
    if (ec) {
        // Cross-device or locked-file rename can fail; fall back to copy+remove.
        std::filesystem::copy_file(tmpFileName, _fileName, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tmpFileName, ec);
        if (ec) {
            LOG_ERROR("ResourcePack: failed to commit pack to {}", _fileName);
            return false;
        }
    }

    _baseFile.open(_fileName, std::ifstream::binary);

    return true;
}

ResourceBuffer ResourcePack::GetFileBuffer(const std::string &fileName) {
    auto &entry = _mapFiles[fileName];

    if (entry.type == ResourceType::ByteArray && !entry.bytes.empty()) {
        ResourceBuffer buffer;
        buffer.memory = entry.bytes;
        buffer.SetupMemoryBuffer();
        return buffer;
    }

    return { _baseFile, entry.offset, entry.size };
}

bool ResourcePack::Loaded() { return _baseFile.is_open(); }

std::vector<char> ResourcePack::_scramble(const std::vector<char> &data, const std::string &key) {
    if (key.empty())
        return data;
    std::vector<char> o;
    size_t            c = 0;
    for (auto s : data)
        o.push_back(s ^ key[(c++) % key.size()]);
    return o;
}

std::string ResourcePack::_makePosix(const std::string &path) {
    std::string o;
    for (auto s : path)
        o += std::string(1, s == '\\' ? '/' : s);
    return o;
}
