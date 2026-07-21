#pragma once

// Engine logging. Use the LOG_* macros — they capture file/line/function automatically.
//
// The record type and the output destinations live alongside this header:
//   core/log/logentry.h              LogLevel + LogEntry
//   core/log/logsink.h               LogSink base (implement to add your own destination)
//   core/log/sinks/*.h               built-in sinks (console, file, memory buffer)

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cstdlib>   // for std::exit
#include <stdexcept> // for std::runtime_error
#include <fmt/core.h>
#include <fmt/format.h>

#include "core/log/logentry.h"
#include "core/log/logsink.h"
#include "core/log/sinks/filesink.h"
#include "core/log/sinks/memorybuffersink.h"
#include "core/log/sinks/sdlconsolesink.h"

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

/**
 * @brief Engine logger. Routes formatted entries to every registered sink.
 */
class Log {
public:
    /// @cond INTERNAL
    // Internal implementation - called by macros
    template <typename... Args>
    static void DebugImpl(const char *file, int line, const char *func, fmt::format_string<Args...> fmt, Args &&...args) {
        get()._logImpl(LogLevel::Debug, false, file, line, func, fmt::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void InfoImpl(const char *file, int line, const char *func, fmt::format_string<Args...> fmt, Args &&...args) {
        get()._logImpl(LogLevel::Info, false, file, line, func, fmt::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void WarningImpl(const char *file, int line, const char *func, fmt::format_string<Args...> fmt, Args &&...args) {
        get()._logImpl(LogLevel::Warning, false, file, line, func, fmt::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    [[noreturn]] static void ErrorImpl(const char *file, int line, const char *func, fmt::format_string<Args...> fmt, Args &&...args) {
        std::string message = fmt::format(fmt, std::forward<Args>(args)...);
        get()._logImpl(LogLevel::Error, false, file, line, func, message);
        throw std::runtime_error(message);
    }

    template <typename... Args>
    [[noreturn]] static void CriticalImpl(const char *file, int line, const char *func, fmt::format_string<Args...> fmt, Args &&...args) {
        std::string message = fmt::format(fmt, std::forward<Args>(args)...);
        get()._logImpl(LogLevel::Critical, false, file, line, func, message);
        get()._flushAll(); // Flush all logs before exit
        std::exit(EXIT_FAILURE);
    }
    /// @endcond

private:
    // Core logging implementation
    void _logImpl(LogLevel level, bool isUserFacing, const char *file, int line, const char *func,
        const std::string &message) {
        LogEntry entry;
        entry.timestamp    = std::chrono::system_clock::now();
        entry.level        = level;
        entry.message      = message;
        entry.file         = _extractFilename(file);
        entry.line         = line;
        entry.function     = _cleanFunctionName(func);
        entry.isUserFacing = isUserFacing;

        _writeToSinks(entry);
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
    static bool DumpToFile(const std::string &filename, LogLevel minLevel = LogLevel::Debug) {
        return get()._dumpToFile(filename, minLevel);
    }

    std::vector<std::unique_ptr<LogSink>> _sinks;
    std::mutex                            _sinkMutex;
    MemoryBufferSink                     *_memoryBufferSink; // Quick access to memory buffer

    // Instance methods
    void                  _addSink(std::unique_ptr<LogSink> sink);
    void                  _clearSinks();
    void                  _flushAll();
    void                  _setMinLevel(LogLevel level);
    std::vector<LogEntry> _getLines(LogLevel minLevel);
    std::vector<LogEntry> _getUserLines();
    bool                  _dumpToFile(const std::string &filename, LogLevel minLevel);

    // Helper functions
    static std::string _extractFilename(const char *path);
    static std::string _cleanFunctionName(const char *funcName);
    void               _writeToSinks(const LogEntry &entry);

public:
    /// @cond INTERNAL
    // Delete copy/move
    Log(const Log &)            = delete;
    Log &operator=(const Log &) = delete;

    // Get singleton instance (auto-initializes on first use)
    static Log &get() {
        static Log instance;
        return instance;
    }
    /// @endcond

private:
    Log();
    ~Log();
};
