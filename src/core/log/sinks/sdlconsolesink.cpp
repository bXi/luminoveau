#include "core/log/sinks/sdlconsolesink.h"

#include <SDL3/SDL.h>
#include <cstdio>

SDLConsoleSink::SDLConsoleSink(LogLevel minLevel)
    : _minLevel(minLevel) { }

void SDLConsoleSink::Write(const LogEntry &entry) {
    if (entry.level < _minLevel) {
        return;
    }

#ifdef __EMSCRIPTEN__
    // SDL_Log → stderr → console.error. printf → stdout → console.log, preserving ANSI colors.
    printf("%s\n", entry.ToColoredString().c_str());
#elif defined(__ANDROID__)
    SDL_Log("%s", entry.ToString().c_str());
#elif defined(__3DS__)
    // stderr is redirected to svcOutputDebugString (consoleDebugInit in lumi_main.cpp), so
    // logs reach the emulator log even when the app dies before the first screen swap.
    // Plain (no ANSI) — the debug log is not a terminal.
    const std::string line = entry.ToString();
    fprintf(stderr, "%s\n", line.c_str());
    // Also mirror to the SD card, flushed per line, so the LAST message before a crash/hang is
    // recoverable on real hardware (SVC debug output is only visible to a debugger). Truncated
    // once per run (static open with "w").
    static FILE *logFile = fopen("sdmc:/lumi_log.txt", "w");
    if (logFile) {
        fprintf(logFile, "%s\n", line.c_str());
        fflush(logFile);
    }
#else
    SDL_Log("%s\n", entry.ToColoredString().c_str());
#endif
}
