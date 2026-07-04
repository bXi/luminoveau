#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <streambuf>
#include <vector>

/// @brief A std::streambuf backed by an in-memory slice of a resource pack file.
struct ResourceBuffer : public std::streambuf {
    /// @brief Constructs an empty buffer.
    ResourceBuffer() = default;
    /// @brief Reads a [offset, offset+size) region of a pack file into memory.
    /// @param ifs Open input stream positioned on the pack file.
    /// @param offset Byte offset of the resource within the file.
    /// @param size Byte length of the resource.
    ResourceBuffer(std::ifstream &ifs, uint32_t offset, uint32_t size);

    /// @brief Points the streambuf get-area at the in-memory buffer.
    void SetupMemoryBuffer() {
        setg(
            reinterpret_cast<char*>(vMemory.data()),
            reinterpret_cast<char*>(vMemory.data()),
            reinterpret_cast<char*>(vMemory.data() + vMemory.size())
        );
    }

    std::vector<uint8_t> vMemory;  ///< In-memory copy of the resource bytes.
};

/// @brief A bundle of files packed into a single (optionally XOR-scrambled) archive.
class ResourcePack : public std::streambuf {
public:
    /// @brief Opens or prepares a resource pack.
    /// @param sFile Path to the pack file.
    /// @param sKey Scramble key (empty for none).
    ResourcePack(std::string sFile, std::string sKey);
    /// @brief Closes the pack and releases its resources.
    ~ResourcePack();

    /// @brief Adds a file from disk to the pack.
    /// @param sFile Path of the file to add.
    /// @return True on success.
    bool AddFile(const std::string &sFile);
    /// @brief Adds a file to the pack from an in-memory byte buffer.
    /// @param sFile Name to store the entry under.
    /// @param bytes The file contents.
    /// @return True on success.
    bool AddFile(const std::string &sFile, std::vector<unsigned char> bytes);
    /// @brief Returns true if the pack contains the named file.
    bool HasFile(const std::string &sFile);
    /// @brief Loads the pack's index from disk.
    bool LoadPack();
    /// @brief Writes the pack (index + data) to disk.
    bool SavePack();
    /// @brief Returns a streambuf over the named file's bytes within the pack.
    ResourceBuffer GetFileBuffer(const std::string &sFile);
    /// @brief Returns true if a pack has been loaded.
    bool Loaded();

private:
    enum class eResourceType { File, ByteArray };

    /// @cond INTERNAL
    struct sResourceFile {
        uint32_t                   nSize;
        uint32_t                   nOffset;
        eResourceType              eType  = eResourceType::File;
        std::vector<unsigned char> aBytes = {};
    };
    /// @endcond

    std::string                          _fileName;
    std::string                          _key;
    std::map<std::string, sResourceFile> mapFiles;
    std::ifstream                        baseFile;

    std::vector<char> scramble(const std::vector<char> &data, const std::string &key);
    std::string makeposix(const std::string &path);
};
