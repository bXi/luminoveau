#pragma once
#include "luminoveau.h"
#include "app/lumi.h"

Lumi::Result AppInit(void** appstate, int argc, char* argv[]);
Lumi::Result AppIterate(void* appstate);
Lumi::Result AppEvent(void* appstate, SDL_Event* event);
void AppQuit(void* appstate, Lumi::Result result);
