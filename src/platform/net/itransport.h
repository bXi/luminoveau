#pragma once

// Internal swappable transport behind the typed Net:: API. One backend is active at a
// time (native SDL_net, or a web WebSocket backend later). net.cpp drives it; user code
// never sees this. Messages are opaque byte blobs ([typeId][payload], built by net.cpp).

#include <cstdint>
#include <functional>
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

    // Hands a blob to a peer out of band. Only transports that cannot reach a peer on their
    // own need this; Net wires it to the active brokerage.
    using SignalSender = std::function<bool(const PlayerId &to, const void *data, uint32_t size)>;

    virtual bool Host(const Net::HostConfig &cfg)  = 0;
    virtual bool Connect(const Net::Endpoint &ep)  = 0;
    virtual void Disconnect()                      = 0;
    // Drops one peer without tearing down the session. Used to refuse a failed handshake.
    virtual void DisconnectPeer(Net::Peer peer)    = 0;

    virtual bool      IsServer() const           = 0;
    virtual bool      IsClient() const           = 0;
    virtual Net::Peer SelfId() const             = 0;
    virtual uint32_t  PeerCount() const          = 0;
    virtual uint32_t  Ping(Net::Peer peer) const = 0;

    // Largest single message this transport accepts; callers fragment above it.
    virtual uint32_t      MaxMessageSize() const = 0;
    // Why the last Host/Connect failed, so Net can surface a reason instead of a bare false.
    virtual Net::NetError LastError() const      = 0;

    virtual void Send(Net::Peer peer, const void *data, uint32_t size, bool reliable) = 0;
    virtual void Broadcast(const void *data, uint32_t size, bool reliable)            = 0;

    // Poll sockets; append connect/disconnect/receive events since the last call.
    virtual void Poll(std::vector<TransportEvent> &out) = 0;

    // Signalling. No-ops for transports that address peers directly.
    virtual void SetSignalSender(SignalSender sender) { (void)sender; }
    virtual void DeliverSignal(const PlayerId &from, const void *data, uint32_t size) {
        (void)from;
        (void)data;
        (void)size;
    }

    // Whether this transport needs a brokerage to introduce peers before it can connect.
    virtual bool NeedsSignaling() const { return false; }
};

// Implemented in transportfactory.cpp. `wantsSignaling` picks the transport that needs a
// brokerage to introduce peers, which is the only one a browser can use.
ITransport *createTransport(bool wantsSignaling);
/// @endcond
