#include "state.h"

void State::_init(std::string stateName) {
    if (!registeredStates.empty()) {
        _setState(stateName);
    }
}

void State::_setState(std::string newState) {
    if (newState != currentState) {

        auto it = registeredStates.find(newState);

        if (it != registeredStates.end()) {
            if (state) state->unload();

            currentState = newState;
            state        = registeredStates[currentState];

            state->load();
        } else {
            std::cout << newState << " is not in the map." << std::endl;
        }
    }
}

void State::_addState(std::string stateName, BaseState *statePtr) {

    auto it = registeredStates.find(stateName);

    if (it != registeredStates.end()) {
        std::cout << stateName << " has been added already." << std::endl;
    } else {
        registeredStates[stateName] = statePtr;
    }
}

void State::_draw() {
    state->draw();
}

void State::_load() {
    state->load();
}

void State::_unload() {
    state->unload();
}
