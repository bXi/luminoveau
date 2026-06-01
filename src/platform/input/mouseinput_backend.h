// Per-backend mouse-position query. Each GPU backend's window/canvas pipeline
// stores cursor coordinates differently (SDL global state vs. canvas-relative JS
// events vs. Dawn-window SDL state), so the actual implementation lives in a
// backend-specific source file selected by CMake.

#pragma once

#include "math/vectors.h"

namespace PlatformInputBackend {
    // Returns the mouse position in logical engine coordinates (same space sprite
    // draws use). Caller does not need to apply CanvasToLogical or display-scale fixups.
    vf2d GetMousePosition();
}
