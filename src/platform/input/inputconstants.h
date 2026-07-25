#pragma once

// Input constants are a public, SDL-keycode-style SCREAMING_CASE vocabulary.
// NOLINTBEGIN(readability-identifier-naming)
enum class InputType {
    GAMEPAD,
    MOUSE_KB
};
enum class Buttons {
    LEFT,
    RIGHT,
    UP,
    DOWN,
    ACCEPT,
    BACK,
    SHOOT,
    SWITCH_NEXT,
    SWITCH_PREV,
    RUN
};
enum class Action {
    HELD,
    PRESSED
};
// NOLINTEND(readability-identifier-naming)
