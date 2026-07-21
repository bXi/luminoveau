#pragma once

// Writes coloured log lines to the SDL console / stdout.

#include "core/log/logsink.h"

/// @brief Log sink that prints ANSI-coloured entries to the console.
class SDLConsoleSink : public LogSink {
public:
    explicit SDLConsoleSink(LogLevel minLevel = LogLevel::Info);

    void Write(const LogEntry &entry) override;

    /// @brief Drops entries below this level.
    void SetMinLevel(LogLevel level) { _minLevel = level; }

private:
    LogLevel _minLevel;
};
