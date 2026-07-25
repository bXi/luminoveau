#pragma once

#include <stdexcept>
#include "math/vectors.h"
#include "platform/window/window.h"

/**
 * @brief Provides functionality for managing the camera view.
 */
class Camera {
public:
    /**
     * @brief Converts a world space position to screen space.
     *
     * @param worldSpace The position in world space.
     * @return The position in screen space.
     */
    static vf2d ToScreenSpace(const vf2d &worldSpace) {
        return vf2d((worldSpace.x - Get()._target.x) * Get()._scale, (worldSpace.y - Get()._target.y) * Get()._scale) + (Window::GetSize() / 2.f);
    }

    /**
     * @brief Converts a screen space position to world space.
     *
     * @param screenSpace The position in screen space.
     * @return The position in world space.
     */
    static vf2d ToWorldSpace(const vf2d &screenSpace) {
        vf2d translatedScreenSpace = screenSpace - (Window::GetSize() / 2.f);
        return { translatedScreenSpace.x / Get()._scale + Get()._target.x, translatedScreenSpace.y / Get()._scale + Get()._target.y };
    }

    /**
     * @brief Locks the camera's position and scale.
     */
    static void Lock() {
        Get()._locked     = true;
        Get()._lockTarget = Get()._target;
        Get()._lockScale  = Get()._scale;
    }

    /**
     * @brief Unlocks the camera, allowing it to move freely.
     */
    static void Unlock() {
        Get()._locked = false;
        Get()._moved  = false;
    }

    /**
     * @brief Checks if the camera is locked.
     *
     * @return True if the camera is locked, false otherwise.
     */
    static bool IsLocked() { return Get()._locked; }

    /**
     * @brief Checks if the camera has moved.
     *
     * @return True if the camera has moved, false otherwise.
     */
    static bool HasMoved() { return Get()._moved; }

    /**
     * @brief Activates the camera.
     */
    static void Activate() { Get()._active = true; }

    /**
     * @brief Deactivates the camera.
     */
    static void Deactivate() { Get()._active = false; }

    /**
     * @brief Checks if the camera is active.
     *
     * @return True if the camera is active, false otherwise.
     */
    static bool IsActive() { return Get()._active; }

    /**
     * @brief Gets the current scale of the camera.
     *
     * @return The current scale of the camera.
     */
    static float GetScale() { return Get()._scale; }

    /**
     * @brief Sets the scale of the camera.
     *
     * @param newScale The new scale value.
     */
    static void SetScale(float newScale) { Get()._scale = newScale; }

    /**
     * @brief Gets the target position of the camera.
     *
     * @return The target position of the camera.
     */
    static vf2d GetTarget() { return Get()._target; }

    /**
     * @brief Sets the target position of the camera.
     *
     * @param newTarget The new target position.
     * @throw std::logic_error if the camera is locked.
     */
    static void SetTarget(const vf2d &newTarget) {
        if (Get()._locked) {
            throw std::logic_error("Attempt to update camera target while locked.");
        } else {
            Get()._target = newTarget;
            Get()._moved  = true;
        }
    }

private:
    vf2d  _target     = { 0.f, 0.f };
    float _scale      = 1.0f;
    bool  _locked     = false;
    vf2d  _lockTarget = _target;
    float _lockScale  = _scale;
    bool  _moved      = false;
    bool  _active     = false;

public:
    /// @cond INTERNAL
    Camera(const Camera &) = delete;

    static Camera &Get() {
        static Camera instance;
        return instance;
    }
    /// @endcond

private:
    Camera() = default;
};
