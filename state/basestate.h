#pragma once

/**
 * @brief Interface for defining base state functionality.
 *
 * Users can extend this class to create their own custom states by implementing the load(),
 * unload(), and draw() methods.
 */
class BaseState {
public:
    /**
     * @brief Loads the state.
     */
    virtual void load() = 0;

    /**
     * @brief Unloads the state.
     */
    virtual void unload() = 0;

    /**
     * @brief Draws the state.
     */
    virtual void draw() = 0;
};