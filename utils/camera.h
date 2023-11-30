#pragma once

#include <stdexcept>
#include "vectors.h"
#include "window/windowhandler.h"

class Camera {
public:

    static vf2d ToScreenSpace(const vf2d& worldSpace) {

        return vf2d((worldSpace.x - get().Target.x) * get().Scale, (worldSpace.y - get().Target.y) * get().Scale) + (Window::GetSize() / 2.f);
    }

    static vf2d ToWorldSpace(const vf2d& screenSpace) {

        vf2d translatedScreenSpace = screenSpace - (Window::GetSize() / 2.f);
        return {translatedScreenSpace.x / get().Scale + get().Target.x, translatedScreenSpace.y / get().Scale + get().Target.y};
    }

    static void Lock() {
        get().Locked = true;
        get().LockTarget = get().Target;
        get().LockScale = get().Scale;
    }

    static void Unlock() {
        get().Locked = false;
        get().Moved = false;
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
            get().Moved = true;
        }
    }
private:
    vf2d Target = { 0.f, 0.f };
    float Scale = 1.0f;
    bool Locked = false;
    vf2d LockTarget = Target;
    float LockScale = Scale;
    bool Moved = false;
    bool Active = false;
public:
	Camera(const Camera&) = delete;
	static Camera& get() { static Camera instance; return instance; }

private:
	Camera() = default;
};
