#pragma once

class BaseState {
public:
    virtual void load() = 0;
    virtual void unload() = 0;
    virtual void draw() = 0;
};