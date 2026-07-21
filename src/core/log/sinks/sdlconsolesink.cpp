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
#else
    SDL_Log("%s\n", entry.ToColoredString().c_str());
#endif
}
