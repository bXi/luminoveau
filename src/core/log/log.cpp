#include "core/log/log.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <sstream>

#ifdef _WIN32

#include <windows.h>

#endif

// Log constructor - auto-initializes on first use
Log::Log()
    : _memoryBufferSink(nullptr) {
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
    _sinks.push_back(std::make_unique<SDLConsoleSink>(LogLevel::Info));

    auto memSink      = std::make_unique<MemoryBufferSink>(1000);
    _memoryBufferSink = memSink.get();
    _sinks.push_back(std::move(memSink));
}

// Log destructor - auto-cleanup on program exit
Log::~Log() {
    _flushAll();
    _sinks.clear();
    _memoryBufferSink = nullptr;
}

void Log::_addSink(std::unique_ptr<LogSink> sink) {
    size_t count;
    {
        std::lock_guard<std::mutex> lock(_sinkMutex);
        _sinks.push_back(std::move(sink));
        count = _sinks.size();
    }
    LOG_INFO("Log sink added ({} total)", count);
}

void Log::_clearSinks() {
    std::lock_guard<std::mutex> lock(_sinkMutex);
    _sinks.clear();
    _memoryBufferSink = nullptr;
}

void Log::_flushAll() {
    std::lock_guard<std::mutex> lock(_sinkMutex);
    for (auto &sink : _sinks) {
        sink->Flush();
    }
}

void Log::_setMinLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(_sinkMutex);

    // Update SDL console sink min level
    for (auto &sink : _sinks) {
        if (auto *sdlSink = dynamic_cast<SDLConsoleSink *>(sink.get())) {
            sdlSink->SetMinLevel(level);
        }
    }
}

std::vector<LogEntry> Log::_getLines(LogLevel minLevel) {
    if (_memoryBufferSink) {
        return _memoryBufferSink->GetEntries(minLevel);
    }
    return {};
}

std::vector<LogEntry> Log::_getUserLines() {
    if (_memoryBufferSink) {
        return _memoryBufferSink->GetUserEntries();
    }
    return {};
}

bool Log::_dumpToFile(const std::string &filename, LogLevel minLevel) {
    auto entries = _getLines(minLevel);
    if (entries.empty()) {
        return false;
    }

    FILE *file = std::fopen(filename.c_str(), "w");
    if (!file) {
        return false;
    }

    for (const auto &entry : entries) {
        std::fprintf(file, "%s\n", entry.ToString().c_str());
    }

    std::fclose(file);
    return true;
}

std::string Log::_extractFilename(const char *path) {
    std::string pathStr(path);

    // Find last slash or backslash
    size_t lastSlash = pathStr.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        return pathStr.substr(lastSlash + 1);
    }

    return pathStr;
}

std::string Log::_cleanFunctionName(const char *funcName) {
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

void Log::_writeToSinks(const LogEntry &entry) {
    std::lock_guard<std::mutex> lock(_sinkMutex);

    for (auto &sink : _sinks) {
        sink->Write(entry);
    }
}
