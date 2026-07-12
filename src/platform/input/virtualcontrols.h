#pragma once

#include "SDL3/SDL.h"
#include "math/vectors.h"
#include "assets/texture/texture.h"
#include <vector>
#include <string>

/**
 * @brief Manages virtual onscreen controls for touch devices
 * 
 * Provides joystick and button controls that can be rendered on screen
 * and respond to touch input. Useful for mobile devices and tablets.
 */
class VirtualControls {
public:
    /// @brief How the onscreen joystick is positioned and shown.
    enum class JoystickMode {
        DISABLED,   ///< No joystick shown.
        STATIC,     ///< Joystick fixed at a set position.
        RELATIVE    ///< Joystick appears where you first touch.
    };

    /// @brief One onscreen touch button (state, layout and optional custom texture).
    struct VirtualButton {
        vf2d individualOffset;          ///< Offset from the anchor point, for this button's layout.
        float radius;                   ///< Button radius in pixels.
        bool isPressed;                 ///< True while the button is currently held.
        bool wasPressed;                ///< Press state from the previous frame (for edge detection).
        SDL_FingerID activeFinger;      ///< Finger currently pressing this button, if any.
        TextureAsset *customTexture;    ///< Custom texture, or nullptr to use the default.
        std::string label;              ///< Optional text label drawn centered on the button.

        /**
         * @brief Get the actual screen position of this button
         * @param anchorOffset The anchor point offset (base position + group offset)
         */
        vf2d GetScreenPosition(const vf2d& anchorOffset) const;
    };

    /// @cond INTERNAL
    struct JoystickState {
        vf2d direction;                 // Normalized direction vector
        float magnitude;                // 0.0 to 1.0
        vf2d touchStart;                // Where touch began
        vf2d touchCurrent;              // Current touch position
        SDL_FingerID activeFinger;      // Which finger is controlling
        bool isActive;
    };
    /// @endcond

    /// @cond INTERNAL
    VirtualControls();
    ~VirtualControls();
    /// @endcond

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

    /**
     * @brief Set a text label drawn centered on a button (default font).
     * @param buttonIndex Index of the button (0-3)
     * @param label Text to show ("" to clear)
     */
    void SetButtonLabel(int buttonIndex, const std::string &label);

    // === Coordinate space ===
    /**
     * @brief Draw and hit-test in physical (device) pixels instead of logical.
     *
     * VirtualControls defaults to logical coordinates (Window::GetWidth/Height).
     * If the app renders its 2D scene in physical pixels (e.g. a swapchain-sized
     * canvas on a HiDPI display), enable this so the controls render and hit-test
     * in the same space and stay anchored to the real screen corners.
     * @param usePhysical True → physical pixels; false (default) → logical.
     */
    void SetUsePhysicalCoords(bool usePhysical) { m_usePhysicalCoords = usePhysical; }

    /**
     * @brief Uniformly scale the on-screen controls (joystick + buttons + offsets).
     *
     * The default cm-based sizes suit tablets; on a phone (especially portrait)
     * they can be too large. This multiplies every size and layout offset, so e.g.
     * 0.5 makes the whole control set half as big and tighter into the corners.
     * @param scale >0; 1.0 = default. Recomputes the layout immediately.
     */
    void SetControlScale(float scale);

    /**
     * @brief Size the controls as a fraction of the viewport, ignoring DPI.
     *
     * cm-based sizing depends on the reported display scale, which is unreliable
     * on some backends (e.g. WebGPU on mobile Safari reports a huge value, making
     * the controls cover the screen). This sizes the joystick radius to
     * @p joystickFraction * min(viewW, viewH) and derives everything else from the
     * default cm ratios, so the controls are always a sensible fraction of the
     * screen. Recomputed automatically when the viewport size changes (rotation).
     * @param joystickFraction e.g. 0.10; <=0 disables (back to cm/SetControlScale).
     */
    void SetControlScaleToViewport(float joystickFraction);

    /**
     * @brief Enable/disable driving the controls with the mouse (desktop testing).
     *
     * On a real touch device the browser also emits synthetic mouse events that
     * mirror each finger, which would double-drive the look region and joystick.
     * Disable this when genuine touch input is present. Default: enabled.
     */
    void SetMouseEmulationEnabled(bool enabled) { m_mouseEmulation = enabled; }

    // === Look region (right-side drag → camera delta) ===
    /**
     * @brief Enable a look/drag region: dragging in the right half of the screen
     *        (outside any button) produces a per-frame delta, like a second stick
     *        for camera turn/pitch. Works alongside the left joystick and buttons.
     * @param enabled True to enable.
     */
    void SetLookRegionEnabled(bool enabled) { m_lookEnabled = enabled; }

    /**
     * @brief Consume the look-drag delta accumulated since the last call.
     * @return Movement delta (dx, dy) in the active coordinate space; resets to 0.
     */
    vf2d ConsumeLookDelta();

    /**
     * @brief Whether a finger/mouse is currently dragging in the look region.
     */
    bool IsLookActive() const { return m_lookActive; }

    /**
     * @brief Consume a "tap" in the look region: a press+release that barely moved
     *        (as opposed to a drag/swipe). Useful for tap-to-fire while the same
     *        region also does drag-to-look. Returns true once per tap, then clears.
     */
    bool ConsumeLookTap();

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

    // Coordinate space: logical (default) or physical (device) pixels.
    bool m_usePhysicalCoords = false;

    // Uniform size multiplier for all controls (1.0 = default cm sizing).
    float m_controlScale = 1.0f;
    // Viewport-relative sizing: joystick radius as a fraction of min(view) when >0.
    float m_relJoystickFrac = 0.0f;
    float m_lastViewW = 0.0f, m_lastViewH = 0.0f;
    void ApplyViewportScale();   ///< Recompute m_controlScale from the viewport.
    // Drive controls with the mouse (desktop test). Off on real touch devices.
    bool m_mouseEmulation = true;
    float viewW() const;   ///< Active-space window width (logical or physical).
    float viewH() const;   ///< Active-space window height.

    // Look region (right-side drag → accumulated camera delta).
    bool m_lookEnabled = false;
    bool m_lookActive = false;
    SDL_FingerID m_lookFinger = static_cast<SDL_FingerID>(-1);
    vf2d m_lookLast{0.0f, 0.0f};
    vf2d m_lookAccum{0.0f, 0.0f};
    vf2d m_lookStart{0.0f, 0.0f};    ///< Where the current look touch began.
    float m_lookMaxDist2 = 0.0f;     ///< Max squared travel from start (tap vs drag).
    bool m_lookTap = false;          ///< A tap (barely-moved press+release) is pending.
    bool IsInLookRegion(const vf2d &pos) const;   ///< Right half, and look enabled.

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
