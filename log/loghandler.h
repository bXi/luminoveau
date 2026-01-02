#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <cstdlib>      // for std::exit
#include <stdexcept>   // for std::runtime_error
#include <fmt/core.h>
#include <fmt/format.h>

// Cross-platform function name macro
#ifdef _MSC_VER
    #define CURRENT_METHOD() __FUNCTION__
#elif defined(__GNUC__)
    #define CURRENT_METHOD() __PRETTY_FUNCTION__
#else
    #define CURRENT_METHOD() __func__
#endif

// Logging macros with automatic location capture
// LOG_DEBUG, LOG_INFO, LOG_WARNING - Log messages only
// LOG_ERROR - Logs the error and throws std::runtime_error (catchable)
// LOG_CRITICAL - Logs the error, flushes all sinks, and exits program with EXIT_FAILURE
#define LOG_DEBUG(fmt, ...) Log::DebugImpl(__FILE__, __LINE__, CURRENT_METHOD(), fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) Log::InfoImpl(__FILE__, __LINE__, CURRENT_METHOD(), fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) Log::WarningImpl(__FILE__, __LINE__, CURRENT_METHOD(), fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Log::ErrorImpl(__FILE__, __LINE__, CURRENT_METHOD(), fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(fmt, ...) Log::CriticalImpl(__FILE__, __LINE__, CURRENT_METHOD(), fmt, ##__VA_ARGS__)

// Log levels
enum class LogLevel {
    Debug,      // Verbose debug information
    Info,       // General information
    Warning,    // Warning but not critical
    Error,      // Error - logs and throws std::runtime_error
    Critical    // Critical error - logs, flushes, and exits program
};

// Single log entry
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;  // Actual timestamp
    LogLevel level;
    std::string message;
    std::string file;           // "shaderhandler.cpp"
    int line;                   // 123
    std::string function;       // "Shaders::_getShader"
    bool isUserFacing;          // true for user-visible console logs
    
    // Timestamp formatting helpers (like PHP Carbon)
    std::string FormatTime() const;           // "12:34:56.789"
    std::string FormatDateTime() const;       // "2025-01-01 12:34:56.789"
    std::string FormatDateTimeShort() const;  // "01-01 12:34:56"
    std::string FormatRelative() const;       // "2 minutes ago"
    std::string FormatCustom(const char* format) const;  // Custom strftime format
    
    // Default formats for ToString
    std::string ToString() const;
    std::string ToColoredString() const;  // For terminal/console with ANSI colors
};

// Base class for log output destinations
class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void Write(const LogEntry& entry) = 0;
    virtual void Flush() {}
};

// Built-in sinks
class SDLConsoleSink : public LogSink {
public:
    explicit SDLConsoleSink(LogLevel minLevel = LogLevel::Info);
    void Write(const LogEntry& entry) override;
    void SetMinLevel(LogLevel level) { minLevel = level; }
    
private:
    LogLevel minLevel;
};

class FileSink : public LogSink {
public:
    explicit FileSink(const std::string& filename, LogLevel minLevel = LogLevel::Debug);
    ~FileSink() override;
    void Write(const LogEntry& entry) override;
    void Flush() override;
    
private:
    std::string filename;
    LogLevel minLevel;
    FILE* file;
};

class MemoryBufferSink : public LogSink {
public:
    explicit MemoryBufferSink(size_t maxEntries = 1000);
    void Write(const LogEntry& entry) override;
    std::vector<LogEntry> GetEntries(LogLevel minLevel = LogLevel::Debug) const;
    std::vector<LogEntry> GetUserEntries() const;
    void Clear();
    
private:
    std::vector<LogEntry> entries;
    size_t maxEntries;
    mutable std::mutex mutex;
};

// Main logging class
class Log {
public:

    // Internal implementation - called by macros
    template<typename... Args>
    static void DebugImpl(const char* file, int line, const char* func, fmt::format_string<Args...> fmt, Args&&... args) {
        get().LogImpl(LogLevel::Debug, false, file, line, func, fmt::format(fmt, std::forward<Args>(args)...));
    }
    
    template<typename... Args>
    static void InfoImpl(const char* file, int line, const char* func, fmt::format_string<Args...> fmt, Args&&... args) {
        get().LogImpl(LogLevel::Info, false, file, line, func, fmt::format(fmt, std::forward<Args>(args)...));
    }
    
    template<typename... Args>
    static void WarningImpl(const char* file, int line, const char* func, fmt::format_string<Args...> fmt, Args&&... args) {
        get().LogImpl(LogLevel::Warning, false, file, line, func, fmt::format(fmt, std::forward<Args>(args)...));
    }
    
    template<typename... Args>
    [[noreturn]] static void ErrorImpl(const char* file, int line, const char* func, fmt::format_string<Args...> fmt, Args&&... args) {
        std::string message = fmt::format(fmt, std::forward<Args>(args)...);
        get().LogImpl(LogLevel::Error, false, file, line, func, message);
        throw std::runtime_error(message);
    }
    
    template<typename... Args>
    [[noreturn]] static void CriticalImpl(const char* file, int line, const char* func, fmt::format_string<Args...> fmt, Args&&... args) {
        std::string message = fmt::format(fmt, std::forward<Args>(args)...);
        get().LogImpl(LogLevel::Critical, false, file, line, func, message);
        get()._flushAll();  // Flush all logs before exit
        std::exit(EXIT_FAILURE);
    }
    
    // Get singleton instance (auto-initializes on first use)
    static Log& get() {
        static Log instance;
        return instance;
    }
    
private:
    // Constructors/destructors
    Log();
    ~Log();
    
    // Delete copy/move
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;
    
    // Core logging implementation
    void LogImpl(LogLevel level, bool isUserFacing, const char* file, int line, const char* func,
                const std::string& message) {
        LogEntry entry;
        entry.timestamp = std::chrono::system_clock::now();
        entry.level = level;
        entry.message = message;
        entry.file = ExtractFilename(file);
        entry.line = line;
        entry.function = CleanFunctionName(func);
        entry.isUserFacing = isUserFacing;
        
        WriteToSinks(entry);
    }
    
    // Sink management
    static void AddSink(std::unique_ptr<LogSink> sink) { get()._addSink(std::move(sink)); }
    static void ClearSinks() { get()._clearSinks(); }
    static void FlushAll() { get()._flushAll(); }
    
    // Configuration
    static void SetMinLevel(LogLevel level) { get()._setMinLevel(level); }
    
    // Retrieval (from memory buffer sink if present)
    static std::vector<LogEntry> GetLines(LogLevel minLevel = LogLevel::Debug) { return get()._getLines(minLevel); }
    static std::vector<LogEntry> GetUserLines() { return get()._getUserLines(); }
    
    // Dump to file
    static bool DumpToFile(const std::string& filename, LogLevel minLevel = LogLevel::Debug) { 
        return get()._dumpToFile(filename, minLevel);
    }
    
    std::vector<std::unique_ptr<LogSink>> sinks;
    std::mutex sinkMutex;
    MemoryBufferSink* memoryBufferSink;  // Quick access to memory buffer
    
    // Instance methods
    void _addSink(std::unique_ptr<LogSink> sink);
    void _clearSinks();
    void _flushAll();
    void _setMinLevel(LogLevel level);
    std::vector<LogEntry> _getLines(LogLevel minLevel);
    std::vector<LogEntry> _getUserLines();
    bool _dumpToFile(const std::string& filename, LogLevel minLevel);
    
    // Helper functions
    static std::string ExtractFilename(const char* path);
    static std::string CleanFunctionName(const char* funcName);
    void WriteToSinks(const LogEntry& entry);
};

