#include "util/lerp.h"

#include "platform/window/window.h"

LerpAnimator *Lerp::_getLerp(const char *name, float startValue, float change, float duration) {

    if (_lerpList.find(name) == _lerpList.end()) {
        _tempLerp             = new LerpAnimator();
        _tempLerp->time       = 0.0f;
        _tempLerp->startValue = startValue;
        _tempLerp->change     = change;
        _tempLerp->duration   = duration;
        _lerpList.try_emplace(name, _tempLerp);
        _tempLerp = nullptr;
    }
    return _lerpList[name];
}

LerpAnimator *Lerp::_getLerp(const char *name) {
    if (_lerpList.contains(name)) {
        return _lerpList[name];
    }

    return nullptr;
}

void Lerp::_resetTime(const char *name) {
    if (_lerpList.contains(name))
        _lerpList[name]->time = 0.0f;
}

void Lerp::_updateLerps() {
    for (auto &lerp : _lerpList) {
        if (lerp.second->started) {
            if (lerp.second->IsFinished())
                lerp.second->canDelete = true;
            else {
                lerp.second->time += (float)Window::GetFrameTime();
            }
        }
    }
}
