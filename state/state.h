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

    void _init(std::string stateName);
    void _setState(std::string newState);

    void _draw();
    void _load();
    void _unload();
    void _addState(std::string stateName, BaseState* statePtr);

public:
	State(const State&) = delete;
	static State& get() { static State instance; return instance; }

private:
	State() = default;
};
