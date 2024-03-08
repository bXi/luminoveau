#pragma once

#include <vector>
#include <functional>
#include <algorithm>

#include "easings.h"

/**
 * @brief Struct for linear interpolation (lerp) animation.
 */
struct LerpAnimator {
public:
    const char *name; /**< The name of the animator. */
    bool canDelete = false; /**< Flag indicating if the animator can be deleted. */
    bool shouldDelete = false; /**< Flag indicating if the animator should be deleted. */
    bool started = true; /**< Flag indicating if the animation has started. */
    float time; /**< Current time of the animation. */
    float startValue; /**< Starting value of the animation. */
    float change; /**< Change in value over the animation. */
    float duration; /**< Duration of the animation. */

    /**
     * @brief Callback function to compute the interpolated value.
     *
     * The default callback performs linear interpolation.
     */
    std::function<float(float time, float startValue, float change, float duration)> callback = [](float _time, float _startValue, float _change,
                                                                                                   float _duration) {
        return (_change * _time / _duration + _startValue);
    };

    /**
     * @brief Checks if the animation has finished.
     *
     * @return True if the animation has finished, false otherwise.
     */
    bool isFinished() const {
        return time >= duration;
    }

    /**
     * @brief Gets the current interpolated value of the animation.
     *
     * @return The current interpolated value.
     */
    float getValue() {
        // Compute interpolated value using the callback function
        float result = callback(time, startValue, change, duration);

        // Calculate the minimum and maximum possible values
        float minValue = std::min(startValue, startValue + change);
        float maxValue = std::max(startValue, startValue + change);

        // Clamp the result within the range [minValue, maxValue]
        return std::clamp(result, minValue, maxValue);
    }
};
/**
 * @brief Provides functionality for managing linear interpolation (lerp) animations.
 */
class Lerp {
public:
    /**
     * @brief Retrieves a lerp animator with the specified parameters or creates a new one if not found.
     *
     * @param name The name of the lerp animator.
     * @param startValue The starting value of the animation.
     * @param change The change in value over the animation.
     * @param duration The duration of the animation.
     * @return A pointer to the lerp animator.
     */
    static LerpAnimator *getLerp(const char *name, float startValue, float change, float duration) {
        return get()._getLerp(name, startValue, change, duration);
    }

    /**
     * @brief Retrieves a lerp animator by name.
     *
     * @param name The name of the lerp animator.
     * @return A pointer to the lerp animator, or nullptr if not found.
     */
    static LerpAnimator *getLerp(const char *name) {
        return get()._getLerp(name);
    }

    /**
     * @brief Resets the time of the lerp animator with the specified name.
     *
     * @param name The name of the lerp animator.
     */
    static void resetTime(const char *name) {
        get()._resetTime(name);
    }

    /**
     * @brief Updates all active lerps.
     */
    static void updateLerps() {
        get()._updateLerps();
    }

private:
    std::unordered_map<const char *, LerpAnimator *> lerpList;
    LerpAnimator *tempLerp = nullptr;

    LerpAnimator *_getLerp(const char *name, float startValue, float change, float duration);

    LerpAnimator *_getLerp(const char *name);

    void _resetTime(const char *name);

    void _updateLerps();

public:
    Lerp(const Lerp &) = delete;

    static Lerp &get() {
        static Lerp instance;
        return instance;
    }

private:
    Lerp() {}
};

