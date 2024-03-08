#pragma once

#include <stdexcept>
#include "vectors.h"
#include "window/windowhandler.h"

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
        return vf2d((worldSpace.x - get().Target.x) * get().Scale, (worldSpace.y - get().Target.y) * get().Scale) + (Window::GetSize() / 2.f);
    }

    /**
     * @brief Converts a screen space position to world space.
     *
     * @param screenSpace The position in screen space.
     * @return The position in world space.
     */
    static vf2d ToWorldSpace(const vf2d &screenSpace) {
        vf2d translatedScreenSpace = screenSpace - (Window::GetSize() / 2.f);
        return {translatedScreenSpace.x / get().Scale + get().Target.x, translatedScreenSpace.y / get().Scale + get().Target.y};
    }

    /**
     * @brief Locks the camera's position and scale.
     */
    static void Lock() {
        get().Locked = true;
        get().LockTarget = get().Target;
        get().LockScale = get().Scale;
    }

    /**
     * @brief Unlocks the camera, allowing it to move freely.
     */
    static void Unlock() {
        get().Locked = false;
        get().Moved = false;
    }

    /**
     * @brief Checks if the camera is locked.
     *
     * @return True if the camera is locked, false otherwise.
     */
    static bool IsLocked() { return get().Locked; }

    /**
     * @brief Checks if the camera has moved.
     *
     * @return True if the camera has moved, false otherwise.
     */
    static bool HasMoved() { return get().Moved; }

    /**
     * @brief Activates the camera.
     */
    static void Activate() { get().Active = true; }

    /**
     * @brief Deactivates the camera.
     */
    static void Deactivate() { get().Active = false; }

    /**
     * @brief Checks if the camera is active.
     *
     * @return True if the camera is active, false otherwise.
     */
    static bool IsActive() { return get().Active; }

    /**
     * @brief Gets the current scale of the camera.
     *
     * @return The current scale of the camera.
     */
    static float GetScale() { return get().Scale; }

    /**
     * @brief Sets the scale of the camera.
     *
     * @param newScale The new scale value.
     */
    static void SetScale(float newScale) { get().Scale = newScale; }

    /**
     * @brief Gets the target position of the camera.
     *
     * @return The target position of the camera.
     */
    static vf2d GetTarget() { return get().Target; }

    /**
     * @brief Sets the target position of the camera.
     *
     * @param newTarget The new target position.
     * @throw std::logic_error if the camera is locked.
     */
    static void SetTarget(const vf2d &newTarget) {
        if (get().Locked) {
            throw std::logic_error("Attempt to update camera target while locked.");
        } else {
            get().Target = newTarget;
            get().Moved = true;
        }
    }

private:
    vf2d Target = {0.f, 0.f};
    float Scale = 1.0f;
    bool Locked = false;
    vf2d LockTarget = Target;
    float LockScale = Scale;
    bool Moved = false;
    bool Active = false;
public:
    Camera(const Camera &) = delete;

    static Camera &get() {
        static Camera instance;
        return instance;
    }

private:
    Camera() = default;
};
