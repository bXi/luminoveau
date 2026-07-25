#pragma once

// Appends log entries to a file on disk.

#include <cstdio>
#include <string>

#include "core/log/logsink.h"

/// @brief Log sink that writes plain-text entries to a file.
class FileSink : public LogSink {
public:
    explicit FileSink(const std::string &filename, LogLevel minLevel = LogLevel::Debug);
    ~FileSink() override;

    void Write(const LogEntry &entry) override;
    void Flush() override;

private:
    std::string _filename;
    LogLevel    _minLevel;
    FILE       *_file;
};
