// Net — transport-agnostic core: lifecycle, typed-message framing + dispatch. The actual
// sockets live behind ITransport (see sdl/ and webgpu/). Wire format for a typed message is
// [uint32 typeId][payload bytes]; net.cpp adds/peels the prefix and routes to handlers.

#include "platform/net/net.h"
#include "platform/net/itransport.h"
#include "core/log/log.h"

#include <unordered_map>
#include <vector>

namespace Net {

namespace {
    ITransport*                                                          s_transport = nullptr;
    std::unordered_map<uint32_t, std::function<void(Peer, const void*, uint32_t)>> s_handlers;
    std::vector<TransportEvent>                                         s_events;

    bool ensureTransport() {
        if (!s_transport) s_transport = createTransport();
        return s_transport != nullptr;
    }
}

bool Init() {
    if (!ensureTransport()) {
        LOG_WARNING("Net::Init — no transport backend available");
        return false;
    }
    return true;
}

void Shutdown() {
    if (s_transport) {
        s_transport->disconnect();
        delete s_transport;
        s_transport = nullptr;
    }
    s_handlers.clear();
    s_events.clear();
}

bool Host(uint16_t port) {
    return ensureTransport() && s_transport->host(port);
}

bool Connect(const std::string& address, uint16_t port) {
    return ensureTransport() && s_transport->connect(address, port);
}

void Disconnect() {
    if (s_transport) s_transport->disconnect();
}

void Update() {
    if (!s_transport) return;
    s_events.clear();
    s_transport->poll(s_events);
    for (const TransportEvent& e : s_events) {
        if (e.type != TransportEvent::Receive) continue;
        if (e.data.size() < sizeof(uint32_t)) continue;
        uint32_t typeId;
        std::memcpy(&typeId, e.data.data(), sizeof(uint32_t));
        auto it = s_handlers.find(typeId);
        if (it == s_handlers.end()) continue;
        it->second(e.peer, e.data.data() + sizeof(uint32_t),
                   (uint32_t)(e.data.size() - sizeof(uint32_t)));
    }
}

bool     IsServer()          { return s_transport && s_transport->isServer(); }
bool     IsClient()          { return s_transport && s_transport->isClient(); }
Peer     GetClientID()       { return s_transport ? s_transport->selfId() : 0; }
uint32_t GetPeerCount()      { return s_transport ? s_transport->peerCount() : 0; }
uint32_t GetPing(Peer peer)  { return s_transport ? s_transport->ping(peer) : 0; }

namespace detail {

// Build [typeId][payload] once, reused by send + broadcast.
static std::vector<uint8_t> frame(uint32_t typeId, const void* data, uint32_t size) {
    std::vector<uint8_t> buf(sizeof(uint32_t) + size);
    std::memcpy(buf.data(), &typeId, sizeof(uint32_t));
    if (size) std::memcpy(buf.data() + sizeof(uint32_t), data, size);
    return buf;
}

void sendRaw(Peer peer, uint32_t typeId, const void* data, uint32_t size, bool reliable) {
    if (!s_transport) return;
    auto buf = frame(typeId, data, size);
    s_transport->send(peer, buf.data(), (uint32_t)buf.size(), reliable);
}

void broadcastRaw(uint32_t typeId, const void* data, uint32_t size, bool reliable) {
    if (!s_transport) return;
    auto buf = frame(typeId, data, size);
    s_transport->broadcast(buf.data(), (uint32_t)buf.size(), reliable);
}

void registerRaw(uint32_t typeId, std::function<void(Peer, const void*, uint32_t)> handler) {
    s_handlers[typeId] = std::move(handler);
}

} // namespace detail
} // namespace Net
