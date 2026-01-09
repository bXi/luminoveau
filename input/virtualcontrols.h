#pragma once

#include "SDL3/SDL.h"
#include "utils/vectors.h"
#include "assettypes/texture.h"
#include <vector>

/**
 * @brief Manages virtual onscreen controls for touch devices
 * 
 * Provides joystick and button controls that can be rendered on screen
 * and respond to touch input. Useful for mobile devices and tablets.
 */
class VirtualControls {
public:
    enum class JoystickMode {
        DISABLED,   // No joystick shown
        STATIC,     // Fixed position joystick
        RELATIVE    // Joystick appears where you first touch
    };

    struct VirtualButton {
        vf2d individualOffset;          // Offset from anchor point (for this button's layout)
        float radius;                   // Button radius
        bool isPressed;                 // Current state
        bool wasPressed;                // Previous state
        SDL_FingerID activeFinger;      // Which finger is pressing this button
        TextureAsset *customTexture;    // Custom texture (nullptr = use default)
        
        /**
         * @brief Get the actual screen position of this button
         * @param anchorOffset The anchor point offset (base position + group offset)
         */
        vf2d GetScreenPosition(const vf2d& anchorOffset) const;
    };

    struct JoystickState {
        vf2d direction;                 // Normalized direction vector
        float magnitude;                // 0.0 to 1.0
        vf2d touchStart;                // Where touch began
        vf2d touchCurrent;              // Current touch position
        SDL_FingerID activeFinger;      // Which finger is controlling
        bool isActive;
    };

    VirtualControls();
    ~VirtualControls();

    // === Lifecycle ===
    /**
     * @brief Update virtual controls state. Call this each frame.
     */
    void Update();

    /**
     * @brief Handle SDL touch events
     * @param event The SDL event to process
     */
    void HandleTouchEvent(const SDL_Event *event);

    /**
     * @brief Render the virtual controls. Call during your render phase.
     */
    void Render();

#ifdef LUMINOVEAU_WITH_IMGUI
    /**
     * @brief Render ImGui debug window for virtual controls
     */
    void RenderDebugWindow();

    /**
     * @brief Show or hide the debug window
     * @param show True to show, false to hide
     */
    void ShowDebugWindow(bool show) { m_showDebugWindow = show; }

    /**
     * @brief Check if debug window is visible
     */
    bool IsDebugWindowVisible() const { return m_showDebugWindow; }
#endif

    // === Configuration ===
    /**
     * @brief Enable or disable virtual controls
     * @param enabled True to enable, false to disable
     */
    void SetEnabled(bool enabled) { m_enabled = enabled; }

    /**
     * @brief Check if virtual controls are enabled
     */
    bool IsEnabled() const { return m_enabled; }

    /**
     * @brief Set the joystick mode
     * @param mode The joystick mode (DISABLED, STATIC, RELATIVE)
     */
    void SetJoystickMode(JoystickMode mode) { m_joystickMode = mode; }

    /**
     * @brief Get the current joystick mode
     */
    JoystickMode GetJoystickMode() const { return m_joystickMode; }

    /**
     * @brief Set the button group offset from bottom-right corner
     * @param offset Offset from bottom-right corner
     */
    void SetButtonGroupOffset(const vf2d& offset) { 
        m_buttonGroupOffset = offset;
        LayoutButtons();
    }

    /**
     * @brief Get the button group offset from bottom-right corner
     */
    vf2d GetButtonGroupOffset() const { return m_buttonGroupOffset; }

    /**
     * @brief Get the anchor point offset (default position + group offset)
     */
    vf2d GetButtonAnchorOffset() const;

    /**
     * @brief Set number of buttons (default 2)
     * @param count Number of buttons to display (0-4)
     */
    void SetButtonCount(int count);

    /**
     * @brief Set the joystick offset from bottom-left corner (for STATIC mode)
     * @param offset Offset from bottom-left corner
     */
    void SetJoystickPosition(const vf2d &offset) { m_joystickOffset = offset; }

    /**
     * @brief Get the actual joystick position (calculated from offset and window size)
     */
    vf2d GetJoystickPosition() const;

    /**
     * @brief Set the joystick base radius
     * @param radius Radius in pixels
     */
    void SetJoystickRadius(float radius) { m_joystickRadius = radius; }

    /**
     * @brief Set the joystick dead zone (0.0 to 1.0)
     * @param deadZone Dead zone threshold
     */
    void SetJoystickDeadZone(float deadZone) { m_joystickDeadZone = deadZone; }

    // === Textures ===
    /**
     * @brief Set custom texture for joystick base
     * @param texture Pointer to texture (nullptr to use default)
     */
    void SetJoystickBaseTexture(TextureAsset *texture) { m_joystickBaseTexture = texture; }

    /**
     * @brief Set custom texture for joystick stick
     * @param texture Pointer to texture (nullptr to use default)
     */
    void SetJoystickStickTexture(TextureAsset *texture) { m_joystickStickTexture = texture; }

    /**
     * @brief Set custom texture for a specific button
     * @param buttonIndex Index of the button (0-3)
     * @param texture Pointer to texture (nullptr to use default)
     */
    void SetButtonTexture(int buttonIndex, TextureAsset *texture);

    // === State Queries ===
    /**
     * @brief Get the joystick state
     */
    const JoystickState &GetJoystickState() const { return m_joystick; }

    /**
     * @brief Get the joystick direction vector (normalized)
     */
    vf2d GetJoystickDirection() const { return m_joystick.direction; }

    /**
     * @brief Get the joystick magnitude (0.0 to 1.0)
     */
    float GetJoystickMagnitude() const { return m_joystick.magnitude; }

    /**
     * @brief Check if a button is currently pressed
     * @param buttonIndex Index of the button (0-3)
     */
    bool IsButtonPressed(int buttonIndex) const;

    /**
     * @brief Check if a button was just pressed this frame
     * @param buttonIndex Index of the button (0-3)
     */
    bool IsButtonJustPressed(int buttonIndex) const;

    /**
     * @brief Check if a button was just released this frame
     * @param buttonIndex Index of the button (0-3)
     */
    bool IsButtonJustReleased(int buttonIndex) const;

private:
    bool m_enabled;
    JoystickMode m_joystickMode;

#ifdef LUMINOVEAU_WITH_IMGUI
    bool m_showDebugWindow;
#endif

    /**
     * @brief Convert centimeters to pixels based on platform DPI
     * @param wantedCM Desired size in centimeters
     * @return Size in pixels
     */
    float cm(float wantedCM) const;

    /**
     * @brief Convert pixels to centimeters based on platform DPI
     * @param pixels Size in pixels
     * @return Size in centimeters
     */
    float pixelsToCm(float pixels) const;

    // Joystick
    JoystickState m_joystick;
    vf2d m_joystickOffset;                  // Offset from bottom-left corner
    float m_joystickRadius;
    float m_joystickDeadZone;
    TextureAsset *m_joystickBaseTexture;
    TextureAsset *m_joystickStickTexture;

    // Buttons
    std::vector<VirtualButton> m_buttons;
    int m_buttonCount;
    vf2d m_buttonGroupOffset;  // Offset from bottom-right corner for button group

    // Default white circle texture
    TextureAsset *m_defaultTexture;

    // Helper methods
    void UpdateJoystick();
    void UpdateButtons();
    void UpdateMouse();
    void RenderJoystick();
    void RenderButtons();
    void InitializeDefaultTexture();
    void LayoutButtons();

    // Touch handling helpers
    bool IsTouchInJoystickArea(const vf2d &touchPos);
    int GetButtonAtPosition(const vf2d &position);
};
