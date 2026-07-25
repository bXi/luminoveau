// Net — transport-agnostic core: lifecycle, typed-message framing + dispatch. The actual
// sockets live behind ITransport (see sdl/ and webgpu/). Wire format for a typed message is
// [uint32 typeId][payload bytes]; net.cpp adds/peels the prefix and routes to handlers.

#include "platform/net/net.h"
#include "platform/net/itransport.h"
#include "core/log/log.h"

#include <unordered_map>
#include <vector>

bool Net::_ensureTransport() {
    if (!_transport)
        _transport = createTransport();
    return _transport != nullptr;
}

bool Net::_init() {
    if (!_ensureTransport()) {
        LOG_WARNING("Net::Init — no transport backend available");
        return false;
    }
    return true;
}

void Net::_shutdown() {
    if (_transport) {
        _transport->Disconnect();
        delete _transport;
        _transport = nullptr;
    }
    _handlers.clear();
}

bool Net::_host(uint16_t port) {
    return _ensureTransport() && _transport->Host(port);
}

bool Net::_connect(const std::string &address, uint16_t port) {
    return _ensureTransport() && _transport->Connect(address, port);
}

void Net::_disconnect() {
    if (_transport)
        _transport->Disconnect();
}

void Net::_update() {
    if (!_transport)
        return;
    static std::vector<TransportEvent> events;
    events.clear();
    _transport->Poll(events);
    for (const TransportEvent &e : events) {
        if (e.type != TransportEvent::Receive)
            continue;
        if (e.data.size() < sizeof(uint32_t))
            continue;
        uint32_t typeId;
        std::memcpy(&typeId, e.data.data(), sizeof(uint32_t));
        auto it = _handlers.find(typeId);
        if (it == _handlers.end())
            continue;
        it->second(e.peer, e.data.data() + sizeof(uint32_t),
            (uint32_t)(e.data.size() - sizeof(uint32_t)));
    }
}

bool      Net::_isServer() { return _transport && _transport->IsServer(); }
bool      Net::_isClient() { return _transport && _transport->IsClient(); }
Net::Peer Net::_getClientID() { return _transport ? _transport->SelfId() : 0; }
uint32_t  Net::_getPeerCount() { return _transport ? _transport->PeerCount() : 0; }
uint32_t  Net::_getPing(Peer peer) { return _transport ? _transport->Ping(peer) : 0; }

// Build [typeId][payload] once, reused by send + broadcast.
static std::vector<uint8_t> frame(uint32_t typeId, const void *data, uint32_t size) {
    std::vector<uint8_t> buf(sizeof(uint32_t) + size);
    std::memcpy(buf.data(), &typeId, sizeof(uint32_t));
    if (size)
        std::memcpy(buf.data() + sizeof(uint32_t), data, size);
    return buf;
}

void Net::_sendRaw(Peer peer, uint32_t typeId, const void *data, uint32_t size, bool reliable) {
    if (!_transport)
        return;
    auto buf = frame(typeId, data, size);
    _transport->Send(peer, buf.data(), (uint32_t)buf.size(), reliable);
}

void Net::_broadcastRaw(uint32_t typeId, const void *data, uint32_t size, bool reliable) {
    if (!_transport)
        return;
    auto buf = frame(typeId, data, size);
    _transport->Broadcast(buf.data(), (uint32_t)buf.size(), reliable);
}

void Net::_registerRaw(uint32_t typeId, std::function<void(Peer, const void *, uint32_t)> handler) {
    _handlers[typeId] = std::move(handler);
}
