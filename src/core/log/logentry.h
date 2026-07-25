#pragma once

// A single log record, plus the severity scale it is graded on. Split out of log.h so
// sinks can consume entries without pulling in the logger itself.

#include <chrono>
#include <string>

/// @brief Severity levels, ordered from most to least verbose.
enum class LogLevel {
    Debug,   // Verbose debug information
    Info,    // General information
    Warning, // Warning but not critical
    Error,   // Error - logs and throws std::runtime_error
    Critical // Critical error - logs, flushes, and exits program
};

/// @cond INTERNAL
/// @brief One log record, as handed to every registered sink.
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;    ///< When the entry was recorded.
    LogLevel                              level;        ///< Severity.
    std::string                           message;      ///< Formatted message text.
    std::string                           file;         ///< Source file, e.g. "shaders.cpp".
    int                                   line;         ///< Source line.
    std::string                           function;     ///< Function name, e.g. "Shaders::_getShader".
    bool                                  isUserFacing; ///< True for user-visible console logs.

    // Timestamp formatting helpers (like PHP Carbon)
    std::string FormatTime() const;                     // "12:34:56.789"
    std::string FormatDateTime() const;                 // "2025-01-01 12:34:56.789"
    std::string FormatDateTimeShort() const;            // "01-01 12:34:56"
    std::string FormatRelative() const;                 // "2 minutes ago"
    std::string FormatCustom(const char *format) const; // Custom strftime format

    // Default formats for ToString
    std::string ToString() const;
    std::string ToColoredString() const; // For terminal/console with ANSI colors
};
/// @endcond
