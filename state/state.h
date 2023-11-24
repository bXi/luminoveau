#pragma once

#include <random>
#include <iostream>
#include <map>
#include <string>

#include "audio/audiohandler.h"

#include "basestate.h"

class State {
public:
	static void Init(std::string stateName) { get()._init(stateName); }
    static void Draw() { get()._draw(); }
    static void Load() { get()._load(); }
    static void Unload() { get()._unload(); }
    static void AddState(std::string stateName, BaseState* state) { get()._addState(stateName, state); };
	static void SetState(std::string newState) { get()._setState(newState); }

private:
    BaseState* state = nullptr;
    std::string currentState = "";
std::map<std::string, BaseState*> registeredStates;

    void _setState(std::string newState)
    {
	    if (newState != currentState) {

            auto it = registeredStates.find(newState);

            if (it != registeredStates.end()) {
                if (state) state->unload();

                currentState = newState;
                state = registeredStates[currentState];

                state->load();
            } else {
                std::cout << newState << " is not in the map." << std::endl;
            }
	    }
    }

    void _draw() { state->draw(); }
    void _load() { state->load(); }
    void _unload() { state->unload(); }

    void _addState(std::string stateName, BaseState* statePtr) {

        auto it = registeredStates.find(stateName);

        if (it != registeredStates.end()) {
            std::cout << stateName << " has been added already." << std::endl;
        } else {
            registeredStates[stateName] = statePtr;
        }
    }

	void _init(std::string stateName)
    {
        if (!registeredStates.empty()) {
            _setState(stateName);
        }
	}
public:
	State(const State&) = delete;
	static State& get() { static State instance; return instance; }

private:
	State() = default;
};
