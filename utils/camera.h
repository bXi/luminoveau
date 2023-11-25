#pragma once

#include <stdexcept> // For std::logic_error
#include "vectors.h" // Include the file with the vf2d definition
#include "window/windowhandler.h"

class Camera {
public:

    static vf2d ToScreenSpace(const vf2d& worldSpace) {

        return vf2d((worldSpace.x - get().Target.x) * get().Scale, (worldSpace.y - get().Target.y) * get().Scale) + (Window::GetSize() / 2.f);
    }

    static vf2d ToWorldSpace(const vf2d& screenSpace) {

        vf2d translatedScreenSpace = screenSpace - (Window::GetSize() / 2.f);
        return vf2d(translatedScreenSpace.x / get().Scale + get().Target.x, translatedScreenSpace.y / get().Scale + get().Target.y);
    }

    static void Lock() {
        get().Locked = true;
        get().LockTarget = get().Target;
        get().LockScale = get().Scale;
    }

    static void Unlock() {
        get().Locked = false;
        get().Moved = false; // Reset the Moved flag when unlocking the camera
    }

    static bool IsLocked() { return get().Locked; }
    static bool HasMoved() { return get().Moved; }
    static void Activate() { get().Active = true; }
    static void Deactivate() { get().Active = false; }
    static bool IsActive() { return get().Active; }

    static float GetScale() { return get().Scale; }
    static void SetScale(float newScale) { get().Scale = newScale; }

    static vf2d GetTarget() { return get().Target; }
    static void SetTarget(const vf2d& newTarget) {
        if (get().Locked) {
            throw std::logic_error("Attempt to update camera target while locked.");
        } else {
            get().Target = newTarget;
            get().Moved = true; // Set the Moved flag when the camera is updated
        }
    }
private:
    vf2d Target;
    float Scale = 1.0f; // Zoom level
    bool Locked;  // Indicates whether the camera is locked for the current frame
    vf2d LockTarget;  // The locked position when the camera is locked
    float LockScale;  // The locked scale when the camera is locked
    bool Moved;  // Flag to indicate if the camera has been moved
    bool Active;  // Flag to indicate if the camera is active (applied during rendering)
public:
	Camera(const Camera&) = delete;
	static Camera& get() { static Camera instance; return instance; }

private:
	Camera() = default;
};
