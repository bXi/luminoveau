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

/**
 * @brief Provides functionality for event registration and firing.
 */
class EventBus {
public:
    /**
     * @brief Registers a callback function for the specified event name.
     *
     * @param eventName The name of the event.
     * @param callback The callback function to be registered.
     */
    static void Register(std::string eventName, EventCallback callback) {
        get()._register(eventName, callback);
    }

    /**
     * @brief Registers a callback function with data for the specified event name.
     *
     * @param eventName The name of the event.
     * @param callback The callback function with data to be registered.
     */
    static void Register(std::string eventName, EventCallbackData callback) {
        get()._register(eventName, callback);
    }

    /**
     * @brief Fires an event with no associated data.
     *
     * @param eventName The name of the event to fire.
     */
    static void Fire(std::string eventName) {
        get()._fire(eventName, std::nullopt);
    }

    /**
     * @brief Fires an event with associated data.
     *
     * @param eventName The name of the event to fire.
     * @param eventData The data associated with the event.
     */
    static void Fire(std::string eventName, EventData eventData) {
        get()._fire(eventName, eventData);
    }

    /**
     * @brief Registers a callback function with data for a system event.
     *
     * @param eventName The system event to register for.
     * @param callback The callback function with data to be registered.
     */
    static void Register(SystemEvent eventName, EventCallbackData callback) {
        get()._register(eventName, callback);
    }

    /**
     * @brief Fires a system event with associated data.
     *
     * @param eventName The system event to fire.
     * @param eventData The data associated with the event.
     */
    static void Fire(SystemEvent eventName, EventData eventData) {
        get()._fire(eventName, eventData);
    }
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
