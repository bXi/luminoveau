#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Core.h>

#include <SDL3/SDL_mouse.h>

#include "core/log/log.h"

class RmlSystem : public Rml::SystemInterface {
public:
    bool LogMessage(Rml::Log::Type type, const Rml::String &msg) override {
        switch (type) {
        case Rml::Log::LT_ALWAYS:
        case Rml::Log::LT_INFO:
        case Rml::Log::LT_DEBUG:
            LOG_INFO("RmlUi: {}", msg.c_str());
            break;

        case Rml::Log::LT_WARNING:
            LOG_WARNING("RmlUi: {}", msg.c_str());
            break;

        case Rml::Log::LT_ERROR:
            LOG_ERROR("RmlUi: {}", msg.c_str());
            break;
        case Rml::Log::LT_ASSERT:
            LOG_CRITICAL("RmlUi: {}", msg.c_str());
            break;
        default:
            LOG_INFO("RmlUi: {}", msg.c_str());
            break;
        }

        return true; // continue execution
    }

    // RCSS `cursor`: over an element that names one, the matching system cursor; anywhere else the
    // cursor that was there before comes back (the default, or the app's own from Window::SetCursor).
    void SetMouseCursor(const Rml::String &name) override {
        SDL_SystemCursor id;
        if (!systemCursor(name, id)) {
            if (_saved) {
                SDL_SetCursor(_saved);
                _saved = nullptr;
            }
            return;
        }
        if (!_saved)
            _saved = SDL_GetCursor();
        SDL_Cursor *&cursor = _cursors[id];
        if (!cursor)
            cursor = SDL_CreateSystemCursor(id);
        if (cursor)
            SDL_SetCursor(cursor);
    }

private:
    // CSS cursor names (and a few RmlUi samples use) -> SDL's system cursors; false = keep the app's.
    static bool systemCursor(const Rml::String &name, SDL_SystemCursor &out) {
        static const struct {
            const char      *name;
            SDL_SystemCursor id;
        } kMap[] = {
            { "pointer", SDL_SYSTEM_CURSOR_POINTER }, { "hand", SDL_SYSTEM_CURSOR_POINTER },
            { "text", SDL_SYSTEM_CURSOR_TEXT }, { "move", SDL_SYSTEM_CURSOR_MOVE },
            { "crosshair", SDL_SYSTEM_CURSOR_CROSSHAIR }, { "wait", SDL_SYSTEM_CURSOR_WAIT },
            { "progress", SDL_SYSTEM_CURSOR_PROGRESS }, { "not-allowed", SDL_SYSTEM_CURSOR_NOT_ALLOWED },
            { "ew-resize", SDL_SYSTEM_CURSOR_EW_RESIZE }, { "col-resize", SDL_SYSTEM_CURSOR_EW_RESIZE },
            { "ns-resize", SDL_SYSTEM_CURSOR_NS_RESIZE }, { "row-resize", SDL_SYSTEM_CURSOR_NS_RESIZE },
            { "nwse-resize", SDL_SYSTEM_CURSOR_NWSE_RESIZE }, { "nesw-resize", SDL_SYSTEM_CURSOR_NESW_RESIZE },
        };
        for (const auto &m : kMap)
            if (name == m.name) {
                out = m.id;
                return true;
            }
        return false;
    }

    SDL_Cursor *_cursors[SDL_SYSTEM_CURSOR_COUNT] = {}; // made on first use, kept for the session
    SDL_Cursor *_saved                            = nullptr;
};
