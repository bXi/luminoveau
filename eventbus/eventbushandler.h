#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <functional>

class EventBus {
public:
    static void Register(std::string eventName, std::function<void()> callback) { get()._register(eventName, callback); }
    static void Fire(std::string eventName) { get()._fire(eventName); }


private:
    void _register(std::string eventName, std::function<void()> callback);
    void _fire(std::string eventName);

    std::unordered_map<std::string, std::vector<std::function<void()>>> _events;
public:
    EventBus(const EventBus&) = delete;
    static EventBus& get() { static EventBus instance; return instance; }
private:
    EventBus() = default;
};
