#pragma once

#include <vector>
#include <functional>
#include <algorithm>

#include "easings.h"

struct LerpAnimator {
private:

public:
    const char *name;
    bool canDelete = false;
    bool shouldDelete = false;

    bool started = true;

    float time;
    float startValue;
    float change;
    float duration;


    std::function<float(float time, float startValue, float change, float duration)> callback = [](float _time, float _startValue, float _change,
                                                                                                   float _duration) {
        return (_change * _time / _duration + _startValue);
    };

    bool isFinished() const {
        return time >= duration;
    }

    float getValue() {
        float result = callback(time, startValue, change, duration);

        float minValue = std::min(startValue, startValue + change);
        float maxValue = std::max(startValue, startValue + change);


        return std::clamp(result, minValue, maxValue);
    }

};

class Lerp {
public:


    static LerpAnimator *getLerp(const char *name, float startValue, float change, float duration) {
        return get()._getLerp(name, startValue, change, duration);
    }

    static LerpAnimator *getLerp(const char *name) {
        return get()._getLerp(name);
    }

    static void resetTime(const char *name) {
        get()._resetTime(name);

    }

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

