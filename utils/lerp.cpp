#include "lerp.h"

#include "window/windowhandler.h"

LerpAnimator* Lerp::_getLerp(const char* name, float startValue, float change, float duration)
{

	if (lerpList.find(name) == lerpList.end()) {
		tempLerp = new LerpAnimator();
		tempLerp->time = 0.0f;
		tempLerp->startValue = startValue;
		tempLerp->change = change;
		tempLerp->duration = duration;
		lerpList.try_emplace(name, tempLerp);
		tempLerp = nullptr;
	}
	return lerpList[name];
}

LerpAnimator* Lerp::_getLerp(const char* name)
{
    if (lerpList.contains(name)) {
        return lerpList[name];
    }

    return nullptr;
}

void Lerp::_resetTime(const char* name)
{
    if (lerpList.contains(name))
	    lerpList[name]->time = 0.0f;
}
void Lerp::_updateLerps()
{
	for (auto& lerp : lerpList) {
		if (lerp.second->started) {
			if (lerp.second->isFinished())
				lerp.second->canDelete = true;
			else {
                lerp.second->time += (float)Window::GetFrameTime();
            }
		}
	}
}
