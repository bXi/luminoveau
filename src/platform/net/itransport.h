#pragma once

// Internal swappable transport behind the typed Net:: API. One backend is active at a
// time (native SDL_net, or a web WebSocket backend later). net.cpp drives it; user code
// never sees this. Messages are opaque byte blobs ([typeId][payload], built by net.cpp).

#include <cstdint>
#include <string>
#include <vector>

#include "platform/net/net.h" // Net::Peer

/// @cond INTERNAL

struct TransportEvent {
    enum Type { Connect,
        Disconnect,
        Receive } type;
    Net::Peer            peer = 0;
    std::vector<uint8_t> data; // payload for Receive
    bool                 reliable = false;
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool Host(uint16_t port)                                = 0;
    virtual bool Connect(const std::string &address, uint16_t port) = 0;
    virtual void Disconnect()                                       = 0;

    virtual bool      IsServer() const           = 0;
    virtual bool      IsClient() const           = 0;
    virtual Net::Peer SelfId() const             = 0;
    virtual uint32_t  PeerCount() const          = 0;
    virtual uint32_t  Ping(Net::Peer peer) const = 0;

    virtual void Send(Net::Peer peer, const void *data, uint32_t size, bool reliable) = 0;
    virtual void Broadcast(const void *data, uint32_t size, bool reliable)            = 0;

    // Poll sockets; append connect/disconnect/receive events since the last call.
    virtual void Poll(std::vector<TransportEvent> &out) = 0;
};

// Implemented per backend (sdl/transport_sdlnet.cpp or webgpu/transport_web.cpp).
ITransport *createTransport();
/// @endcond
