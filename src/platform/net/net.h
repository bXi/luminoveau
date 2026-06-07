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

// Peer identifier. On a client, peer 0 is the server. On a server, peers are connected
// clients (1..N); 0 is reserved/self.
using Peer = uint32_t;
constexpr Peer SERVER_PEER = 0;

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
bool Init();
void Shutdown();

bool Host(uint16_t port);                                   // become a server
bool Connect(const std::string& address, uint16_t port);   // become a client
void Disconnect();

void Update();                                              // poll transport, dispatch messages

bool IsServer();
bool IsClient();

Peer     GetClientID();          // this client's peer id (client side)
uint32_t GetPeerCount();         // connected peers
uint32_t GetPing(Peer peer);     // round-trip ms, 0 if unknown

// ── Typed messaging ───────────────────────────────────────────────────────────
template <typename T>
void Send(Peer peer, const T& packet) {
    static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
    detail::sendRaw(peer, detail::typeId<T>(), &packet, sizeof(T), false);
}

template <typename T>
void SendReliable(Peer peer, const T& packet) {
    static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
    detail::sendRaw(peer, detail::typeId<T>(), &packet, sizeof(T), true);
}

template <typename T>
void Broadcast(const T& packet) {
    static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
    detail::broadcastRaw(detail::typeId<T>(), &packet, sizeof(T), false);
}

template <typename T>
void BroadcastReliable(const T& packet) {
    static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
    detail::broadcastRaw(detail::typeId<T>(), &packet, sizeof(T), true);
}

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
    using Socket = void*;                 // opaque (e.g. SDLNet_DatagramSocket*)
    struct Address { void* handle = nullptr; uint16_t port = 0; };  // opaque host + port

    Socket Open(uint16_t port);                                   // bind a UDP socket (0 = any free port)
    void   Close(Socket s);
    bool   Send(Socket s, const Address& to, const void* data, int len);
    int    Recv(Socket s, Address& from, void* data, int maxLen); // >0 bytes, 0 none, <0 error

    Address     Resolve(const std::string& host, uint16_t port);  // blocking hostname/IP resolve
    bool        Valid(const Address& a);
    bool        Equal(const Address& a, const Address& b);
    std::string ToString(const Address& a);                       // "ip:port"
    void        Free(Address& a);
}

} // namespace Net
