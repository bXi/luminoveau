#include "filehandler.h"
#include "log/loghandler.h"
#include "physfs.h"

#include <iostream>
#include <cstring>
#include <fstream>
#include <filesystem>

#ifdef __ANDROID__
#include "SDL3/SDL.h"
#endif

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

// ============================================================================
// PHYSFS INITIALIZATION
// ============================================================================

void FileHandler::_ensurePhysFS() {
    if (!_physfsInitialized) {
        _initPhysFS();
    }
}

bool FileHandler::_initPhysFS() {
    // Guard against double initialization
    if (_physfsInitialized) {
        LOG_INFO("PhysFS already initialized, skipping");
        return true;
    }
    if (PHYSFS_init(nullptr) == 0) {
        std::cerr << "Failed to initialize PhysFS: "
                  << PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()) << std::endl;
        return false;
    }

#ifdef __ANDROID__
    LOG_INFO("=== SEARCHING FOR ASSETS ===");
    // For now, just mount current directory so we don't crash
    PHYSFS_mount(".", nullptr, 1);
#else
    // Desktop: Mount current working directory
    if (!PHYSFS_mount("./", nullptr, 1)) {
        std::cerr << "Failed to mount current working directory: "
                  << PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()) << std::endl;
        PHYSFS_deinit();
        return false;
    }
#endif

    #ifdef PACKED_ASSET_FILE
        LOG_INFO("found packed asset file: {}", PACKED_ASSET_FILE);
        if (!PHYSFS_mount(PACKED_ASSET_FILE, nullptr, 0)) {
            std::cerr << "Failed to mount archive (" << PACKED_ASSET_FILE << "): "
                      << PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()) << std::endl;
            PHYSFS_deinit();
            return false;
        }
    #endif

    _physfsInitialized = true;
    return true;
}

// ============================================================================
// PATH MANAGEMENT
// ============================================================================

std::string FileHandler::_getWritableDirectory() {
    // If org and app names are set, use SDL_GetPrefPath
    if (!_orgName.empty() && !_appName.empty()) {
        char* prefPath = SDL_GetPrefPath(_orgName.c_str(), _appName.c_str());
        if (prefPath) {
            std::string path(prefPath);
            SDL_free(prefPath);
            return path;
        }
        LOG_ERROR("SDL_GetPrefPath failed: {}", SDL_GetError());
    }
    
#ifdef __ANDROID__
    // Android MUST use internal storage even without org/app names
    char* prefPath = SDL_GetPrefPath("", "");
    if (prefPath) {
        std::string path(prefPath);
        SDL_free(prefPath);
        return path;
    }
    LOG_ERROR("SDL_GetPrefPath failed on Android: {}", SDL_GetError());
    return "/sdcard/";  // Fallback (may not work without permissions)
#else
    // Desktop: Fall back to executable directory
    return _getBaseDirectory();
#endif
}

std::string FileHandler::_getBaseDirectory() {
    const char* basePath = SDL_GetBasePath();
    if (basePath) {
        std::string path(basePath);
        SDL_free((void*)basePath);
        return path;
    }
    
    LOG_ERROR("SDL_GetBasePath failed: {}", SDL_GetError());
    return "./";  // Ultimate fallback
}

bool FileHandler::_createDirectoryRecursive(const std::string& path) {
    try {
        std::filesystem::path fsPath(path);
        return std::filesystem::create_directories(fsPath);
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR("Failed to create directory {}: {}", path, e.what());
        return false;
    }
}

// ============================================================================
// FILE READING (PHYSFS)
// ============================================================================

PhysFSFileData FileHandler::_readFile(const std::string& filename) {
    PhysFSFileData result = {nullptr, 0, {0}};
    
#ifdef __ANDROID__
    // On Android, use SDL_IOFromFile which can read from APK assets
    SDL_IOStream* file = SDL_IOFromFile(filename.c_str(), "rb");
    if (!file) {
        LOG_ERROR("Failed to open file: {} - {}", filename, SDL_GetError());
        return result;
    }
    
    Sint64 fileSize = SDL_GetIOSize(file);
    if (fileSize <= 0) {
        LOG_ERROR("Invalid file size: {}", fileSize);
        SDL_CloseIO(file);
        return result;
    }
    
    void* buffer = malloc(fileSize);
    if (!buffer) {
        LOG_ERROR("Failed to allocate memory for file: {}", filename);
        SDL_CloseIO(file);
        return result;
    }
    
    size_t bytesRead = SDL_ReadIO(file, buffer, fileSize);
    if (bytesRead != (size_t)fileSize) {
        LOG_ERROR("Failed to read file: {} - {}", filename, SDL_GetError());
        free(buffer);
        SDL_CloseIO(file);
        return result;
    }
    
    SDL_CloseIO(file);
    
    result.data = buffer;
    result.fileSize = static_cast<int>(fileSize);
    return result;
#else
    // Desktop: Use PhysFS (auto-initialize if needed)
    _ensurePhysFS();
    
    if (!PHYSFS_isInit()) {
        std::cerr << "PhysFS failed to initialize, cannot read file: " << filename << std::endl;
        return result;
    }
    
    if (!PHYSFS_exists(filename.c_str())) {
        std::cerr << "File does not exist: " << filename << std::endl;
        return result;
    }

    PHYSFS_File* file = PHYSFS_openRead(filename.c_str());
    if (!file) {
        std::cerr << "Failed to open file: " << filename
                  << " - " << PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()) << std::endl;
        return result;
    }

    PHYSFS_sint64 fileSize = PHYSFS_fileLength(file);
    if (fileSize <= 0) {
        std::cerr << "Invalid file size: " << fileSize << std::endl;
        PHYSFS_close(file);
        return result;
    }

    void* buffer = malloc(fileSize);
    if (!buffer) {
        std::cerr << "Failed to allocate memory for file: " << filename << std::endl;
        PHYSFS_close(file);
        return result;
    }

    PHYSFS_sint64 bytesRead = PHYSFS_readBytes(file, buffer, fileSize);
    if (bytesRead != fileSize) {
        std::cerr << "Failed to read file: " << filename
                  << " - " << PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()) << std::endl;
        free(buffer);
        PHYSFS_close(file);
        return result;
    }

    PHYSFS_close(file);

    result.data = buffer;
    result.fileSize = static_cast<int>(fileSize);
    return result;
#endif
}

// ============================================================================
// FILE READING (TEXT AND BINARY)
// ============================================================================

std::string FileHandler::_readTextFile(const std::string& filepath) {
    // Try reading as PhysFS asset first (for bundled game files)
    PhysFSFileData physfsData = _readFile(filepath);
    if (physfsData.data) {
        std::string result((char*)physfsData.data, physfsData.fileSize);
        free(physfsData.data);
        return result;
    }
    
    // Fall back to normal file I/O (for writable files like saves/configs)
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to open text file: {}", filepath);
        return "";
    }
    
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::string content(fileSize, '\0');
    file.read(&content[0], fileSize);
    
    if (file.fail() && !file.eof()) {
        LOG_ERROR("Failed to read text file: {}", filepath);
        return "";
    }
    
    return content;
}

std::vector<uint8_t> FileHandler::_readBinaryFile(const std::string& filepath) {
    // Try reading as PhysFS asset first
    PhysFSFileData physfsData = _readFile(filepath);
    if (physfsData.data) {
        std::vector<uint8_t> result((uint8_t*)physfsData.data, 
                                    (uint8_t*)physfsData.data + physfsData.fileSize);
        free(physfsData.data);
        return result;
    }
    
    // Fall back to normal file I/O
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to open binary file: {}", filepath);
        return {};
    }
    
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> content(fileSize);
    file.read(reinterpret_cast<char*>(content.data()), fileSize);
    
    if (file.fail() && !file.eof()) {
        LOG_ERROR("Failed to read binary file: {}", filepath);
        return {};
    }
    
    return content;
}

// ============================================================================
// FILE WRITING
// ============================================================================

bool FileHandler::_writeFile(const std::string& filepath, const void* data, size_t size) {
    // Create parent directories if they don't exist
    std::filesystem::path fsPath(filepath);
    std::filesystem::path parentPath = fsPath.parent_path();
    
    if (!parentPath.empty() && !_directoryExists(parentPath.string())) {
        if (!_createDirectoryRecursive(parentPath.string())) {
            LOG_ERROR("Failed to create directories for: {}", filepath);
            return false;
        }
    }
    
    // Write the file
    std::ofstream file(filepath, std::ios::out | std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to open file for writing: {}", filepath);
        return false;
    }
    
    file.write(static_cast<const char*>(data), size);
    
    if (file.fail()) {
        LOG_ERROR("Failed to write to file: {}", filepath);
        return false;
    }
    
    return true;
}

// ============================================================================
// FILE QUERIES
// ============================================================================

bool FileHandler::_fileExists(const std::string& filepath) {
    // Check PhysFS first (for bundled assets) - auto-initialize if needed
    _ensurePhysFS();
    
    if (PHYSFS_isInit() && PHYSFS_exists(filepath.c_str())) {
        return true;
    }
    
    // Check filesystem (for writable files)
    return std::filesystem::exists(filepath);
}

bool FileHandler::_directoryExists(const std::string& dirpath) {
    try {
        return std::filesystem::exists(dirpath) && std::filesystem::is_directory(dirpath);
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

size_t FileHandler::_getFileSize(const std::string& filepath) {
    try {
        if (std::filesystem::exists(filepath)) {
            return std::filesystem::file_size(filepath);
        }
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR("Failed to get file size for {}: {}", filepath, e.what());
    }
    return 0;
}

// ============================================================================
// FILE DELETION
// ============================================================================

bool FileHandler::_deleteFile(const std::string& filepath) {
    try {
        if (std::filesystem::exists(filepath)) {
            return std::filesystem::remove(filepath);
        }
        return true;  // File doesn't exist, consider it a success
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR("Failed to delete file {}: {}", filepath, e.what());
        return false;
    }
}

bool FileHandler::_deleteDirectory(const std::string& dirpath) {
    try {
        if (std::filesystem::exists(dirpath)) {
            return std::filesystem::remove_all(dirpath) > 0;
        }
        return true;  // Directory doesn't exist, consider it a success
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR("Failed to delete directory {}: {}", dirpath, e.what());
        return false;
    }
}

bool FileHandler::_clearLogs() {
    std::string systemDir = GetSystemDirectory();
    
    if (!_directoryExists(systemDir)) {
        return true;  // No logs to clear
    }
    
    try {
        bool allDeleted = true;
        for (const auto& entry : std::filesystem::directory_iterator(systemDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".log") {
                if (!_deleteFile(entry.path().string())) {
                    allDeleted = false;
                }
            }
        }
        return allDeleted;
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR("Failed to clear logs: {}", e.what());
        return false;
    }
}
