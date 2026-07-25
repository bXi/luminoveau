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
            reinterpret_cast<char *>(memory.data()),
            reinterpret_cast<char *>(memory.data()),
            reinterpret_cast<char *>(memory.data() + memory.size()));
    }

    std::vector<uint8_t> memory; ///< In-memory copy of the resource bytes.
};

/// @brief A bundle of files packed into a single (optionally XOR-scrambled) archive.
class ResourcePack : public std::streambuf {
public:
    /// @brief Opens or prepares a resource pack.
    /// @param fileName Path to the pack file.
    /// @param key Scramble key (empty for none).
    ResourcePack(std::string fileName, std::string key);
    /// @brief Closes the pack and releases its resources.
    ~ResourcePack();

    /// @brief Adds a file from disk to the pack.
    /// @param fileName Path of the file to add.
    /// @return True on success.
    bool AddFile(const std::string &fileName);
    /// @brief Adds a file to the pack from an in-memory byte buffer.
    /// @param fileName Name to store the entry under.
    /// @param bytes The file contents.
    /// @return True on success.
    bool AddFile(const std::string &fileName, std::vector<unsigned char> bytes);
    /// @brief Returns true if the pack contains the named file.
    bool HasFile(const std::string &fileName);
    /// @brief Loads the pack's index from disk.
    bool LoadPack();
    /// @brief Writes the pack (index + data) to disk.
    bool SavePack();
    /// @brief Returns a streambuf over the named file's bytes within the pack.
    ResourceBuffer GetFileBuffer(const std::string &fileName);
    /// @brief Returns true if a pack has been loaded.
    bool Loaded();

private:
    enum class ResourceType { File,
        ByteArray };

    /// @cond INTERNAL
    struct ResourceFile {
        uint32_t                   size;
        uint32_t                   offset;
        ResourceType               type  = ResourceType::File;
        std::vector<unsigned char> bytes = {};
    };
    /// @endcond

    std::string                         _fileName;
    std::string                         _key;
    std::map<std::string, ResourceFile> _mapFiles;
    std::ifstream                       _baseFile;

    std::vector<char> _scramble(const std::vector<char> &data, const std::string &key);
    std::string       _makePosix(const std::string &path);
};
