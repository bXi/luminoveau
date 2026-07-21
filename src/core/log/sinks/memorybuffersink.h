#pragma once

// Keeps the most recent entries in memory so they can be shown in-engine (debug overlay)
// or dumped to a file after the fact.

#include <cstddef>
#include <mutex>
#include <vector>

#include "core/log/logsink.h"

/// @brief Log sink that retains a bounded ring of recent entries in memory.
class MemoryBufferSink : public LogSink {
public:
    explicit MemoryBufferSink(size_t maxEntries = 1000);

    void Write(const LogEntry &entry) override;

    /// @brief Returns retained entries at or above the given level.
    std::vector<LogEntry> GetEntries(LogLevel minLevel = LogLevel::Debug) const;
    /// @brief Returns only the entries flagged user-facing.
    std::vector<LogEntry> GetUserEntries() const;
    /// @brief Drops all retained entries.
    void Clear();

private:
    std::vector<LogEntry> _entries;
    size_t                _maxEntries;
    mutable std::mutex    _mutex;
};
