#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Net — high-level client/server networking.
//
// Hides sockets, SDL_net types, packet buffers and transport details. Two layers:
//
//   1. Typed high-level API (Net::Send<T> / RegisterMessage<T> / ...): strongly-typed
//      POD packet structs, auto-dispatched by type. For new games. Sits on a swappable
//      ITransport (native SDL_net now: TCP stream = reliable, UDP datagram = unreliable;
//      browser WebSocket later).
//
//   2. Net::Udp — a thin raw-datagram path (open/send/recv/resolve) for code that brings
//      its own protocol + reliability (e.g. Quake's net_dgrm driver). Native only.
//
// Serialization is raw memcpy of trivially-copyable structs (all targets little-endian).
// Message type IDs are derived from the type name at compile time (both peers must be the
// same build — fine for a single game).
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <cstring>
#include <string>
#include <functional>
#include <type_traits>

namespace Net {

/// @brief Peer identifier. On a client, peer 0 is the server; on a server, peers are
///        connected clients (1..N) and 0 is reserved/self.
using Peer = uint32_t;
/// @brief The reserved peer id for the server.
constexpr Peer SERVER_PEER = 0;

/// @brief Callback invoked for a received message of type T, from a given peer.
/// @tparam T The trivially-copyable packet type.
template <typename T>
using MessageHandler = std::function<void(Peer peer, const T& packet)>;

namespace detail {
    // Compile-time FNV-1a hash of a C string.
    constexpr uint32_t fnv1a(const char* s, uint32_t h = 2166136261u) {
        return (*s == 0) ? h : fnv1a(s + 1, (h ^ static_cast<uint8_t>(*s)) * 16777619u);
    }
    // Stable per-type id from the compiler's decorated function name.
    template <typename T>
    constexpr uint32_t typeId() {
#if defined(_MSC_VER)
        return fnv1a(__FUNCSIG__);
#else
        return fnv1a(__PRETTY_FUNCTION__);
#endif
    }
    // Transport-agnostic primitives implemented in net.cpp.
    void sendRaw(Peer peer, uint32_t typeId, const void* data, uint32_t size, bool reliable);
    void broadcastRaw(uint32_t typeId, const void* data, uint32_t size, bool reliable);
    void registerRaw(uint32_t typeId, std::function<void(Peer, const void*, uint32_t)> handler);
}

// ── Lifecycle / connection ────────────────────────────────────────────────────
/// @brief Initializes the networking subsystem. Call before Host/Connect.
bool Init();
/// @brief Shuts down networking and releases transport resources.
void Shutdown();

/// @brief Becomes a server listening on the given port.
/// @param port Port to listen on.
/// @return True on success.
bool Host(uint16_t port);
/// @brief Becomes a client and connects to a server.
/// @param address Server host name or IP.
/// @param port Server port.
/// @return True on success.
bool Connect(const std::string& address, uint16_t port);
/// @brief Disconnects from the current session (server or client).
void Disconnect();

/// @brief Polls the transport and dispatches received messages to their handlers. Call each frame.
void Update();

/// @brief Returns true if this peer is acting as the server.
bool IsServer();
/// @brief Returns true if this peer is acting as a client.
bool IsClient();

/// @brief Returns this client's own peer id (client side).
Peer     GetClientID();
/// @brief Returns the number of currently connected peers.
uint32_t GetPeerCount();
/// @brief Returns the round-trip time to a peer in milliseconds (0 if unknown).
/// @param peer The peer to query.
uint32_t GetPing(Peer peer);

// ── Typed messaging ───────────────────────────────────────────────────────────
/// @brief Sends a packet to a peer over the unreliable channel.
/// @tparam T Trivially-copyable packet type.
/// @param peer Destination peer.
/// @param packet The packet to send.
template <typename T>
void Send(Peer peer, const T& packet) {
    static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
    detail::sendRaw(peer, detail::typeId<T>(), &packet, sizeof(T), false);
}

/// @brief Sends a packet to a peer over the reliable channel.
/// @tparam T Trivially-copyable packet type.
/// @param peer Destination peer.
/// @param packet The packet to send.
template <typename T>
void SendReliable(Peer peer, const T& packet) {
    static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
    detail::sendRaw(peer, detail::typeId<T>(), &packet, sizeof(T), true);
}

/// @brief Sends a packet to all connected peers over the unreliable channel.
/// @tparam T Trivially-copyable packet type.
/// @param packet The packet to broadcast.
template <typename T>
void Broadcast(const T& packet) {
    static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
    detail::broadcastRaw(detail::typeId<T>(), &packet, sizeof(T), false);
}

/// @brief Sends a packet to all connected peers over the reliable channel.
/// @tparam T Trivially-copyable packet type.
/// @param packet The packet to broadcast.
template <typename T>
void BroadcastReliable(const T& packet) {
    static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
    detail::broadcastRaw(detail::typeId<T>(), &packet, sizeof(T), true);
}

/// @brief Registers a handler invoked whenever a packet of type T arrives.
/// @tparam T Trivially-copyable packet type (its type id is derived at compile time).
/// @param handler Callback receiving the sender peer and the decoded packet.
template <typename T>
void RegisterMessage(MessageHandler<T> handler) {
    static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
    detail::registerRaw(detail::typeId<T>(),
        [handler](Peer peer, const void* data, uint32_t size) {
            if (size != sizeof(T)) return;
            T packet;
            std::memcpy(&packet, data, sizeof(T));
            handler(peer, packet);
        });
}

// ── Thin raw-UDP path (native only) ───────────────────────────────────────────
// For protocols that do their own packet format + reliability (Quake's net_dgrm).
// Opaque handles — no SDL_net types leak out.
namespace Udp {
    /// @brief An opaque UDP socket handle.
    using Socket = void*;
    /// @brief An opaque UDP endpoint (host + port).
    struct Address {
        void* handle = nullptr;  ///< Opaque backend host handle.
        uint16_t port = 0;       ///< Port number.
    };

    /// @brief Binds a UDP socket. Pass 0 to use any free port.
    /// @param port Port to bind, or 0 for any.
    /// @return A socket handle (null on failure).
    Socket Open(uint16_t port);
    /// @brief Closes a UDP socket.
    void   Close(Socket s);
    /// @brief Sends a datagram to an address.
    /// @param s Socket to send on.
    /// @param to Destination address.
    /// @param data Payload bytes.
    /// @param len Payload length.
    /// @return True on success.
    bool   Send(Socket s, const Address& to, const void* data, int len);
    /// @brief Receives a datagram if one is available.
    /// @param s Socket to read from.
    /// @param from Filled with the sender's address.
    /// @param data Buffer to receive into.
    /// @param maxLen Buffer capacity.
    /// @return Bytes received (>0), 0 if none, <0 on error.
    int    Recv(Socket s, Address& from, void* data, int maxLen);

    /// @brief Resolves a hostname/IP + port into an Address (blocking).
    Address     Resolve(const std::string& host, uint16_t port);
    /// @brief Returns true if the address is valid/resolved.
    bool        Valid(const Address& a);
    /// @brief Returns true if two addresses refer to the same endpoint.
    bool        Equal(const Address& a, const Address& b);
    /// @brief Formats an address as "ip:port".
    std::string ToString(const Address& a);
    /// @brief Frees any backend resources held by an address.
    void        Free(Address& a);
}

} // namespace Net
