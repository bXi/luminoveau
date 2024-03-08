#include "eventbushandler.h"

void EventBus::_fire(std::string eventName, std::optional<EventData> eventData) {
    bool eventFound = false;

    if (eventData.has_value()) {
        auto datait = _eventsData.find(eventName);
        if (datait != _eventsData.end()) {
            for (const auto &callback: datait->second) {
                callback(eventData.value());
            }
            eventFound = true;
        }
    } else {
        auto it = _events.find(eventName);
        if (it != _events.end()) {
            for (const auto &callback: it->second) {
                callback();
            }
            eventFound = true;
        }
    }

    if (!eventFound) {
        std::cout << "Event '" << eventName << "' not registered." << std::endl;
    }
}

void EventBus::_register(std::string eventName, EventCallback callback) {
    _events[eventName].push_back(callback);
}

void EventBus::_register(std::string eventName, EventCallbackData callback) {
    _eventsData[eventName].push_back(callback);
}

void EventBus::_register(SystemEvent eventName, EventCallbackData callback) {
    _systemEvents[eventName].push_back(callback);
}

void EventBus::_fire(SystemEvent eventName, EventData eventData) {
    for (auto &callback: _systemEvents[eventName]) {
        callback(eventData);
    }
}
