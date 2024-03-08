#pragma once

#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <variant>
#include <optional>

enum class SystemEvent {
    GAMEPAD_CONNECTED,
    GAMEPAD_DISCONNECTED,

    WINDOW_RESIZE,
    WINDOW_FULLSCREEN,
};

using EventData = std::unordered_map<std::string, std::variant<int, float, std::string>>;

using EventCallback = std::function<void()>;
using EventCallbackData = std::function<void(EventData)>;

class EventBus {
public:
    static void Register(std::string eventName, EventCallback callback) { get()._register(eventName, callback); }

    static void Register(std::string eventName, EventCallbackData callback) { get()._register(eventName, callback); }

    static void Fire(std::string eventName) { get()._fire(eventName, std::nullopt); }

    static void Fire(std::string eventName, EventData eventData) { get()._fire(eventName, eventData); }

    static void Register(SystemEvent eventName, EventCallbackData callback) { get()._register(eventName, callback); }

    static void Fire(SystemEvent eventName, EventData eventData) { get()._fire(eventName, eventData); }

private:
    void _fire(std::string eventName, std::optional<EventData> eventData);

    void _register(std::string eventName, EventCallback callback);

    void _register(std::string eventName, EventCallbackData callback);

    void _register(SystemEvent eventName, EventCallbackData callback);

    void _fire(SystemEvent eventName, EventData eventData);

    std::unordered_map<std::string, std::vector<EventCallback>> _events;
    std::unordered_map<std::string, std::vector<EventCallbackData>> _eventsData;
    std::unordered_map<SystemEvent, std::vector<EventCallbackData>> _systemEvents;
public:
    EventBus(const EventBus &) = delete;

    static EventBus &get() {
        static EventBus instance;
        return instance;
    }

private:
    EventBus() = default;
};
