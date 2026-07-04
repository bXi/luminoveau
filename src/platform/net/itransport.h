#pragma once

// Internal swappable transport behind the typed Net:: API. One backend is active at a
// time (native SDL_net, or a web WebSocket backend later). net.cpp drives it; user code
// never sees this. Messages are opaque byte blobs ([typeId][payload], built by net.cpp).

#include <cstdint>
#include <string>
#include <vector>

#include "platform/net/net.h"   // Net::Peer

namespace Net {

/// @cond INTERNAL

struct TransportEvent {
    enum Type { Connect, Disconnect, Receive } type;
    Peer                 peer = 0;
    std::vector<uint8_t> data;          // payload for Receive
    bool                 reliable = false;
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool host(uint16_t port)                                = 0;
    virtual bool connect(const std::string& address, uint16_t port) = 0;
    virtual void disconnect()                                       = 0;

    virtual bool     isServer()  const = 0;
    virtual bool     isClient()  const = 0;
    virtual Peer     selfId()    const = 0;
    virtual uint32_t peerCount() const = 0;
    virtual uint32_t ping(Peer peer) const = 0;

    virtual void send(Peer peer, const void* data, uint32_t size, bool reliable) = 0;
    virtual void broadcast(const void* data, uint32_t size, bool reliable)       = 0;

    // Poll sockets; append connect/disconnect/receive events since the last call.
    virtual void poll(std::vector<TransportEvent>& out) = 0;
};

// Implemented per backend (sdl/transport_sdlnet.cpp or webgpu/transport_web.cpp).
ITransport* createTransport();
/// @endcond

} // namespace Net
