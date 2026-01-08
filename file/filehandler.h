#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include <SDL3/sdl.h>

struct PhysFSFileData {
    void* data;
    int fileSize;
    std::vector<uint8_t> fileDataVector;
};

/**
 * @brief Handles cross-platform file I/O operations.
 * 
 * FileHandler provides a unified interface for reading and writing files across
 * all platforms (Windows, Linux, macOS, Android). It uses PhysFS for asset loading
 * and SDL3's path functions for writable storage.
 * 
 * Configuration:
 * - Call SetOrganizationName() and SetApplicationName() to define writable storage location
 * - If not set, defaults to executable directory (except Android, which uses internal storage)
 * 
 * Usage:
 *   FileHandler::SetOrganizationName("Luminoveau");
 *   FileHandler::SetApplicationName("DeltaLight2");
 *   
 *   std::string data = FileHandler::ReadTextFile("config.json");
 *   FileHandler::WriteTextFile(FileHandler::GetSystemDirectory() + "log.txt", logData);
 */
class FileHandler {
public:
    // ========================================================================
    // CONFIGURATION
    // ========================================================================
    
    /**
     * @brief Sets the organization/company name for writable storage paths.
     * @param name Organization name (e.g., "Luminoveau")
     */
    static void SetOrganizationName(const std::string& name) { get()._orgName = name; }
    
    /**
     * @brief Sets the application/game name for writable storage paths.
     * @param name Application name (e.g., "DeltaLight2")
     */
    static void SetApplicationName(const std::string& name) { get()._appName = name; }
    
    // ========================================================================
    // PATH RETRIEVAL
    // ========================================================================
    
    /**
     * @brief Gets the writable directory for user data and saves.
     * 
     * If org/app names are set, uses platform-specific user data directories.
     * Otherwise, defaults to executable directory (Android always uses internal storage).
     * 
     * @return Path to writable directory with trailing slash
     */
    static std::string GetWritableDirectory() { return get()._getWritableDirectory(); }
    
    /**
     * @brief Gets the system directory for engine files (shader cache, logs, etc.).
     * @return Path to LumiSystem directory with trailing slash
     */
    static std::string GetSystemDirectory() { return GetWritableDirectory() + "LumiSystem/"; }
    
    /**
     * @brief Gets the base executable directory (read-only on mobile platforms).
     * @return Path to executable directory with trailing slash
     */
    static std::string GetBaseDirectory() { return get()._getBaseDirectory(); }
    
    // ========================================================================
    // FILE READING
    // ========================================================================
    
    /**
     * @brief Initializes the PhysFS file system for asset loading.
     * @return true if initialization succeeded, false otherwise
     */
    static bool InitPhysFS() { return get()._initPhysFS(); }
    
    /**
     * @brief Reads a file from PhysFS (for assets bundled with the game).
     * @param filename Relative path to the file
     * @return PhysFSFileData containing the file data (caller must free data pointer)
     */
    static PhysFSFileData ReadFile(const std::string &filename) { return get()._readFile(filename); }
    
    /**
     * @brief Legacy API - same as ReadFile().
     * @param filename Relative path to the file
     * @return PhysFSFileData containing the file data
     */
    static PhysFSFileData GetFileFromPhysFS(const std::string &filename) { return ReadFile(filename); }
    
    /**
     * @brief Reads an entire text file into a string.
     * 
     * Works with both PhysFS assets and absolute file paths.
     * Returns empty string on failure.
     * 
     * @param filepath Path to the text file
     * @return File contents as string, or empty string on error
     */
    static std::string ReadTextFile(const std::string& filepath) { return get()._readTextFile(filepath); }
    
    /**
     * @brief Reads an entire file as binary data.
     * 
     * Works with both PhysFS assets and absolute file paths.
     * Returns empty vector on failure.
     * 
     * @param filepath Path to the file
     * @return File contents as byte vector, or empty vector on error
     */
    static std::vector<uint8_t> ReadBinaryFile(const std::string& filepath) { return get()._readBinaryFile(filepath); }
    
    // ========================================================================
    // FILE WRITING
    // ========================================================================
    
    /**
     * @brief Writes binary data to a file (creates directories if needed).
     * @param filepath Path to the file to write
     * @param data Pointer to data to write
     * @param size Size of data in bytes
     * @return true if write succeeded, false otherwise
     */
    static bool WriteFile(const std::string& filepath, const void* data, size_t size) { 
        return get()._writeFile(filepath, data, size); 
    }
    
    /**
     * @brief Writes text to a file (creates directories if needed).
     * @param filepath Path to the file to write
     * @param text Text content to write
     * @return true if write succeeded, false otherwise
     */
    static bool WriteTextFile(const std::string& filepath, const std::string& text) {
        return WriteFile(filepath, text.c_str(), text.size());
    }
    
    // ========================================================================
    // FILE/DIRECTORY QUERIES
    // ========================================================================
    
    /**
     * @brief Checks if a file exists.
     * @param filepath Path to check
     * @return true if file exists and is readable
     */
    static bool FileExists(const std::string& filepath) { return get()._fileExists(filepath); }
    
    /**
     * @brief Checks if a directory exists.
     * @param dirpath Directory path to check
     * @return true if directory exists
     */
    static bool DirectoryExists(const std::string& dirpath) { return get()._directoryExists(dirpath); }
    
    /**
     * @brief Gets the size of a file in bytes.
     * @param filepath Path to the file
     * @return File size in bytes, or 0 on error
     */
    static size_t GetFileSize(const std::string& filepath) { return get()._getFileSize(filepath); }
    
    // ========================================================================
    // FILE/DIRECTORY DELETION
    // ========================================================================
    
    /**
     * @brief Deletes a single file.
     * @param filepath Path to the file to delete
     * @return true if deletion succeeded, false otherwise
     */
    static bool DeleteFile(const std::string& filepath) { return get()._deleteFile(filepath); }
    
    /**
     * @brief Deletes a directory and all its contents recursively.
     * @param dirpath Path to the directory to delete
     * @return true if deletion succeeded, false otherwise
     */
    static bool DeleteDirectory(const std::string& dirpath) { return get()._deleteDirectory(dirpath); }
    
    /**
     * @brief Clears the entire LumiSystem directory (deletes all engine files).
     * @return true if cleared successfully, false otherwise
     */
    static bool ClearSystemDirectory() { return DeleteDirectory(GetSystemDirectory()); }
    
    // ========================================================================
    // CONVENIENCE METHODS
    // ========================================================================
    
    /**
     * @brief Deletes the shader cache file.
     * @return true if deletion succeeded or file didn't exist, false on error
     */
    static bool DeleteShaderCache() { 
        return DeleteFile(GetSystemDirectory() + "shader.cache"); 
    }
    
    /**
     * @brief Clears all log files from the LumiSystem directory.
     * 
     * Deletes all files ending with .log extension.
     * 
     * @return true if all logs were deleted successfully
     */
    static bool ClearLogs() { return get()._clearLogs(); }

private:
    // Configuration
    std::string _orgName;
    std::string _appName;
    bool _physfsInitialized = false;  // Guard against double initialization
    
    // Initialization
    bool _initPhysFS();
    void _ensurePhysFS();  // Lazy initialization helper
    
    // Path management
    std::string _getWritableDirectory();
    std::string _getBaseDirectory();
    bool _createDirectoryRecursive(const std::string& path);
    
    // File reading
    PhysFSFileData _readFile(const std::string& filename);
    std::string _readTextFile(const std::string& filepath);
    std::vector<uint8_t> _readBinaryFile(const std::string& filepath);
    
    // File writing
    bool _writeFile(const std::string& filepath, const void* data, size_t size);
    
    // File queries
    bool _fileExists(const std::string& filepath);
    bool _directoryExists(const std::string& dirpath);
    size_t _getFileSize(const std::string& filepath);
    
    // File deletion
    bool _deleteFile(const std::string& filepath);
    bool _deleteDirectory(const std::string& dirpath);
    bool _clearLogs();

public:
    FileHandler(const FileHandler &) = delete;

    static FileHandler &get() {
        static FileHandler instance;
        return instance;
    }

private:
    FileHandler() = default;
    ~FileHandler() = default;
};
