#include "core/log/sinks/memorybuffersink.h"

MemoryBufferSink::MemoryBufferSink(size_t maxEntries)
    : _maxEntries(maxEntries) {
    _entries.reserve(maxEntries);
}

void MemoryBufferSink::Write(const LogEntry &entry) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (_entries.size() >= _maxEntries) {
        // Remove oldest entry (FIFO)
        _entries.erase(_entries.begin());
    }

    _entries.push_back(entry);
}

std::vector<LogEntry> MemoryBufferSink::GetEntries(LogLevel minLevel) const {
    std::lock_guard<std::mutex> lock(_mutex);

    std::vector<LogEntry> result;
    result.reserve(_entries.size());

    for (const auto &entry : _entries) {
        if (entry.level >= minLevel) {
            result.push_back(entry);
        }
    }

    return result;
}

std::vector<LogEntry> MemoryBufferSink::GetUserEntries() const {
    std::lock_guard<std::mutex> lock(_mutex);

    std::vector<LogEntry> result;
    result.reserve(_entries.size());

    for (const auto &entry : _entries) {
        if (entry.isUserFacing) {
            result.push_back(entry);
        }
    }

    return result;
}

void MemoryBufferSink::Clear() {
    std::lock_guard<std::mutex> lock(_mutex);
    _entries.clear();
}
