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
#include <unordered_map>

/// @cond INTERNAL
// The transport layer is internal plumbing; only a forward declaration is needed here so
// the public header never pulls in SDL_net / socket types. Mirrors how IGpu is declared.
class ITransport;
/// @endcond

/// @brief High-level client/server networking.
class Net {
public:
    /// @brief Peer identifier. On a client, peer 0 is the server; on a server, peers are
    ///        connected clients (1..N) and 0 is reserved/self.
    using Peer = uint32_t;
    /// @brief The reserved peer id for the server.
    static constexpr Peer SERVER_PEER = 0;

    /// @brief Callback invoked for a received message of type T, from a given peer.
    /// @tparam T The trivially-copyable packet type.
    template <typename T>
    using MessageHandler = std::function<void(Peer peer, const T &packet)>;

    // ── Lifecycle / connection ────────────────────────────────────────────────
    /// @brief Initializes the networking subsystem. Call before Host/Connect.
    static bool Init() { return Get()._init(); }
    /// @brief Shuts down networking and releases transport resources.
    static void Shutdown() { Get()._shutdown(); }

    /// @brief Becomes a server listening on the given port.
    /// @param port Port to listen on.
    /// @return True on success.
    static bool Host(uint16_t port) { return Get()._host(port); }
    /// @brief Becomes a client and connects to a server.
    /// @param address Server host name or IP.
    /// @param port Server port.
    /// @return True on success.
    static bool Connect(const std::string &address, uint16_t port) { return Get()._connect(address, port); }
    /// @brief Disconnects from the current session (server or client).
    static void Disconnect() { Get()._disconnect(); }

    /// @brief Polls the transport and dispatches received messages to their handlers. Call each frame.
    static void Update() { Get()._update(); }

    /// @brief Returns true if this peer is acting as the server.
    static bool IsServer() { return Get()._isServer(); }
    /// @brief Returns true if this peer is acting as a client.
    static bool IsClient() { return Get()._isClient(); }

    /// @brief Returns this client's own peer id (client side).
    static Peer GetClientID() { return Get()._getClientID(); }
    /// @brief Returns the number of currently connected peers.
    static uint32_t GetPeerCount() { return Get()._getPeerCount(); }
    /// @brief Returns the round-trip time to a peer in milliseconds (0 if unknown).
    /// @param peer The peer to query.
    static uint32_t GetPing(Peer peer) { return Get()._getPing(peer); }

    // ── Typed messaging ───────────────────────────────────────────────────────
    /// @brief Sends a packet to a peer over the unreliable channel.
    /// @tparam T Trivially-copyable packet type.
    /// @param peer Destination peer.
    /// @param packet The packet to send.
    template <typename T>
    static void Send(Peer peer, const T &packet) {
        static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
        Get()._sendRaw(peer, _typeId<T>(), &packet, sizeof(T), false);
    }

    /// @brief Sends a packet to a peer over the reliable channel.
    /// @tparam T Trivially-copyable packet type.
    /// @param peer Destination peer.
    /// @param packet The packet to send.
    template <typename T>
    static void SendReliable(Peer peer, const T &packet) {
        static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
        Get()._sendRaw(peer, _typeId<T>(), &packet, sizeof(T), true);
    }

    /// @brief Sends a packet to all connected peers over the unreliable channel.
    /// @tparam T Trivially-copyable packet type.
    /// @param packet The packet to broadcast.
    template <typename T>
    static void Broadcast(const T &packet) {
        static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
        Get()._broadcastRaw(_typeId<T>(), &packet, sizeof(T), false);
    }

    /// @brief Sends a packet to all connected peers over the reliable channel.
    /// @tparam T Trivially-copyable packet type.
    /// @param packet The packet to broadcast.
    template <typename T>
    static void BroadcastReliable(const T &packet) {
        static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
        Get()._broadcastRaw(_typeId<T>(), &packet, sizeof(T), true);
    }

    /// @brief Registers a handler invoked whenever a packet of type T arrives.
    /// @tparam T Trivially-copyable packet type (its type id is derived at compile time).
    /// @param handler Callback receiving the sender peer and the decoded packet.
    template <typename T>
    static void RegisterMessage(MessageHandler<T> handler) {
        static_assert(std::is_trivially_copyable_v<T>, "Net packet must be trivially copyable");
        Get()._registerRaw(_typeId<T>(),
            [handler](Peer peer, const void *data, uint32_t size) {
                if (size != sizeof(T))
                    return;
                T packet;
                std::memcpy(&packet, data, sizeof(T));
                handler(peer, packet);
            });
    }

    // ── Thin raw-UDP path (native only) ───────────────────────────────────────
    // For protocols that do their own packet format + reliability (Quake's net_dgrm).
    // Opaque handles — no SDL_net types leak out.
    /// @brief Raw-datagram socket path for protocols that bring their own reliability.
    class Udp {
    public:
        /// @brief An opaque UDP socket handle.
        using Socket = void *;
        /// @brief An opaque UDP endpoint (host + port).
        struct Address {
            void    *handle = nullptr; ///< Opaque backend host handle.
            uint16_t port   = 0;       ///< Port number.
        };

        /// @brief Binds a UDP socket. Pass 0 to use any free port.
        /// @param port Port to bind, or 0 for any.
        /// @return A socket handle (null on failure).
        static Socket Open(uint16_t port) { return Get()._open(port); }
        /// @brief Closes a UDP socket.
        static void Close(Socket s) { Get()._close(s); }
        /// @brief Sends a datagram to an address.
        /// @param s Socket to send on.
        /// @param to Destination address.
        /// @param data Payload bytes.
        /// @param len Payload length.
        /// @return True on success.
        static bool Send(Socket s, const Address &to, const void *data, int len) { return Get()._send(s, to, data, len); }
        /// @brief Receives a datagram if one is available.
        /// @param s Socket to read from.
        /// @param from Filled with the sender's address.
        /// @param data Buffer to receive into.
        /// @param maxLen Buffer capacity.
        /// @return Bytes received (>0), 0 if none, <0 on error.
        static int Recv(Socket s, Address &from, void *data, int maxLen) { return Get()._recv(s, from, data, maxLen); }

        /// @brief Resolves a hostname/IP + port into an Address (blocking).
        static Address Resolve(const std::string &host, uint16_t port) { return Get()._resolve(host, port); }
        /// @brief Returns true if the address is valid/resolved.
        static bool Valid(const Address &a) { return Get()._valid(a); }
        /// @brief Returns true if two addresses refer to the same endpoint.
        static bool Equal(const Address &a, const Address &b) { return Get()._equal(a, b); }
        /// @brief Formats an address as "ip:port".
        static std::string ToString(const Address &a) { return Get()._toString(a); }
        /// @brief Frees any backend resources held by an address.
        static void Free(Address &a) { Get()._free(a); }

    private:
        Socket      _open(uint16_t port);
        void        _close(Socket s);
        bool        _send(Socket s, const Address &to, const void *data, int len);
        int         _recv(Socket s, Address &from, void *data, int maxLen);
        Address     _resolve(const std::string &host, uint16_t port);
        bool        _valid(const Address &a);
        bool        _equal(const Address &a, const Address &b);
        std::string _toString(const Address &a);
        void        _free(Address &a);

    public:
        /// @cond INTERNAL
        Udp(const Udp &) = delete;

        static Udp &Get() {
            static Udp instance;
            return instance;
        }
        /// @endcond

    private:
        Udp() = default;
    };

private:
    // Compile-time FNV-1a hash of a C string.
    static constexpr uint32_t _fnv1a(const char *s, uint32_t h = 2166136261u) {           // NOLINT(readability-identifier-naming) — private static; clang-tidy files statics as ClassMethod
        return (*s == 0) ? h : _fnv1a(s + 1, (h ^ static_cast<uint8_t>(*s)) * 16777619u); // NOLINT(readability-identifier-naming) — private static; clang-tidy files statics as ClassMethod
    }
    // Stable per-type id from the compiler's decorated function name.
    template <typename T>
    static constexpr uint32_t _typeId() { // NOLINT(readability-identifier-naming) — private static; clang-tidy files statics as ClassMethod
#if defined(_MSC_VER)
        return _fnv1a(__FUNCSIG__);
#else
        return _fnv1a(__PRETTY_FUNCTION__);
#endif
    }

    bool     _init();
    void     _shutdown();
    bool     _host(uint16_t port);
    bool     _connect(const std::string &address, uint16_t port);
    void     _disconnect();
    void     _update();
    bool     _isServer();
    bool     _isClient();
    Peer     _getClientID();
    uint32_t _getPeerCount();
    uint32_t _getPing(Peer peer);

    // Transport-agnostic primitives.
    void _sendRaw(Peer peer, uint32_t typeId, const void *data, uint32_t size, bool reliable);
    void _broadcastRaw(uint32_t typeId, const void *data, uint32_t size, bool reliable);
    void _registerRaw(uint32_t typeId, std::function<void(Peer, const void *, uint32_t)> handler);

    bool _ensureTransport();

    ITransport                                                                     *_transport = nullptr;
    std::unordered_map<uint32_t, std::function<void(Peer, const void *, uint32_t)>> _handlers;

public:
    /// @cond INTERNAL
    Net(const Net &) = delete;

    static Net &Get() {
        static Net instance;
        return instance;
    }
    /// @endcond

private:
    Net() = default;
};
