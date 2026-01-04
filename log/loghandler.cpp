#include "loghandler.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

// LogEntry timestamp formatting methods
std::string LogEntry::FormatTime() const {
    auto time = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string LogEntry::FormatDateTime() const {
    auto time = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string LogEntry::FormatDateTimeShort() const {
    auto time = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string LogEntry::FormatRelative() const {
    auto now = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp);
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

std::string LogEntry::FormatCustom(const char* format) const {
    auto time = std::chrono::system_clock::to_time_t(timestamp);
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
        case LogLevel::Debug:    oss << "[DEBUG] "; break;
        case LogLevel::Info:     oss << "[INFO] "; break;
        case LogLevel::Warning:  oss << "[WARNING] "; break;
        case LogLevel::Error:    oss << "[ERROR] "; break;
        case LogLevel::Critical: oss << "[CRITICAL] "; break;
    }

    // Timestamp
    oss << "[" << FormatTime() << "] ";

    // Function and message (ClassName::method: message)
    oss << function << ": " << message;

    return oss.str();
}

std::string LogEntry::ToColoredString() const {
    // ANSI color codes
    const char* reset = "\033[0m";
    const char* darkBlue = "\033[34m";      // Dark blue for brackets
    const char* lightBlue = "\033[94m";     // Light blue for "Lumi"
    const char* gray = "\033[90m";          // Gray for timestamp
    const char* levelColor;

    switch (level) {
        case LogLevel::Debug:    levelColor = "\033[36m"; break; // Cyan
        case LogLevel::Info:     levelColor = "\033[32m"; break; // Green
        case LogLevel::Warning:  levelColor = "\033[33m"; break; // Yellow
        case LogLevel::Error:    levelColor = "\033[31m"; break; // Red
        case LogLevel::Critical: levelColor = "\033[1;31m"; break; // Bold Red
    }

    std::ostringstream oss;

    // [Lumi] with colors
    oss << darkBlue << "[" << lightBlue << "Lumi" << darkBlue << "]" << reset << " ";

    // Level with color
    oss << levelColor;
    switch (level) {
        case LogLevel::Debug:    oss << "[DEBUG]"; break;
        case LogLevel::Info:     oss << "[INFO]"; break;
        case LogLevel::Warning:  oss << "[WARNING]"; break;
        case LogLevel::Error:    oss << "[ERROR]"; break;
        case LogLevel::Critical: oss << "[CRITICAL]"; break;
    }
    oss << reset << " ";

    // Timestamp in gray
    oss << gray << "[" << FormatTime() << "]" << reset << " ";

    // Function and message
    oss << function << ": " << message;

    return oss.str();
}

// SDLConsoleSink implementation
SDLConsoleSink::SDLConsoleSink(LogLevel minLevel) : minLevel(minLevel) {}

void SDLConsoleSink::Write(const LogEntry& entry) {
    if (entry.level < minLevel) {
        return;
    }

#ifdef __ANDROID__
    //Removing color because logcat doesn't show this properly.
    SDL_Log("%s", entry.ToString().c_str());
#else
    SDL_Log("%s\n", entry.ToColoredString().c_str());
#endif
}

// FileSink implementation
FileSink::FileSink(const std::string& filename, LogLevel minLevel)
    : filename(filename), minLevel(minLevel), file(nullptr) {
    file = std::fopen(filename.c_str(), "a");
    if (!file) {
        SDL_Log("Failed to open log file: {}", filename.c_str());
    }
}

FileSink::~FileSink() {
    if (file) {
        std::fclose(file);
    }
}

void FileSink::Write(const LogEntry& entry) {
    if (!file || entry.level < minLevel) {
        return;
    }

    std::fprintf(file, "%s\n", entry.ToString().c_str());
}

void FileSink::Flush() {
    if (file) {
        std::fflush(file);
    }
}

// MemoryBufferSink implementation
MemoryBufferSink::MemoryBufferSink(size_t maxEntries) : maxEntries(maxEntries) {
    entries.reserve(maxEntries);
}

void MemoryBufferSink::Write(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex);

    if (entries.size() >= maxEntries) {
        // Remove oldest entry (FIFO)
        entries.erase(entries.begin());
    }

    entries.push_back(entry);
}

std::vector<LogEntry> MemoryBufferSink::GetEntries(LogLevel minLevel) const {
    std::lock_guard<std::mutex> lock(mutex);

    std::vector<LogEntry> result;
    result.reserve(entries.size());

    for (const auto& entry : entries) {
        if (entry.level >= minLevel) {
            result.push_back(entry);
        }
    }

    return result;
}

std::vector<LogEntry> MemoryBufferSink::GetUserEntries() const {
    std::lock_guard<std::mutex> lock(mutex);

    std::vector<LogEntry> result;
    result.reserve(entries.size());

    for (const auto& entry : entries) {
        if (entry.isUserFacing) {
            result.push_back(entry);
        }
    }

    return result;
}

void MemoryBufferSink::Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    entries.clear();
}

// Log constructor - auto-initializes on first use
Log::Log() : memoryBufferSink(nullptr) {
    #ifdef _WIN32
    // Enable ANSI colors
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
    #endif

    // Add default sinks
    sinks.push_back(std::make_unique<SDLConsoleSink>(LogLevel::Info));

    auto memSink = std::make_unique<MemoryBufferSink>(1000);
    memoryBufferSink = memSink.get();
    sinks.push_back(std::move(memSink));

    LogEntry entry;
    entry.level = LogLevel::Info;
    entry.timestamp = std::chrono::system_clock::now();
    entry.function = "Log::Log";
    entry.message = "Logging system initialized with " + std::to_string(sinks.size()) + " sinks";

    SDL_Log("%s", entry.ToColoredString().c_str());
}

// Log destructor - auto-cleanup on program exit
Log::~Log() {
    _flushAll();
    sinks.clear();
    memoryBufferSink = nullptr;

    LogEntry entry;
    entry.level = LogLevel::Info;
    entry.timestamp = std::chrono::system_clock::now();
    entry.function = "Log::~Log";
    entry.message = "Logging system shut down";

    SDL_Log("%s", entry.ToColoredString().c_str());
}

void Log::_addSink(std::unique_ptr<LogSink> sink) {
    std::lock_guard<std::mutex> lock(sinkMutex);
    sinks.push_back(std::move(sink));
}

void Log::_clearSinks() {
    std::lock_guard<std::mutex> lock(sinkMutex);
    sinks.clear();
    memoryBufferSink = nullptr;
}

void Log::_flushAll() {
    std::lock_guard<std::mutex> lock(sinkMutex);
    for (auto& sink : sinks) {
        sink->Flush();
    }
}

void Log::_setMinLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(sinkMutex);

    // Update SDL console sink min level
    for (auto& sink : sinks) {
        if (auto* sdlSink = dynamic_cast<SDLConsoleSink*>(sink.get())) {
            sdlSink->SetMinLevel(level);
        }
    }
}

std::vector<LogEntry> Log::_getLines(LogLevel minLevel) {
    if (memoryBufferSink) {
        return memoryBufferSink->GetEntries(minLevel);
    }
    return {};
}

std::vector<LogEntry> Log::_getUserLines() {
    if (memoryBufferSink) {
        return memoryBufferSink->GetUserEntries();
    }
    return {};
}

bool Log::_dumpToFile(const std::string& filename, LogLevel minLevel) {
    auto entries = _getLines(minLevel);
    if (entries.empty()) {
        return false;
    }

    FILE* file = std::fopen(filename.c_str(), "w");
    if (!file) {
        return false;
    }

    for (const auto& entry : entries) {
        std::fprintf(file, "%s\n", entry.ToString().c_str());
    }

    std::fclose(file);
    return true;
}

std::string Log::ExtractFilename(const char* path) {
    std::string pathStr(path);

    // Find last slash or backslash
    size_t lastSlash = pathStr.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        return pathStr.substr(lastSlash + 1);
    }

    return pathStr;
}

std::string Log::CleanFunctionName(const char* funcName) {
    std::string func(funcName);

    // GCC/Clang format: "returnType ClassName::methodName(params)"
    // MSVC format: "ClassName::methodName"

    // Remove template parameters: "Foo<T>::bar" -> "Foo::bar"
    size_t templateStart = func.find('<');
    while (templateStart != std::string::npos) {
        size_t templateEnd = func.find('>', templateStart);
        if (templateEnd != std::string::npos) {
            func.erase(templateStart, templateEnd - templateStart + 1);
            templateStart = func.find('<');
        } else {
            break;
        }
    }

    // Find opening parenthesis to locate the end of function name
    size_t parenPos = func.find('(');
    if (parenPos == std::string::npos) {
        // No parenthesis, return as-is
        return func;
    }

    // Find the last :: before the parenthesis
    size_t lastScope = func.rfind("::", parenPos);

    if (lastScope != std::string::npos) {
        // Found scope operator - extract "ClassName::methodName"
        // Find the start of the class name (space or start of string)
        size_t classStart = func.rfind(' ', lastScope);
        if (classStart == std::string::npos) {
            classStart = 0;
        } else {
            classStart++; // Skip the space
        }

        // Extract from class name to opening parenthesis
        return func.substr(classStart, parenPos - classStart);
    } else {
        // No scope operator - it's a free function
        // Find the function name (after last space before parenthesis)
        size_t funcStart = func.rfind(' ', parenPos);
        if (funcStart == std::string::npos) {
            funcStart = 0;
        } else {
            funcStart++;
        }
        return func.substr(funcStart, parenPos - funcStart);
    }
}

void Log::WriteToSinks(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(sinkMutex);

    for (auto& sink : sinks) {
        sink->Write(entry);
    }
}
