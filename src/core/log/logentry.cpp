#include "core/log/logentry.h"

#include <chrono>
#include <iomanip>
#include <sstream>

// LogEntry timestamp formatting methods
std::string LogEntry::FormatTime() const {
    auto time = std::chrono::system_clock::to_time_t(timestamp);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                  timestamp.time_since_epoch())
        % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string LogEntry::FormatDateTime() const {
    auto time = std::chrono::system_clock::to_time_t(timestamp);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                  timestamp.time_since_epoch())
        % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string LogEntry::FormatDateTimeShort() const {
    auto time = std::chrono::system_clock::to_time_t(timestamp);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                  timestamp.time_since_epoch())
        % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string LogEntry::FormatRelative() const {
    auto now     = std::chrono::system_clock::now();
    auto diff    = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp);
    auto seconds = diff.count();

    if (seconds < 0) {
        return "in the future";
    } else if (seconds < 1) {
        return "just now";
    } else if (seconds < 60) {
        return std::to_string(seconds) + " second" + (seconds == 1 ? "" : "s") + " ago";
    } else if (seconds < 3600) {
        auto minutes = seconds / 60;
        return std::to_string(minutes) + " minute" + (minutes == 1 ? "" : "s") + " ago";
    } else if (seconds < 86400) {
        auto hours = seconds / 3600;
        return std::to_string(hours) + " hour" + (hours == 1 ? "" : "s") + " ago";
    } else {
        auto days = seconds / 86400;
        return std::to_string(days) + " day" + (days == 1 ? "" : "s") + " ago";
    }
}

std::string LogEntry::FormatCustom(const char *format) const {
    auto               time = std::chrono::system_clock::to_time_t(timestamp);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), format);
    return oss.str();
}

// LogEntry ToString methods
std::string LogEntry::ToString() const {
    std::ostringstream oss;

    // [Lumi] prefix
    oss << "[Lumi] ";

    // Level
    switch (level) {
    case LogLevel::Debug:
        oss << "[DBUG] ";
        break;
    case LogLevel::Info:
        oss << "[INFO] ";
        break;
    case LogLevel::Warning:
        oss << "[WARN] ";
        break;
    case LogLevel::Error:
        oss << "[ERRO] ";
        break;
    case LogLevel::Critical:
        oss << "[CRIT] ";
        break;
    }

    // Timestamp
    oss << "[" << FormatTime() << "] ";

    // Function and message (ClassName::method: message)
    oss << function << ": " << message;

    return oss.str();
}

std::string LogEntry::ToColoredString() const {
    // ANSI color codes
    const char *reset     = "\033[0m";
    const char *darkBlue  = "\033[34m"; // Dark blue for brackets
    const char *lightBlue = "\033[94m"; // Light blue for "Lumi"
    const char *gray      = "\033[90m"; // Gray for timestamp
    const char *levelColor;

    switch (level) {
    case LogLevel::Debug:
        levelColor = "\033[36m";
        break; // Cyan
    case LogLevel::Info:
        levelColor = "\033[32m";
        break; // Green
    case LogLevel::Warning:
        levelColor = "\033[33m";
        break; // Yellow
    case LogLevel::Error:
        levelColor = "\033[31m";
        break; // Red
    case LogLevel::Critical:
        levelColor = "\033[1;31m";
        break; // Bold Red
    }

    std::ostringstream oss;

    // [Lumi] with colors
    oss << darkBlue << "[" << lightBlue << "Lumi" << darkBlue << "]" << reset << " ";

    // Level with color
    oss << levelColor;
    switch (level) {
    case LogLevel::Debug:
        oss << "[DBUG]";
        break;
    case LogLevel::Info:
        oss << "[INFO]";
        break;
    case LogLevel::Warning:
        oss << "[WARN]";
        break;
    case LogLevel::Error:
        oss << "[ERRO]";
        break;
    case LogLevel::Critical:
        oss << "[CRIT]";
        break;
    }
    oss << reset << " ";

    // Timestamp in gray
    oss << gray << "[" << FormatTime() << "]" << reset << " ";

    // Function and message
    oss << function << ": " << message;

    return oss.str();
}
