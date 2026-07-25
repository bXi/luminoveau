// Per-backend mouse-position query. Each GPU backend's window/canvas pipeline
// stores cursor coordinates differently (SDL global state vs. canvas-relative JS
// events vs. Dawn-window SDL state), so the actual implementation lives in a
// backend-specific source file selected by CMake.

#pragma once

#include <cstdint>

#include "math/vectors.h"

/// @brief Per-backend mouse seam. Private impls live in the backend .cpp
///        (platform/input/<backend>/mouseinput.cpp) that CMake links.
class PlatformInputBackend {
public:
    // Returns the mouse position in logical engine coordinates (same space sprite
    // draws use). Caller does not need to apply CanvasToLogical or display-scale fixups.
    static vf2d GetMousePosition() { return Get()._getMousePosition(); }

    // Returns the pressed mouse-button mask in SDL layout (bit0 = left, bit1 = middle,
    // bit2 = right). On the web backend a single touch reports as the left button so
    // pointer/touch drives the same button API; native defers to SDL_GetMouseState.
    static uint32_t GetMouseButtons() { return Get()._getMouseButtons(); }

private:
    vf2d     _getMousePosition();
    uint32_t _getMouseButtons();

public:
    PlatformInputBackend(const PlatformInputBackend &) = delete;

    static PlatformInputBackend &Get() {
        static PlatformInputBackend instance;
        return instance;
    }

private:
    PlatformInputBackend() = default;
};
