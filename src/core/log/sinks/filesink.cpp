#include "core/log/sinks/filesink.h"

#include <SDL3/SDL.h>
#include <cstdio>

FileSink::FileSink(const std::string &filename, LogLevel minLevel)
    : _filename(filename)
    , _minLevel(minLevel)
    , _file(nullptr) {
    _file = std::fopen(_filename.c_str(), "a");
    if (!_file) {
        SDL_Log("Failed to open log file: %s", _filename.c_str());
    }
}

FileSink::~FileSink() {
    if (_file) {
        std::fclose(_file);
    }
}

void FileSink::Write(const LogEntry &entry) {
    if (!_file || entry.level < _minLevel) {
        return;
    }

    std::fprintf(_file, "%s\n", entry.ToString().c_str());
}

void FileSink::Flush() {
    if (_file) {
        std::fflush(_file);
    }
}
