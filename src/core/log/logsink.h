#pragma once

// Abstract log output destination. Implement this and hand it to Log::AddSink to route
// entries somewhere new; the built-in sinks live in core/log/sinks/.

#include "core/log/logentry.h"

/// @brief Base class for log output destinations.
class LogSink {
public:
    virtual ~LogSink() = default;

    /// @brief Called for every entry that passes the logger's minimum level.
    virtual void Write(const LogEntry &entry) = 0;

    /// @brief Flushes any buffered output. No-op by default.
    virtual void Flush() { }
};
