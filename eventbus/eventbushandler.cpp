#include "eventbushandler.h"

void EventBus::_register(std::string eventName, std::function<void()> callback) {
    _events[eventName].push_back(callback);
}

void EventBus::_fire(std::string eventName) {
    for (auto& callback : _events[eventName]) {
        callback();
    }
}
