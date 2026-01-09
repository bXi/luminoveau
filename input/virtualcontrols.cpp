#include "virtualcontrols.h"
#include "draw/drawhandler.h"
#include "window/windowhandler.h"
#include <cmath>

#ifdef LUMINOVEAU_WITH_IMGUI
#include "imgui.h"
#endif

// Support mouse as a pseudo-finger ID for desktop testing
static const SDL_FingerID MOUSE_FINGER_ID = static_cast<SDL_FingerID>(-2);

VirtualControls::VirtualControls()
    : m_enabled(false)
    , m_joystickMode(JoystickMode::DISABLED)
    , m_joystickOffset(0.0f, 0.0f)
    , m_joystickRadius(0.0f)
    , m_joystickDeadZone(0.15f)
    , m_joystickBaseTexture(nullptr)
    , m_joystickStickTexture(nullptr)
    , m_buttonCount(2)
    , m_defaultTexture(nullptr)
    , m_buttonGroupOffset(0.0f, 0.0f)
#ifdef LUMINOVEAU_WITH_IMGUI
    , m_showDebugWindow(false)
#endif
{
    m_joystick.direction = {0.0f, 0.0f};
    m_joystick.magnitude = 0.0f;
    m_joystick.isActive = false;
    m_joystick.activeFinger = -1;

    // Default geometry using cm-based sizing
    m_joystickRadius = cm(3.0f);
    m_joystickOffset = {cm(4.5f), -cm(4.5f)};  // Offset from bottom-left corner

    InitializeDefaultTexture();
    SetButtonCount(4);
}

VirtualControls::~VirtualControls() {
}

float VirtualControls::cm(float wantedCM) const {
#ifdef __ANDROID__
    constexpr float logicalPerCm = 160.0f / 2.54f;
#else
    constexpr float logicalPerCm = 96.0f / 2.54f;
#endif

    float scale = SDL_GetWindowDisplayScale(Window::GetWindow());
    return wantedCM * logicalPerCm * scale;
}

float VirtualControls::pixelsToCm(float pixels) const {
#ifdef __ANDROID__
    constexpr float logicalPerCm = 160.0f / 2.54f;
#else
    constexpr float logicalPerCm = 96.0f / 2.54f;
#endif

    float scale = SDL_GetWindowDisplayScale(Window::GetWindow());
    return pixels / (logicalPerCm * scale);
}

vf2d VirtualControls::GetJoystickPosition() const {
    // Calculate actual position from offset and window size
    // Offset is from bottom-left corner
    float x = m_joystickOffset.x;
    float y = Window::GetHeight() + m_joystickOffset.y;  // Negative offset moves up
    return {x, y};
}

vf2d VirtualControls::VirtualButton::GetScreenPosition(const vf2d& anchorOffset) const {
    // Calculate actual position from individual offset + anchor offset
    // Final position is from bottom-right corner
    float x = Window::GetWidth() + anchorOffset.x + individualOffset.x;
    float y = Window::GetHeight() + anchorOffset.y + individualOffset.y;
    return {x, y};
}

vf2d VirtualControls::GetButtonAnchorOffset() const {
    // Group offset moves the anchor point from bottom-right corner (0, 0)
    // Default anchor is at bottom-right corner
    return {m_buttonGroupOffset.x, m_buttonGroupOffset.y};
}

void VirtualControls::InitializeDefaultTexture() {
    m_defaultTexture = nullptr;
}

void VirtualControls::Update() {
    if (!m_enabled) return;

    UpdateJoystick();
    UpdateMouse();
    UpdateButtons();
}

void VirtualControls::HandleTouchEvent(const SDL_Event *event) {
    if (!m_enabled) return;

    vf2d touchPos;
    SDL_FingerID fingerID;

    switch (event->type) {
        case SDL_EVENT_FINGER_DOWN: {
            fingerID = event->tfinger.fingerID;
            touchPos = {
                event->tfinger.x * Window::GetWidth(),
                event->tfinger.y * Window::GetHeight()
            };

            // Check joystick activation (left half of screen or static area)
            if (m_joystickMode != JoystickMode::DISABLED &&
                !m_joystick.isActive &&
                IsTouchInJoystickArea(touchPos)) {
                m_joystick.isActive = true;
                m_joystick.activeFinger = fingerID;

                m_joystick.touchStart = (m_joystickMode == JoystickMode::RELATIVE)
                                            ? touchPos
                                            : GetJoystickPosition();

                m_joystick.touchCurrent = touchPos;
            }
                // Check button activation
            else {
                int buttonIdx = GetButtonAtPosition(touchPos);
                if (buttonIdx >= 0 && buttonIdx < (int) m_buttons.size()) {
                    m_buttons[buttonIdx].isPressed = true;
                    m_buttons[buttonIdx].activeFinger = fingerID;
                }
            }
            break;
        }

        case SDL_EVENT_FINGER_MOTION: {
            fingerID = event->tfinger.fingerID;
            touchPos = {
                event->tfinger.x * Window::GetWidth(),
                event->tfinger.y * Window::GetHeight()
            };

            // Update joystick if this finger owns it
            if (m_joystick.isActive && m_joystick.activeFinger == fingerID) {
                m_joystick.touchCurrent = touchPos;
            }
            
            // Check if finger moved off any button it was pressing
            for (auto &button: m_buttons) {
                if (button.activeFinger == fingerID && button.isPressed) {
                    int buttonIdx = GetButtonAtPosition(touchPos);
                    // Find which button this finger owns
                    int ownerIdx = -1;
                    for (int i = 0; i < (int)m_buttons.size(); ++i) {
                        if (m_buttons[i].activeFinger == fingerID) {
                            ownerIdx = i;
                            break;
                        }
                    }
                    // If finger moved off its button, release it
                    if (buttonIdx != ownerIdx) {
                        button.isPressed = false;
                        button.activeFinger = -1;
                    }
                }
            }
            break;
        }

        case SDL_EVENT_FINGER_UP: {
            fingerID = event->tfinger.fingerID;

            // Release joystick if owned by this finger
            if (m_joystick.isActive && m_joystick.activeFinger == fingerID) {
                m_joystick.isActive = false;
                m_joystick.direction = {0.0f, 0.0f};
                m_joystick.magnitude = 0.0f;
                m_joystick.activeFinger = -1;
            }

            // Release any buttons owned by this finger
            for (auto &button: m_buttons) {
                if (button.activeFinger == fingerID) {
                    button.isPressed = false;
                    button.activeFinger = -1;
                }
            }
            break;
        }
    }
}

void VirtualControls::UpdateMouse() {
    bool mouseDown = Input::MouseButtonDown(SDL_BUTTON_LEFT);
    vf2d mousePos = Input::GetMousePosition();
    float halfWidth = Window::GetWidth() * 0.5f;

    if (mouseDown) {
        // Joystick activation
        if (m_joystickMode != JoystickMode::DISABLED) {
            if (!m_joystick.isActive) {
                // Only activate on left half (or static area)
                if (IsTouchInJoystickArea(mousePos)) {
                    m_joystick.isActive = true;
                    m_joystick.activeFinger = MOUSE_FINGER_ID;

                    m_joystick.touchStart = (m_joystickMode == JoystickMode::RELATIVE)
                                                ? mousePos
                                                : GetJoystickPosition();

                    m_joystick.touchCurrent = mousePos;
                }
            } else {
                // If mouse owns the joystick, update it
                if (m_joystick.activeFinger == MOUSE_FINGER_ID) {
                    m_joystick.touchCurrent = mousePos;
                }
            }
        }

        // Button activation (right half only if joystick not active)
        if (!m_joystick.isActive && mousePos.x >= halfWidth) {
            int idx = GetButtonAtPosition(mousePos);
            if (idx >= 0) {
                auto &b = m_buttons[idx];
                if (b.activeFinger == -1 || b.activeFinger == MOUSE_FINGER_ID) {
                    b.isPressed = true;
                    b.activeFinger = MOUSE_FINGER_ID;
                }
            }
        }
        
        // Check if mouse moved off any button it was pressing
        for (auto &button: m_buttons) {
            if (button.activeFinger == MOUSE_FINGER_ID && button.isPressed) {
                int buttonIdx = GetButtonAtPosition(mousePos);
                // Find which button the mouse owns
                int ownerIdx = -1;
                for (int i = 0; i < (int)m_buttons.size(); ++i) {
                    if (m_buttons[i].activeFinger == MOUSE_FINGER_ID) {
                        ownerIdx = i;
                        break;
                    }
                }
                // If mouse moved off its button, release it
                if (buttonIdx != ownerIdx) {
                    button.isPressed = false;
                    button.activeFinger = -1;
                }
            }
        }
    } else {
        // Release joystick if mouse owned it
        if (m_joystick.activeFinger == MOUSE_FINGER_ID) {
            m_joystick.isActive = false;
            m_joystick.activeFinger = -1;
            m_joystick.direction = {0.0f, 0.0f};
            m_joystick.magnitude = 0.0f;
        }

        // Release buttons owned by mouse
        for (auto &b: m_buttons) {
            if (b.activeFinger == MOUSE_FINGER_ID) {
                b.isPressed = false;
                b.activeFinger = -1;
            }
        }
    }
}

void VirtualControls::UpdateJoystick() {
    if (!m_joystick.isActive || m_joystickMode == JoystickMode::DISABLED) {
        m_joystick.direction = {0.0f, 0.0f};
        m_joystick.magnitude = 0.0f;
        return;
    }

    // Calculate direction vector
    vf2d delta = m_joystick.touchCurrent - m_joystick.touchStart;
    float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);

    // Clamp to joystick radius
    if (distance > m_joystickRadius) {
        delta = delta * (m_joystickRadius / distance);
        distance = m_joystickRadius;
    }

    // Calculate magnitude (0.0 to 1.0)
    float mag = distance / m_joystickRadius;

    // Apply dead zone
    if (mag < m_joystickDeadZone) {
        m_joystick.direction = {0.0f, 0.0f};
        m_joystick.magnitude = 0.0f;
    } else if (distance > 0.0f) {
        // Normalize direction
        m_joystick.direction = delta / distance;

        // Remap magnitude to account for dead zone
        m_joystick.magnitude = (mag - m_joystickDeadZone) / (1.0f - m_joystickDeadZone);
    }
}

void VirtualControls::UpdateButtons() {
    for (auto &button: m_buttons) {
        button.wasPressed = button.isPressed;
    }
}

void VirtualControls::Render() {
    if (!m_enabled) return;

    RenderJoystick();
    RenderButtons();
}

void VirtualControls::RenderJoystick() {
    if (m_joystickMode == JoystickMode::DISABLED) return;

    // Don't render if not active in RELATIVE mode
    if (m_joystickMode == JoystickMode::RELATIVE && !m_joystick.isActive) return;

    vf2d basePos = (m_joystickMode == JoystickMode::RELATIVE)
                       ? m_joystick.touchStart
                       : GetJoystickPosition();

    TextureAsset *baseTexture = m_joystickBaseTexture ? m_joystickBaseTexture : m_defaultTexture;
    TextureAsset *stickTexture = m_joystickStickTexture ? m_joystickStickTexture : m_defaultTexture;

    // Render base
    if (baseTexture) {
        Draw::Texture(
            *baseTexture,
            basePos,
            {m_joystickRadius * 2.0f, m_joystickRadius * 2.0f},
            {255, 255, 255, 128}
        );
    } else {
        Draw::CircleFilled(basePos, m_joystickRadius, {255, 255, 255, 128});
    }

    // Render stick
    if (m_joystick.isActive) {
        vf2d stickPos = basePos + (m_joystick.touchCurrent - m_joystick.touchStart);

        // Clamp stick position
        vf2d delta = stickPos - basePos;
        float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (dist > m_joystickRadius) {
            stickPos = basePos + (delta / dist) * m_joystickRadius;
        }

        float stickRadius = m_joystickRadius * 0.5f;

        if (stickTexture) {
            Draw::Texture(
                *stickTexture,
                stickPos,
                {stickRadius * 2.0f, stickRadius * 2.0f},
                {255, 255, 255, 200}
            );
        } else {
            Draw::CircleFilled(stickPos, stickRadius, {255, 255, 255, 200});
        }
    }
}

void VirtualControls::RenderButtons() {
    vf2d anchorOffset = GetButtonAnchorOffset();
    
    for (const auto &button: m_buttons) {
        TextureAsset *texture = button.customTexture ? button.customTexture : m_defaultTexture;
        uint8_t alpha = button.isPressed ? 255 : 128;
        
        vf2d screenPos = button.GetScreenPosition(anchorOffset);

        if (texture) {
            Draw::Texture(
                *texture,
                screenPos,
                {button.radius * 2.0f, button.radius * 2.0f},
                {255, 255, 255, alpha}
            );
        } else {
            Draw::CircleFilled(screenPos, button.radius, {255, 255, 255, alpha});
        }
    }
}

void VirtualControls::SetButtonCount(int count) {
    count = std::max(0, std::min(4, count));
    m_buttonCount = count;

    m_buttons.clear();
    m_buttons.resize(count);

    for (auto &button: m_buttons) {
        button.isPressed = false;
        button.wasPressed = false;
        button.activeFinger = -1;
        button.customTexture = nullptr;
    }

    LayoutButtons();
}

void VirtualControls::LayoutButtons() {
    if (m_buttonCount == 0) return;

    if (m_buttonCount >= 1) {
        // Primary button (A) - large, center (at anchor point)
        m_buttons[0].individualOffset = {-cm(3.20f), -cm(2.90f)};
        m_buttons[0].radius = cm(2.2f);
    }

    if (m_buttonCount >= 2) {
        // Secondary button (B) - smaller, to the right and down
        m_buttons[1].individualOffset = {-cm(7.10f), -cm(2.0f)};
        m_buttons[1].radius = cm(1.4f);
    }

    if (m_buttonCount >= 3) {
        // Third button (X) - above primary
        m_buttons[2].individualOffset = {-cm(2.f), -cm(6.5f)};
        m_buttons[2].radius = cm(1.2f);
    }

    if (m_buttonCount >= 4) {
        // Fourth button (Y) - above and to the left
        m_buttons[3].individualOffset = {-cm(5.1f), -cm(6.5f)};
        m_buttons[3].radius = cm(1.2f);
    }
}

void VirtualControls::SetButtonTexture(int buttonIndex, TextureAsset *texture) {
    if (buttonIndex >= 0 && buttonIndex < (int) m_buttons.size()) {
        m_buttons[buttonIndex].customTexture = texture;
    }
}

bool VirtualControls::IsButtonPressed(int buttonIndex) const {
    if (buttonIndex >= 0 && buttonIndex < (int) m_buttons.size()) {
        return m_buttons[buttonIndex].isPressed;
    }
    return false;
}

bool VirtualControls::IsButtonJustPressed(int buttonIndex) const {
    if (buttonIndex >= 0 && buttonIndex < (int) m_buttons.size()) {
        const auto &b = m_buttons[buttonIndex];
        return b.isPressed && !b.wasPressed;
    }
    return false;
}

bool VirtualControls::IsButtonJustReleased(int buttonIndex) const {
    if (buttonIndex >= 0 && buttonIndex < (int) m_buttons.size()) {
        const auto &b = m_buttons[buttonIndex];
        return !b.isPressed && b.wasPressed;
    }
    return false;
}

bool VirtualControls::IsTouchInJoystickArea(const vf2d &touchPos) {
    if (m_joystickMode == JoystickMode::STATIC) {
        vf2d joystickPos = GetJoystickPosition();
        vf2d delta = touchPos - joystickPos;
        float distSq = delta.x * delta.x + delta.y * delta.y;
        return distSq <= (m_joystickRadius * m_joystickRadius * 2.0f);
    }

    // RELATIVE mode: left half of screen
    return touchPos.x < Window::GetWidth() / 2.0f;
}

int VirtualControls::GetButtonAtPosition(const vf2d &position) {
    vf2d anchorOffset = GetButtonAnchorOffset();
    
    for (int i = 0; i < (int) m_buttons.size(); ++i) {
        vf2d buttonPos = m_buttons[i].GetScreenPosition(anchorOffset);
        vf2d delta = position - buttonPos;
        float distSq = delta.x * delta.x + delta.y * delta.y;
        float radiusSq = m_buttons[i].radius * m_buttons[i].radius;

        if (distSq <= radiusSq) {
            return i;
        }
    }

    return -1;
}

#ifdef LUMINOVEAU_WITH_IMGUI
void VirtualControls::RenderDebugWindow() {
    if (!ImGui::Begin("Virtual Controls Debug", &m_showDebugWindow)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Virtual Controls Debug");
    ImGui::Separator();

    // Enabled state
    ImGui::Checkbox("Enabled", &m_enabled);
    ImGui::SameLine();
    if (ImGui::Button(m_showDebugWindow ? "Hide Debug" : "Show Debug")) {
        m_showDebugWindow = !m_showDebugWindow;
    }

    ImGui::Separator();

    // Joystick section
    if (ImGui::CollapsingHeader("Joystick", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Mode selection
        const char* modeNames[] = {"DISABLED", "STATIC", "RELATIVE"};
        int currentMode = (int)m_joystickMode;
        if (ImGui::Combo("Mode", &currentMode, modeNames, 3)) {
            m_joystickMode = (JoystickMode)currentMode;
        }

        if (m_joystickMode != JoystickMode::DISABLED) {
            // Joystick parameters
            float radiusCm = pixelsToCm(m_joystickRadius);
            if (ImGui::DragFloat("Radius (cm)", &radiusCm, 0.1f, 0.5f, 10.0f, "%.2f cm")) {
                m_joystickRadius = cm(radiusCm);
            }

            float deadZone = m_joystickDeadZone;
            if (ImGui::SliderFloat("Dead Zone", &deadZone, 0.0f, 0.5f)) {
                m_joystickDeadZone = deadZone;
            }

            // Static offset (only for STATIC mode)
            if (m_joystickMode == JoystickMode::STATIC) {
                ImGui::TextDisabled("(Offset from bottom-left corner)");
                float offsetCm[2] = {pixelsToCm(m_joystickOffset.x), pixelsToCm(m_joystickOffset.y)};
                if (ImGui::DragFloat2("Offset (cm)", offsetCm, 0.1f, -20.0f, 20.0f, "%.2f cm")) {
                    m_joystickOffset = {cm(offsetCm[0]), cm(offsetCm[1])};
                }
                ImGui::TextDisabled("Negative Y moves up from bottom");
            }

            ImGui::Separator();

            // Joystick state
            ImGui::Text("State:");
            ImGui::Text("  Active: %s", m_joystick.isActive ? "YES" : "NO");
            
            if (m_joystick.isActive) {
                ImGui::Text("  Direction: (%.2f, %.2f)", m_joystick.direction.x, m_joystick.direction.y);
                ImGui::Text("  Magnitude: %.2f", m_joystick.magnitude);
                ImGui::Text("  Touch Start: (%.1f, %.1f)", m_joystick.touchStart.x, m_joystick.touchStart.y);
                ImGui::Text("  Touch Current: (%.1f, %.1f)", m_joystick.touchCurrent.x, m_joystick.touchCurrent.y);
                ImGui::Text("  Finger ID: %lld", (long long)m_joystick.activeFinger);
            } else {
                ImGui::TextDisabled("  (Not active)");
            }
        }
    }

    ImGui::Separator();

    // Buttons section
    if (ImGui::CollapsingHeader("Buttons", ImGuiTreeNodeFlags_DefaultOpen)) {
        int buttonCount = m_buttonCount;
        if (ImGui::SliderInt("Button Count", &buttonCount, 0, 4)) {
            SetButtonCount(buttonCount);
        }

        // Button group positioning
        ImGui::Separator();
        ImGui::Text("Button Group Positioning:");
        ImGui::TextDisabled("(Offset from bottom-right corner)");
        
        float groupOffsetCm[2] = {pixelsToCm(m_buttonGroupOffset.x), pixelsToCm(m_buttonGroupOffset.y)};
        if (ImGui::DragFloat2("Group Offset (cm)", groupOffsetCm, 0.1f, -20.0f, 20.0f, "%.2f cm")) {
            SetButtonGroupOffset({cm(groupOffsetCm[0]), cm(groupOffsetCm[1])});
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Reset##GroupOffset")) {
            SetButtonGroupOffset({0.0f, 0.0f});
        }
        
        // Show anchor point info
        ImGui::Separator();
        vf2d anchorOffset = GetButtonAnchorOffset();
        vf2d anchorScreen = {Window::GetWidth() + anchorOffset.x, Window::GetHeight() + anchorOffset.y};
        ImGui::TextDisabled("Anchor Point (screen): (%.1f, %.1f) px", anchorScreen.x, anchorScreen.y);
        ImGui::TextDisabled("All button offsets are relative to this anchor");

        ImGui::Separator();

        for (int i = 0; i < m_buttonCount; ++i) {
            ImGui::PushID(i);
            
            const char* buttonNames[] = {"A (Primary)", "B (Secondary)", "X (Third)", "Y (Fourth)"};
            if (ImGui::TreeNode(buttonNames[i])) {
                auto& btn = m_buttons[i];

                // Show actual screen position (read-only)
                vf2d screenPos = btn.GetScreenPosition(anchorOffset);
                ImGui::TextDisabled("Screen Position: (%.1f, %.1f) px", screenPos.x, screenPos.y);
                ImGui::Separator();

                // Edit individual offset (relative to anchor point)
                ImGui::Text("Individual Offset (from anchor):");
                ImGui::TextDisabled("Copy these values to LayoutButtons()");
                float offsetCm[2] = {pixelsToCm(btn.individualOffset.x), pixelsToCm(btn.individualOffset.y)};
                if (ImGui::DragFloat2("Offset (cm)", offsetCm, 0.1f, -10.0f, 10.0f, "%.2f cm")) {
                    btn.individualOffset = {cm(offsetCm[0]), cm(offsetCm[1])};
                }

                // Radius
                float radiusCm = pixelsToCm(btn.radius);
                if (ImGui::DragFloat("Radius (cm)", &radiusCm, 0.1f, 0.3f, 5.0f, "%.2f cm")) {
                    btn.radius = cm(radiusCm);
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
            LayoutButtons();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(Respects group offset)");
        
        // Code example
        if (m_buttonCount > 0) {
            ImGui::Separator();
            if (ImGui::TreeNode("Code Example (copy to LayoutButtons)")) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 0.7f, 1.0f));
                
                for (int i = 0; i < m_buttonCount; ++i) {
                    float x = pixelsToCm(m_buttons[i].individualOffset.x);
                    float y = pixelsToCm(m_buttons[i].individualOffset.y);
                    float r = pixelsToCm(m_buttons[i].radius);
                    
                    ImGui::Text("m_buttons[%d].individualOffset = {cm(%.2ff), cm(%.2ff)};", i, x, y);
                    ImGui::Text("m_buttons[%d].radius = cm(%.2ff);", i, r);
                    if (i < m_buttonCount - 1) ImGui::Spacing();
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
        ImGui::Text("             %.2fx%.2f cm", pixelsToCm(Window::GetWidth()), pixelsToCm(Window::GetHeight()));
        ImGui::Separator();
        ImGui::Text("Window Scale: %.2f", Window::GetScale());
        ImGui::Text("Display Scale (DPI): %.2f", SDL_GetWindowDisplayScale(Window::GetWindow()));
    }

    ImGui::End();
}
#endif
