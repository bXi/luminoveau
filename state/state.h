#pragma once

#include <random>
#include <iostream>
#include <map>
#include <string>

#include "audio/audiohandler.h"

#include "basestate.h"

/**
 * @brief Manages the states of the application.
 */
class State {
public:
    /**
     * @brief Initializes the state manager with the specified state name.
     *
     * @param stateName The name of the initial state.
     */
    static void Init(std::string stateName) { get()._init(stateName); }

    /**
     * @brief Draws the current state.
     */
    static void Draw() { get()._draw(); }

    /**
     * @brief Loads the current state.
     */
    static void Load() { get()._load(); }

    /**
     * @brief Unloads the current state.
     */
    static void Unload() { get()._unload(); }

    /**
     * @brief Adds a new state to the state manager.
     *
     * @param stateName The name of the state to add.
     * @param state A pointer to the BaseState object representing the new state.
     */
    static void AddState(std::string stateName, BaseState *state) { get()._addState(stateName, state); };

    /**
     * @brief Sets the current state to the one with the specified name.
     *
     * @param newState The name of the state to set.
     */
    static void SetState(std::string newState) { get()._setState(newState); }

private:
    BaseState *state = nullptr;
    std::string currentState = "";
    std::map<std::string, BaseState *> registeredStates;

    void _init(std::string stateName);

    void _setState(std::string newState);

    void _draw();

    void _load();

    void _unload();

    void _addState(std::string stateName, BaseState *statePtr);

public:
    State(const State &) = delete;

    static State &get() {
        static State instance;
        return instance;
    }

private:
    State() = default;
};
