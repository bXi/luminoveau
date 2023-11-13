#pragma once

#include <random>
#include <iostream>
#include <map>

#include "audio/audiohandler.h"

#include "basestate.h"

class State {
public:
	static void Init() { get()._init(); }
    static void Draw() { get()._draw(); }
    static void Load() { get()._load(); }
    static void Unload() { get()._unload(); }
    static void AddState(const char* stateName, BaseState* state) { get()._addState(stateName, state); };
	static void SetState(const char* newState) { get()._setState(newState); }

private:
    BaseState* state = nullptr;
    const char* currentState = "";
    std::map<const char*, BaseState*> registeredStates;

	void _setState(const char* newState)
	{
		if (newState != currentState) {

            auto it = registeredStates.find(newState);

            if (it != registeredStates.end()) {
                state->unload();

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

    void _addState(const char* stateName, BaseState* statePtr) {

        auto it = registeredStates.find(stateName);

        if (it != registeredStates.end()) {
            std::cout << stateName << " has been added already." << std::endl;
        } else {
            registeredStates[stateName] = statePtr;
        }
    }

	void _init()
    {
        if (!registeredStates.empty()) {
            currentState = registeredStates.begin()->first;
            state = registeredStates.begin()->second;
            state->load();
        }
	}
public:
	State(const State&) = delete;
	static State& get() { static State instance; return instance; }

private:
	State() = default;
};
