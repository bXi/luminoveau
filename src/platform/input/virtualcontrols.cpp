#include "platform/input/virtualcontrols.h"
#include "platform/window/window.h"
#include <cmath>
#include <algorithm>

#include "draw/draw.h"
#include "draw/text.h"
#include "assets/assethandler.h"

#ifdef LUMINOVEAU_WITH_IMGUI
#include "imgui.h"
#endif

// Support mouse as a pseudo-finger ID for desktop testing
static const SDL_FingerID MOUSE_FINGER_ID = static_cast<SDL_FingerID>(-2);

VirtualControls::VirtualControls()
    : _enabled(false)
    , _joystickMode(JoystickMode::DISABLED)
    , _joystickOffset(0.0f, 0.0f)
    , _joystickRadius(0.0f)
    , _joystickDeadZone(0.15f)
    , _joystickBaseTexture(nullptr)
    , _joystickStickTexture(nullptr)
    , _buttonCount(2)
    , _defaultTexture(nullptr)
    , _buttonGroupOffset(0.0f, 0.0f)
#ifdef LUMINOVEAU_WITH_IMGUI
    , _showDebugWindow(false)
#endif
{
    _joystick.direction    = { 0.0f, 0.0f };
    _joystick.magnitude    = 0.0f;
    _joystick.isActive     = false;
    _joystick.activeFinger = -1;

    // Default geometry using cm-based sizing
    _joystickRadius = _cm(3.0f);
    _joystickOffset = { _cm(4.5f), -_cm(4.5f) }; // Offset from bottom-left corner

    _initializeDefaultTexture();
    SetButtonCount(4);
}

VirtualControls::~VirtualControls() {
}

float VirtualControls::_cm(float wantedCM) const {
#ifdef __ANDROID__
    constexpr float logicalPerCm = 160.0f / 2.54f;
#else
    constexpr float logicalPerCm = 96.0f / 2.54f;
#endif

    float scale = SDL_GetWindowDisplayScale(Window::GetWindow());
    return wantedCM * logicalPerCm * scale * _controlScale;
}

float VirtualControls::_pixelsToCm(float pixels) const {
#ifdef __ANDROID__
    constexpr float logicalPerCm = 160.0f / 2.54f;
#else
    constexpr float logicalPerCm = 96.0f / 2.54f;
#endif

    float scale = SDL_GetWindowDisplayScale(Window::GetWindow());
    return pixels / (logicalPerCm * scale);
}

float VirtualControls::_viewW() const {
    return _usePhysicalCoords ? (float)Window::GetPhysicalWidth() : (float)Window::GetWidth();
}
float VirtualControls::_viewH() const {
    return _usePhysicalCoords ? (float)Window::GetPhysicalHeight() : (float)Window::GetHeight();
}

vf2d VirtualControls::GetJoystickPosition() const {
    // Calculate actual position from offset and window size
    // Offset is from bottom-left corner
    float x = _joystickOffset.x;
    float y = _viewH() + _joystickOffset.y; // Negative offset moves up
    return { x, y };
}

vf2d VirtualControls::VirtualButton::GetScreenPosition(const vf2d &anchorOffset) const {
    // Calculate actual position from individual offset + anchor offset
    // Final position is from bottom-right corner. anchorOffset.z/w unused; the
    // window extents are baked into anchorOffset by the caller (see GetButtonAnchorOffset).
    float x = anchorOffset.x + individualOffset.x;
    float y = anchorOffset.y + individualOffset.y;
    return { x, y };
}

vf2d VirtualControls::GetButtonAnchorOffset() const {
    // Anchor at the bottom-right corner of the active-space window, shifted by the
    // group offset. GetScreenPosition then just adds each button's individual offset.
    return { _viewW() + _buttonGroupOffset.x, _viewH() + _buttonGroupOffset.y };
}

void VirtualControls::_initializeDefaultTexture() {
    _defaultTexture = nullptr;
}

void VirtualControls::Update() {
    if (!_enabled)
        return;

    // Rotation / window resize: re-fit viewport-relative controls.
    if (_relJoystickFrac > 0.0f && (_viewW() != _lastViewW || _viewH() != _lastViewH))
        _applyViewportScale();

    _updateJoystick();
    if (_mouseEmulation)
        _updateMouse();
    _updateButtons();
}

void VirtualControls::SetControlScale(float scale) {
    _relJoystickFrac = 0.0f; // explicit absolute scale disables viewport mode
    _controlScale    = (scale > 0.0f) ? scale : 1.0f;
    // Recompute cm-derived geometry with the new scale.
    _joystickRadius = _cm(3.0f);
    _joystickOffset = { _cm(4.5f), -_cm(4.5f) };
    _layoutButtons();
}

void VirtualControls::SetControlScaleToViewport(float joystickFraction) {
    _relJoystickFrac = (joystickFraction > 0.0f) ? joystickFraction : 0.0f;
    _applyViewportScale();
}

void VirtualControls::_applyViewportScale() {
    if (_relJoystickFrac <= 0.0f)
        return;
    float minDim = std::min(_viewW(), _viewH());
    if (minDim <= 0.0f)
        return;
    // cm(3.0) is the joystick radius at scale 1.0; pick the scale that makes it
    // _relJoystickFrac * minDim. Because cm() includes the (unreliable) display
    // scale, this cancels it out — the result is purely viewport-relative.
    _controlScale   = 1.0f;
    float base      = _cm(3.0f);
    _controlScale   = (base > 0.0f) ? (_relJoystickFrac * minDim / base) : 1.0f;
    _joystickRadius = _cm(3.0f);
    _joystickOffset = { _cm(4.5f), -_cm(4.5f) };
    _layoutButtons();
    _lastViewW = _viewW();
    _lastViewH = _viewH();
}

void VirtualControls::HandleTouchEvent(const SDL_Event *event) {
    if (!_enabled)
        return;

    vf2d         touchPos;
    SDL_FingerID fingerID;

    switch (event->type) {
    case SDL_EVENT_FINGER_DOWN: {
        fingerID = event->tfinger.fingerID;
        touchPos = {
            event->tfinger.x * _viewW(),
            event->tfinger.y * _viewH()
        };

        // Check joystick activation (left half of screen or static area)
        if (_joystickMode != JoystickMode::DISABLED && !_joystick.isActive && _isTouchInJoystickArea(touchPos)) {
            _joystick.isActive     = true;
            _joystick.activeFinger = fingerID;

            _joystick.touchStart = (_joystickMode == JoystickMode::RELATIVE)
                ? touchPos
                : GetJoystickPosition();

            _joystick.touchCurrent = touchPos;
        }
        // Check button activation, then the look region.
        else {
            int buttonIdx = _getButtonAtPosition(touchPos);
            if (buttonIdx >= 0 && buttonIdx < (int)_buttons.size()) {
                _buttons[buttonIdx].isPressed    = true;
                _buttons[buttonIdx].activeFinger = fingerID;
            }
            // Not a button: the right joystick claims the right half (twin-stick), if enabled.
            else if (_joystickRightMode != JoystickMode::DISABLED && !_joystickRight.isActive && _isTouchInRightJoystickArea(touchPos)) {
                _joystickRight.isActive     = true;
                _joystickRight.activeFinger = fingerID;
                _joystickRight.touchStart   = (_joystickRightMode == JoystickMode::RELATIVE)
                      ? touchPos
                      : _getRightJoystickPosition();
                _joystickRight.touchCurrent = touchPos;
            }
            // Otherwise a drag in the right half drives the look region.
            else if (!_lookActive && _isInLookRegion(touchPos)) {
                _lookActive   = true;
                _lookFinger   = fingerID;
                _lookLast     = touchPos;
                _lookStart    = touchPos;
                _lookMaxDist2 = 0.0f;
            }
        }
        break;
    }

    case SDL_EVENT_FINGER_MOTION: {
        fingerID = event->tfinger.fingerID;
        touchPos = {
            event->tfinger.x * _viewW(),
            event->tfinger.y * _viewH()
        };

        // Update joystick if this finger owns it
        if (_joystick.isActive && _joystick.activeFinger == fingerID) {
            _joystick.touchCurrent = touchPos;
        }
        if (_joystickRight.isActive && _joystickRight.activeFinger == fingerID) {
            _joystickRight.touchCurrent = touchPos;
        }

        // Accumulate look delta if this finger owns the look region
        if (_lookActive && _lookFinger == fingerID) {
            _lookAccum = _lookAccum + (touchPos - _lookLast);
            _lookLast  = touchPos;
            vf2d  off  = touchPos - _lookStart;
            float d2   = off.x * off.x + off.y * off.y;
            if (d2 > _lookMaxDist2)
                _lookMaxDist2 = d2;
        }

        // Check if finger moved off any button it was pressing
        for (auto &button : _buttons) {
            if (button.activeFinger == fingerID && button.isPressed) {
                int buttonIdx = _getButtonAtPosition(touchPos);
                // Find which button this finger owns
                int ownerIdx = -1;
                for (int i = 0; i < (int)_buttons.size(); ++i) {
                    if (_buttons[i].activeFinger == fingerID) {
                        ownerIdx = i;
                        break;
                    }
                }
                // If finger moved off its button, release it
                if (buttonIdx != ownerIdx) {
                    button.isPressed    = false;
                    button.activeFinger = -1;
                }
            }
        }
        break;
    }

    case SDL_EVENT_FINGER_UP: {
        fingerID = event->tfinger.fingerID;

        // Release joystick if owned by this finger
        if (_joystick.isActive && _joystick.activeFinger == fingerID) {
            _joystick.isActive     = false;
            _joystick.direction    = { 0.0f, 0.0f };
            _joystick.magnitude    = 0.0f;
            _joystick.activeFinger = -1;
        }
        if (_joystickRight.isActive && _joystickRight.activeFinger == fingerID) {
            _joystickRight.isActive     = false;
            _joystickRight.direction    = { 0.0f, 0.0f };
            _joystickRight.magnitude    = 0.0f;
            _joystickRight.activeFinger = -1;
        }

        // Release the look region if owned by this finger. If the touch barely
        // moved, it was a tap (not a swipe) → flag it for tap-to-fire.
        if (_lookActive && _lookFinger == fingerID) {
            float thr = 0.04f * std::min(_viewW(), _viewH());
            if (_lookMaxDist2 <= thr * thr)
                _lookTap = true;
            _lookActive = false;
            _lookFinger = static_cast<SDL_FingerID>(-1);
        }

        // Release any buttons owned by this finger
        for (auto &button : _buttons) {
            if (button.activeFinger == fingerID) {
                button.isPressed    = false;
                button.activeFinger = -1;
            }
        }
        break;
    }
    }
}

void VirtualControls::_updateMouse() {
    bool mouseDown = Input::MouseButtonDown(SDL_BUTTON_LEFT);
    vf2d mousePos  = Input::GetMousePosition();
    // Input::GetMousePosition() is logical; convert to physical when needed so the
    // mouse-as-finger test agrees with the (physical) render + hit-test.
    if (_usePhysicalCoords)
        mousePos = mousePos * Window::GetDisplayScale();
    float halfWidth = _viewW() * 0.5f;

    if (mouseDown) {
        // Joystick activation
        if (_joystickMode != JoystickMode::DISABLED) {
            if (!_joystick.isActive) {
                // Only activate on left half (or static area)
                if (_isTouchInJoystickArea(mousePos)) {
                    _joystick.isActive     = true;
                    _joystick.activeFinger = MOUSE_FINGER_ID;

                    _joystick.touchStart = (_joystickMode == JoystickMode::RELATIVE)
                        ? mousePos
                        : GetJoystickPosition();

                    _joystick.touchCurrent = mousePos;
                }
            } else {
                // If mouse owns the joystick, update it
                if (_joystick.activeFinger == MOUSE_FINGER_ID) {
                    _joystick.touchCurrent = mousePos;
                }
            }
        }

        // Button activation (right half only if joystick not active)
        if (!_joystick.isActive && mousePos.x >= halfWidth) {
            int idx = _getButtonAtPosition(mousePos);
            if (idx >= 0) {
                auto &b = _buttons[idx];
                if (b.activeFinger == -1 || b.activeFinger == MOUSE_FINGER_ID) {
                    b.isPressed    = true;
                    b.activeFinger = MOUSE_FINGER_ID;
                }
            }
        }

        // Look region via mouse (right half, not on a button): drag = camera delta.
        if (_lookEnabled && !_joystick.isActive && _isInLookRegion(mousePos)
            && _getButtonAtPosition(mousePos) < 0) {
            if (!_lookActive) {
                _lookActive   = true;
                _lookFinger   = MOUSE_FINGER_ID;
                _lookLast     = mousePos;
                _lookStart    = mousePos;
                _lookMaxDist2 = 0.0f;
            } else if (_lookFinger == MOUSE_FINGER_ID) {
                _lookAccum = _lookAccum + (mousePos - _lookLast);
                _lookLast  = mousePos;
                vf2d  off  = mousePos - _lookStart;
                float d2   = off.x * off.x + off.y * off.y;
                if (d2 > _lookMaxDist2)
                    _lookMaxDist2 = d2;
            }
        }

        // Check if mouse moved off any button it was pressing
        for (auto &button : _buttons) {
            if (button.activeFinger == MOUSE_FINGER_ID && button.isPressed) {
                int buttonIdx = _getButtonAtPosition(mousePos);
                // Find which button the mouse owns
                int ownerIdx = -1;
                for (int i = 0; i < (int)_buttons.size(); ++i) {
                    if (_buttons[i].activeFinger == MOUSE_FINGER_ID) {
                        ownerIdx = i;
                        break;
                    }
                }
                // If mouse moved off its button, release it
                if (buttonIdx != ownerIdx) {
                    button.isPressed    = false;
                    button.activeFinger = -1;
                }
            }
        }
    } else {
        // Release joystick if mouse owned it
        if (_joystick.activeFinger == MOUSE_FINGER_ID) {
            _joystick.isActive     = false;
            _joystick.activeFinger = -1;
            _joystick.direction    = { 0.0f, 0.0f };
            _joystick.magnitude    = 0.0f;
        }

        // Release buttons owned by mouse
        for (auto &b : _buttons) {
            if (b.activeFinger == MOUSE_FINGER_ID) {
                b.isPressed    = false;
                b.activeFinger = -1;
            }
        }

        // Release the look region if the mouse owned it; barely-moved = tap.
        if (_lookActive && _lookFinger == MOUSE_FINGER_ID) {
            float thr = 0.04f * std::min(_viewW(), _viewH());
            if (_lookMaxDist2 <= thr * thr)
                _lookTap = true;
            _lookActive = false;
            _lookFinger = static_cast<SDL_FingerID>(-1);
        }
    }
}

// Compute a stick's normalized direction + dead-zoned magnitude from its touch start/current.
static void computeStick(VirtualControls::JoystickState &js, float radius, float deadZone) {
    vf2d  delta    = js.touchCurrent - js.touchStart;
    float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (distance > radius) {
        delta    = delta * (radius / distance);
        distance = radius;
    }
    float mag = radius > 0.0f ? distance / radius : 0.0f;
    if (mag < deadZone || distance <= 0.0f) {
        js.direction = { 0.0f, 0.0f };
        js.magnitude = 0.0f;
    } else {
        js.direction = delta / distance;
        js.magnitude = (mag - deadZone) / (1.0f - deadZone);
    }
}

void VirtualControls::_updateJoystick() {
    if (!_joystick.isActive || _joystickMode == JoystickMode::DISABLED) {
        _joystick.direction = { 0.0f, 0.0f };
        _joystick.magnitude = 0.0f;
    } else {
        computeStick(_joystick, _joystickRadius, _joystickDeadZone);
    }

    if (!_joystickRight.isActive || _joystickRightMode == JoystickMode::DISABLED) {
        _joystickRight.direction = { 0.0f, 0.0f };
        _joystickRight.magnitude = 0.0f;
    } else {
        computeStick(_joystickRight, _joystickRadius, _joystickDeadZone);
    }
}

void VirtualControls::_updateButtons() {
    for (auto &button : _buttons) {
        button.wasPressed = button.isPressed;
    }
}

void VirtualControls::Render() {
    if (!_enabled)
        return;

    _renderJoystick();
    _renderButtons();
}

// Draw one stick's base ring + (when active) its knob at basePos.
void VirtualControls::_renderStick(const JoystickState &js, JoystickMode mode, const vf2d &basePos) {
    TextureAsset *baseTexture  = _joystickBaseTexture ? _joystickBaseTexture : _defaultTexture;
    TextureAsset *stickTexture = _joystickStickTexture ? _joystickStickTexture : _defaultTexture;

    if (baseTexture) {
        Draw::Texture(*baseTexture, basePos, { _joystickRadius * 2.0f, _joystickRadius * 2.0f }, { 255, 255, 255, 128 });
    } else {
        Draw::CircleFilled(basePos, _joystickRadius, { 255, 255, 255, 128 });
    }

    if (js.isActive) {
        vf2d  stickPos = basePos + (js.touchCurrent - js.touchStart);
        vf2d  delta    = stickPos - basePos;
        float dist     = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (dist > _joystickRadius)
            stickPos = basePos + (delta / dist) * _joystickRadius;

        float stickRadius = _joystickRadius * 0.5f;
        if (stickTexture) {
            Draw::Texture(*stickTexture, stickPos, { stickRadius * 2.0f, stickRadius * 2.0f }, { 255, 255, 255, 200 });
        } else {
            Draw::CircleFilled(stickPos, stickRadius, { 255, 255, 255, 200 });
        }
    }
}

void VirtualControls::_renderJoystick() {
    if (_joystickMode != JoystickMode::DISABLED && !(_joystickMode == JoystickMode::RELATIVE && !_joystick.isActive)) {
        vf2d basePos = (_joystickMode == JoystickMode::RELATIVE) ? _joystick.touchStart : GetJoystickPosition();
        _renderStick(_joystick, _joystickMode, basePos);
    }
    if (_joystickRightMode != JoystickMode::DISABLED && !(_joystickRightMode == JoystickMode::RELATIVE && !_joystickRight.isActive)) {
        vf2d basePos = (_joystickRightMode == JoystickMode::RELATIVE) ? _joystickRight.touchStart : _getRightJoystickPosition();
        _renderStick(_joystickRight, _joystickRightMode, basePos);
    }
}

void VirtualControls::_renderButtons() {
    vf2d anchorOffset = GetButtonAnchorOffset();

    for (const auto &button : _buttons) {
        TextureAsset *texture = button.customTexture ? button.customTexture : _defaultTexture;
        uint8_t       alpha   = button.isPressed ? 255 : 128;

        vf2d screenPos = button.GetScreenPosition(anchorOffset);

        if (texture) {
            Draw::Texture(
                *texture,
                screenPos,
                { button.radius * 2.0f, button.radius * 2.0f },
                { 255, 255, 255, alpha });
        } else {
            Draw::CircleFilled(screenPos, button.radius, { 255, 255, 255, alpha });
        }

        // Centered text label (default font), scaled to fit within the button.
        if (!button.label.empty()) {
            Font  font = AssetHandler::GetDefaultFont();
            float size = button.radius * 0.55f;
            vf2d  ts   = Text::GetRenderedTextSize(font, button.label, size);
            float maxW = button.radius * 1.5f; // keep the label inside the circle
            if (ts.x > maxW && ts.x > 0.0f) {
                size *= maxW / ts.x;
                ts = Text::GetRenderedTextSize(font, button.label, size);
            }
            Text::DrawText(font, screenPos - ts * 0.5f, button.label,
                { 35, 35, 35, alpha }, size);
        }
    }
}

void VirtualControls::SetButtonCount(int count) {
    count        = std::max(0, std::min(4, count));
    _buttonCount = count;

    _buttons.clear();
    _buttons.resize(count);

    for (auto &button : _buttons) {
        button.isPressed     = false;
        button.wasPressed    = false;
        button.activeFinger  = -1;
        button.customTexture = nullptr;
    }

    _layoutButtons();
}

void VirtualControls::_layoutButtons() {
    if (_buttonCount == 0)
        return;

    if (_buttonCount >= 1) {
        // Primary button (A) - large, center (at anchor point)
        _buttons[0].individualOffset = { -_cm(3.20f), -_cm(2.90f) };
        _buttons[0].radius           = _cm(2.2f);
    }

    if (_buttonCount >= 2) {
        // Secondary button (B) - smaller, to the right and down
        _buttons[1].individualOffset = { -_cm(7.10f), -_cm(2.0f) };
        _buttons[1].radius           = _cm(1.4f);
    }

    if (_buttonCount >= 3) {
        // Third button (X) - above primary
        _buttons[2].individualOffset = { -_cm(2.f), -_cm(6.5f) };
        _buttons[2].radius           = _cm(1.2f);
    }

    if (_buttonCount >= 4) {
        // Fourth button (Y) - above and to the left
        _buttons[3].individualOffset = { -_cm(5.1f), -_cm(6.5f) };
        _buttons[3].radius           = _cm(1.2f);
    }
}

void VirtualControls::SetButtonTexture(int buttonIndex, TextureAsset *texture) {
    if (buttonIndex >= 0 && buttonIndex < (int)_buttons.size()) {
        _buttons[buttonIndex].customTexture = texture;
    }
}

bool VirtualControls::IsButtonPressed(int buttonIndex) const {
    if (buttonIndex >= 0 && buttonIndex < (int)_buttons.size()) {
        return _buttons[buttonIndex].isPressed;
    }
    return false;
}

bool VirtualControls::IsButtonJustPressed(int buttonIndex) const {
    if (buttonIndex >= 0 && buttonIndex < (int)_buttons.size()) {
        const auto &b = _buttons[buttonIndex];
        return b.isPressed && !b.wasPressed;
    }
    return false;
}

bool VirtualControls::IsButtonJustReleased(int buttonIndex) const {
    if (buttonIndex >= 0 && buttonIndex < (int)_buttons.size()) {
        const auto &b = _buttons[buttonIndex];
        return !b.isPressed && b.wasPressed;
    }
    return false;
}

bool VirtualControls::_isTouchInJoystickArea(const vf2d &touchPos) {
    if (_joystickMode == JoystickMode::STATIC) {
        vf2d  joystickPos = GetJoystickPosition();
        vf2d  delta       = touchPos - joystickPos;
        float distSq      = delta.x * delta.x + delta.y * delta.y;
        return distSq <= (_joystickRadius * _joystickRadius * 2.0f);
    }

    // RELATIVE mode: left half of screen
    return touchPos.x < _viewW() / 2.0f;
}

vf2d VirtualControls::_getRightJoystickPosition() const {
    // Mirror the left stick's inset to the bottom-right corner.
    return { _viewW() - _joystickOffset.x, _viewH() + _joystickOffset.y };
}

bool VirtualControls::_isTouchInRightJoystickArea(const vf2d &touchPos) const {
    if (_joystickRightMode == JoystickMode::STATIC) {
        vf2d delta = touchPos - _getRightJoystickPosition();
        return (delta.x * delta.x + delta.y * delta.y) <= (_joystickRadius * _joystickRadius * 2.0f);
    }
    // RELATIVE mode: right half of screen.
    return touchPos.x >= _viewW() / 2.0f;
}

bool VirtualControls::_isInLookRegion(const vf2d &pos) const {
    // Right half of the screen; the look region only claims a drag once buttons
    // and the joystick have had first refusal (checked by the caller).
    return _lookEnabled && pos.x >= _viewW() / 2.0f;
}

vf2d VirtualControls::ConsumeLookDelta() {
    vf2d d     = _lookAccum;
    _lookAccum = { 0.0f, 0.0f };
    return d;
}

bool VirtualControls::ConsumeLookTap() {
    bool t   = _lookTap;
    _lookTap = false;
    return t;
}

void VirtualControls::SetButtonLabel(int buttonIndex, const std::string &label) {
    if (buttonIndex >= 0 && buttonIndex < (int)_buttons.size()) {
        _buttons[buttonIndex].label = label;
    }
}

int VirtualControls::_getButtonAtPosition(const vf2d &position) {
    vf2d anchorOffset = GetButtonAnchorOffset();

    for (int i = 0; i < (int)_buttons.size(); ++i) {
        vf2d  buttonPos = _buttons[i].GetScreenPosition(anchorOffset);
        vf2d  delta     = position - buttonPos;
        float distSq    = delta.x * delta.x + delta.y * delta.y;
        float radiusSq  = _buttons[i].radius * _buttons[i].radius;

        if (distSq <= radiusSq) {
            return i;
        }
    }

    return -1;
}

#ifdef LUMINOVEAU_WITH_IMGUI
void VirtualControls::RenderDebugWindow() {
    if (!ImGui::Begin("Virtual Controls Debug", &_showDebugWindow)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Virtual Controls Debug");
    ImGui::Separator();

    // Enabled state
    ImGui::Checkbox("Enabled", &_enabled);
    ImGui::SameLine();
    if (ImGui::Button(_showDebugWindow ? "Hide Debug" : "Show Debug")) {
        _showDebugWindow = !_showDebugWindow;
    }

    ImGui::Separator();

    // Joystick section
    if (ImGui::CollapsingHeader("Joystick", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Mode selection
        const char *modeNames[] = { "DISABLED", "STATIC", "RELATIVE" };
        int         currentMode = (int)_joystickMode;
        if (ImGui::Combo("Mode", &currentMode, modeNames, 3)) {
            _joystickMode = (JoystickMode)currentMode;
        }

        if (_joystickMode != JoystickMode::DISABLED) {
            // Joystick parameters
            float radiusCm = _pixelsToCm(_joystickRadius);
            if (ImGui::DragFloat("Radius (cm)", &radiusCm, 0.1f, 0.5f, 10.0f, "%.2f cm")) {
                _joystickRadius = _cm(radiusCm);
            }

            float deadZone = _joystickDeadZone;
            if (ImGui::SliderFloat("Dead Zone", &deadZone, 0.0f, 0.5f)) {
                _joystickDeadZone = deadZone;
            }

            // Static offset (only for STATIC mode)
            if (_joystickMode == JoystickMode::STATIC) {
                ImGui::TextDisabled("(Offset from bottom-left corner)");
                float offsetCm[2] = { _pixelsToCm(_joystickOffset.x), _pixelsToCm(_joystickOffset.y) };
                if (ImGui::DragFloat2("Offset (cm)", offsetCm, 0.1f, -20.0f, 20.0f, "%.2f cm")) {
                    _joystickOffset = { _cm(offsetCm[0]), _cm(offsetCm[1]) };
                }
                ImGui::TextDisabled("Negative Y moves up from bottom");
            }

            ImGui::Separator();

            // Joystick state
            ImGui::Text("State:");
            ImGui::Text("  Active: %s", _joystick.isActive ? "YES" : "NO");

            if (_joystick.isActive) {
                ImGui::Text("  Direction: (%.2f, %.2f)", _joystick.direction.x, _joystick.direction.y);
                ImGui::Text("  Magnitude: %.2f", _joystick.magnitude);
                ImGui::Text("  Touch Start: (%.1f, %.1f)", _joystick.touchStart.x, _joystick.touchStart.y);
                ImGui::Text("  Touch Current: (%.1f, %.1f)", _joystick.touchCurrent.x, _joystick.touchCurrent.y);
                ImGui::Text("  Finger ID: %lld", (long long)_joystick.activeFinger);
            } else {
                ImGui::TextDisabled("  (Not active)");
            }
        }
    }

    ImGui::Separator();

    // Buttons section
    if (ImGui::CollapsingHeader("Buttons", ImGuiTreeNodeFlags_DefaultOpen)) {
        int buttonCount = _buttonCount;
        if (ImGui::SliderInt("Button Count", &buttonCount, 0, 4)) {
            SetButtonCount(buttonCount);
        }

        // Button group positioning
        ImGui::Separator();
        ImGui::Text("Button Group Positioning:");
        ImGui::TextDisabled("(Offset from bottom-right corner)");

        float groupOffsetCm[2] = { _pixelsToCm(_buttonGroupOffset.x), _pixelsToCm(_buttonGroupOffset.y) };
        if (ImGui::DragFloat2("Group Offset (cm)", groupOffsetCm, 0.1f, -20.0f, 20.0f, "%.2f cm")) {
            SetButtonGroupOffset({ _cm(groupOffsetCm[0]), _cm(groupOffsetCm[1]) });
        }

        ImGui::SameLine();
        if (ImGui::Button("Reset##GroupOffset")) {
            SetButtonGroupOffset({ 0.0f, 0.0f });
        }

        // Show anchor point info
        ImGui::Separator();
        vf2d anchorOffset = GetButtonAnchorOffset();
        vf2d anchorScreen = { Window::GetWidth() + anchorOffset.x, Window::GetHeight() + anchorOffset.y };
        ImGui::TextDisabled("Anchor Point (screen): (%.1f, %.1f) px", anchorScreen.x, anchorScreen.y);
        ImGui::TextDisabled("All button offsets are relative to this anchor");

        ImGui::Separator();

        for (int i = 0; i < _buttonCount; ++i) {
            ImGui::PushID(i);

            const char *buttonNames[] = { "A (Primary)", "B (Secondary)", "X (Third)", "Y (Fourth)" };
            if (ImGui::TreeNode(buttonNames[i])) {
                auto &btn = _buttons[i];

                // Show actual screen position (read-only)
                vf2d screenPos = btn.GetScreenPosition(anchorOffset);
                ImGui::TextDisabled("Screen Position: (%.1f, %.1f) px", screenPos.x, screenPos.y);
                ImGui::Separator();

                // Edit individual offset (relative to anchor point)
                ImGui::Text("Individual Offset (from anchor):");
                ImGui::TextDisabled("Copy these values to _layoutButtons()");
                float offsetCm[2] = { _pixelsToCm(btn.individualOffset.x), _pixelsToCm(btn.individualOffset.y) };
                if (ImGui::DragFloat2("Offset (cm)", offsetCm, 0.1f, -10.0f, 10.0f, "%.2f cm")) {
                    btn.individualOffset = { _cm(offsetCm[0]), _cm(offsetCm[1]) };
                }

                // Radius
                float radiusCm = _pixelsToCm(btn.radius);
                if (ImGui::DragFloat("Radius (cm)", &radiusCm, 0.1f, 0.3f, 5.0f, "%.2f cm")) {
                    btn.radius = _cm(radiusCm);
                }

                // State
                ImGui::Separator();
                ImGui::Text("State:");
                ImGui::Text("  Pressed: %s", btn.isPressed ? "YES" : "NO");
                ImGui::Text("  Was Pressed: %s", btn.wasPressed ? "YES" : "NO");

                if (btn.activeFinger != -1) {
                    ImGui::Text("  Finger ID: %lld", (long long)btn.activeFinger);
                } else {
                    ImGui::TextDisabled("  (No active finger)");
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        // Quick reset to default layout
        ImGui::Separator();
        if (ImGui::Button("Reset to Default Layout")) {
            _layoutButtons();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(Respects group offset)");

        // Code example
        if (_buttonCount > 0) {
            ImGui::Separator();
            if (ImGui::TreeNode("Code Example (copy to _layoutButtons)")) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 0.7f, 1.0f));

                for (int i = 0; i < _buttonCount; ++i) {
                    float x = _pixelsToCm(_buttons[i].individualOffset.x);
                    float y = _pixelsToCm(_buttons[i].individualOffset.y);
                    float r = _pixelsToCm(_buttons[i].radius);

                    ImGui::Text("_buttons[%d].individualOffset = {cm(%.2ff), cm(%.2ff)};", i, x, y);
                    ImGui::Text("_buttons[%d].radius = cm(%.2ff);", i, r);
                    if (i < _buttonCount - 1)
                        ImGui::Spacing();
                }

                ImGui::PopStyleColor();
                ImGui::TreePop();
            }
        }
    }

    ImGui::Separator();

    // Screen info
    if (ImGui::CollapsingHeader("Screen Info")) {
        ImGui::Text("Window Size: %.0fx%.0f px", Window::GetWidth(), Window::GetHeight());
        ImGui::Text("             %.2fx%.2f cm", _pixelsToCm(Window::GetWidth()), _pixelsToCm(Window::GetHeight()));
        ImGui::Separator();
        ImGui::Text("Window Scale: %.2f", Window::GetScale());
        ImGui::Text("Display Scale (DPI): %.2f", SDL_GetWindowDisplayScale(Window::GetWindow()));
    }

    ImGui::End();
}
#endif
