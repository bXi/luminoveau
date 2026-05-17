#pragma once

#include <string>
#include <vector>
#include <cstdint>

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    virtual bool        exists(const std::string& path) = 0;
    virtual bool        isDirectory(const std::string& path) = 0;

    virtual std::vector<uint8_t> readFile(const std::string& path) = 0;
    virtual bool writeFile(const std::string& path,
                           const uint8_t* data, size_t size) = 0;

    virtual std::vector<std::string> listDirectory(const std::string& path) = 0;

    virtual bool mountPack(const std::string& packPath,
                           const std::string& mountPoint = "/") = 0;
    virtual void unmountPack(const std::string& packPath) = 0;

    virtual std::string getBasePath() = 0;
    virtual std::string getPrefPath(const std::string& org,
                                    const std::string& app) = 0;
};
