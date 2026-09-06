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
// Orthogonal to the transport is the brokerage: who introduces peers to each other. A
// direct address needs no broker at all; Steam and friends need one. Which broker is
// active decides what the session API can do, so ask Net::Can rather than assuming.
//
// Serialization is raw memcpy of trivially-copyable structs (all targets little-endian).
// Message type IDs are derived from the type name at compile time, so both peers must be
// the same build; the connect handshake turns that into a clean VersionMismatch.
// ─────────────────────────────────────────────────────────────────────────────

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "platform/net/playerid.h"

/// @cond INTERNAL
// Transport and brokerage are internal plumbing; forward declarations only, so the public
// header never pulls in SDL_net or a platform SDK. Mirrors how IGpu is declared.
class ITransport;
class IBroker;
/// @endcond

/// @brief Gives a packet type an id that is stable across compilers.
///
/// By default a message's type id is hashed from the compiler's decorated function name, which is
/// only stable between peers built by the *same compiler*: GCC writes `[with T = Hello]` where
/// Clang writes `[T = Hello]` and MSVC writes something else again, so one struct hashes to three
/// different ids. The failure is quiet and misleading — the connect handshake uses a fixed id, so
/// peers connect, and then every typed message is dropped for want of a handler. It reads as a
/// session that joins successfully and does nothing.
///
/// Specialise this to pin a packet's id. Any non-zero value will do, as long as both sides agree
/// and the values do not collide within one game:
///
/// @code
/// template <> struct NetMessageId<Heartbeat> { static constexpr uint32_t value = 0x1001; };
/// @endcode
///
/// Left at zero the derived id stands, so nothing that already works changes — but a game that
/// wants two platforms in one session has to pin every packet it sends.
template <typename T>
struct NetMessageId {
    static constexpr uint32_t value = 0; ///< 0 = derive from the compiler's decorated name.
};

/// @brief High-level client/server networking.
class Net {
public:
    /// @brief Which brokerage introduces peers. Auto picks the best one available.
    enum class Broker { Auto,
        Direct,    ///< Addresses typed in or passed around out of band. No service, no accounts.
        Lan,       ///< UDP broadcast discovery on the local subnet.
        Signaling, ///< A service that introduces peers; the only route a browser has. See SetSignalingUrl.
        Steam,
        Eos };

    /// @brief What the active brokerage can actually do. Query rather than assume.
    enum class Feature { Browse,    ///< A list of joinable sessions can be produced.
        Invite,                     ///< Platform invites / join-from-friend exist.
        Friends,                    ///< A friends list is available.
        Relay,                      ///< Connections survive a failed NAT punch.
        Crossplay,                  ///< Peers may be on other platforms.
        Lobby };                    ///< Pre-session lobby with shared key/value data.

    /// @brief Why a host or join attempt failed.
    enum class NetError { None,
        NoTransport,       ///< No networking backend on this platform.
        BrokerUnavailable, ///< The brokerage this endpoint needs is not running.
        HostUnreachable,
        NatBlockedNoRelay, ///< Punch failed and the active broker has no relay to fall back on.
        RelayFailed,       ///< Punch failed *with* a relay configured — a server problem, not the
                           ///< player's network. Distinct from NatBlockedNoRelay because the two
                           ///< need opposite responses, and distinct from Refused because that
                           ///< already means a rejected handshake: one message covering both
                           ///< leaves "refused" meaning either "your relay is broken" or "the
                           ///< other side is a different build", which is no help at all.
        VersionMismatch,   ///< The other side is a different build.
        LobbyFull,
        Refused,           ///< The handshake was rejected — build id or packet layout.
        Timeout };

    /// @brief Peer identifier. On a client, peer 0 is the server; on a server, peers are
    ///        connected clients (1..N) and 0 is reserved/self.
    using Peer = uint32_t;
    /// @brief The reserved peer id for the server.
    static constexpr Peer SERVER_PEER = 0;

    /// @brief Callback invoked for a received message of type T, from a given peer.
    /// @tparam T The trivially-copyable packet type.
    template <typename T>
    using MessageHandler = std::function<void(Peer peer, const T &packet)>;

    /// @brief How a session is advertised. A port only means anything to an address broker.
    struct HostConfig {
        std::string name;              ///< Shown in session lists.
        uint16_t    port          = 0; ///< 0 = the broker's choice, or not applicable.
        int         slots         = 8;
        bool        publicListing = true;
        /// @brief Opaque payload passed through to anyone browsing — map name, mode, whatever.
        std::vector<uint8_t> blob;
    };

    /// @brief Where to join: an address, or a peer the broker knows how to reach.
    struct Endpoint {
        enum class Kind { Address,
            Player,
            Lobby,
            JoinCode };

        Kind        kind    = Kind::Address;
        std::string address;       ///< Kind::Address
        uint16_t    port    = 0;   ///< Kind::Address
        PlayerId    player;        ///< Kind::Player
        uint64_t    lobby   = 0;   ///< Kind::Lobby
        std::string code;          ///< Kind::JoinCode
    };

    /// @brief One joinable session found by the active brokerage.
    struct SessionInfo {
        Endpoint             endpoint; ///< Pass straight to Join.
        PlayerId             host;     ///< Who is hosting; one session, however many addresses reach it.
        std::string          name;
        int                  players = 0;
        int                  slots   = 0;
        std::vector<uint8_t> blob; ///< Whatever the host put in HostConfig::blob.
    };

    // ── Lifecycle / connection ────────────────────────────────────────────────
    /// @brief Initializes the networking subsystem. Call before Host/Join.
    /// @param preference Which brokerage to use; Auto takes the best one built in.
    static bool Init(Broker preference = Broker::Auto) { return Get()._init(preference); }
    /// @brief Shuts down networking and releases transport and broker resources.
    static void Shutdown() { Get()._shutdown(); }

    /// @brief Identifies this build to the other side. Peers whose id differs are refused.
    /// @param id Any value the game considers a compatibility fence.
    /// @note Must be computed identically on every platform that shares a session. A literal
    ///       constant or a hash of a version string is fine; anything build-local — a
    ///       timestamp, a path, __DATE__, a compiler-derived value — is not, and will refuse
    ///       every cross-platform join with VersionMismatch.
    /// @note This covers what the *game* considers incompatible. The engine checks its own
    ///       protocol version and the layout of registered packets separately, so this does
    ///       not need to account for either.
    static void SetBuildId(uint64_t id) { Get()._setBuildId(id); }

    /// @brief One ICE server: a STUN server to discover an address with, or a TURN server to
    ///        relay through when no direct path exists.
    ///
    /// @note **Credentials are separate fields rather than packed into the URL, and they have to
    ///       be.** The `turn:user:password@host` form works on native, where libdatachannel
    ///       parses it — and silently does not on the web, where datachannel-wasm stores an
    ///       unparsed URL as a `Dummy` server and hands the browser an empty username and
    ///       password. A relay configured that way fails to authenticate in exactly the build
    ///       most likely to need one.
    struct IceServer {
        IceServer() = default;

        /// Implicit on purpose, so `SetIceServers({"stun:host:3478"})` still reads well.
        IceServer(std::string url_) : url(std::move(url_)) {}

        IceServer(std::string url_, std::string username_, std::string credential_)
            : url(std::move(url_)), username(std::move(username_)),
              credential(std::move(credential_)) {}

        std::string url;        ///< "stun:host:3478", "turn:host:3478", "turns:host:5349".
        std::string username;   ///< TURN only. Empty for STUN.
        std::string credential; ///< TURN only. Empty for STUN.
    };

    /// @brief Sets the ICE servers used to find a path between peers.
    /// @param servers STUN and TURN entries, e.g. `{"stun:stun.example.org:3478"}` or
    ///        `{{"turn:turn.example.org:3478", user, password}}`.
    /// @note Without a TURN server a peer behind symmetric NAT or CGNAT cannot be reached at
    ///       all, and joining it fails with NatBlockedNoRelay. TURN is what makes
    ///       Can(Feature::Relay) true.
    /// @note Defaults to a public STUN server, which is enough to discover an address but
    ///       never enough to relay. Ship your own if you would rather not depend on it.
    /// @note May be called after Init, and the signalling broker does: the list is read when a
    ///       peer connection is opened, not when the transport starts, so a service that hands
    ///       out short-lived relay credentials in its welcome can set them then. Guarded by a
    ///       mutex for that reason — the broker's socket callback is not the game's thread.
    static void SetIceServers(std::vector<IceServer> servers) { Get()._setIceServers(std::move(servers)); }

    /// @brief Supplies the CA certificates used to verify a wss:// signalling service.
    /// @param pemOrPath Either a path to a PEM bundle or the PEM text itself.
    /// @note Native builds only; a browser uses its own trust store. Mbed TLS carries no
    ///       system trust, so without this a wss:// service is refused with "No CA Chain is
    ///       set". Common Unix bundle locations are tried automatically, but Windows keeps
    ///       its certificates somewhere Mbed TLS cannot read — ship a cacert.pem there and
    ///       point this at it.
    static void SetSignalingCaCert(const std::string &pemOrPath) { Get()._signalingCaCert = pemOrPath; }

    /// @brief Points the engine at a signalling service, which is what lets a browser and a
    ///        native peer reach each other. Call before Init.
    /// @param url A WebSocket URL; a browser needs wss:// unless the page itself is insecure.
    /// @note The engine ships no default — the service is the game's to run.
    static void SetSignalingUrl(const std::string &url) { Get()._signalingUrl = url; }

    /// @brief This host's own room code, once the brokerage has minted one.
    /// @return The code, or empty if hosting has not been brokered or the brokerage has no codes.
    /// @note There is nowhere else to learn it. An Endpoint::code names somebody *else's* session,
    ///       and a host does not appear in its own session list. Arrives asynchronously — empty
    ///       until the service answers, so poll it rather than reading it once after Host().
    static const std::string &RoomCode() { return Get()._roomCode; }

    /// @brief Becomes a server.
    /// @param cfg How the session is advertised.
    /// @return True on success; see LastError otherwise.
    static bool Host(const HostConfig &cfg) { return Get()._host(cfg); }
    /// @brief Becomes a server listening on the given port.
    /// @param port Port to listen on.
    /// @return True on success.
    static bool Host(uint16_t port) { return Get()._host(HostConfig{ .port = port }); }

    /// @brief Becomes a client and joins a session.
    /// @param ep Where to join.
    /// @return True on success; see LastError otherwise.
    static bool Join(const Endpoint &ep) { return Get()._join(ep); }
    /// @brief Joins without blocking. The callback fires once, from Update.
    /// @param ep Where to join.
    /// @param onResult Receives whether the join succeeded and, if not, why.
    /// @note Negotiating a path can take seconds, which the blocking overload spends inside
    ///       the frame. Prefer this one anywhere that has to keep drawing.
    static void Join(const Endpoint &ep, std::function<void(bool, NetError)> onResult) {
        Get()._joinAsync(ep, std::move(onResult));
    }
    /// @brief Becomes a client and connects to a server by address.
    /// @param address Server host name or IP.
    /// @param port Server port.
    /// @return True on success.
    static bool Connect(const std::string &address, uint16_t port) {
        return Get()._join(Endpoint{ .address = address, .port = port });
    }
    /// @brief Disconnects from the current session (server or client).
    static void Disconnect() { Get()._disconnect(); }

    /// @brief Polls the broker and transport and dispatches received messages. Call each frame.
    static void Update() { Get()._update(); }

    // ── Capabilities and identity ─────────────────────────────────────────────
    /// @brief Returns true if the active brokerage supports a feature.
    /// @param feature The capability to test.
    static bool Can(Feature feature) { return Get()._can(feature); }
    /// @brief Returns why the last Host or Join failed.
    static NetError LastError() { return Get()._lastError; }
    /// @brief Returns this machine's player identity, as minted by the active brokerage.
    static PlayerId LocalPlayer() { return Get()._localPlayer(); }

    /// @brief Returns the identity of a connected peer, or an invalid id if it is unknown.
    /// @param peer The peer to look up.
    static PlayerId IdOf(Peer peer) { return Get()._idOf(peer); }
    /// @brief Returns the peer for an identity, or 0 if that player is not in this session.
    /// @param id The identity to look up.
    static Peer PeerOf(const PlayerId &id) { return Get()._peerOf(id); }

    /// @brief Largest single message the transport accepts. Fragment anything bigger yourself.
    static uint32_t MaxMessageSize() { return Get()._maxMessageSize(); }

    // ── Finding sessions ──────────────────────────────────────────────────────
    /// @brief Asks the brokerage what is joinable. Results arrive through OnSessions.
    ///        Does nothing unless Can(Feature::Browse).
    static void QuerySessions() { Get()._querySessions(); }
    /// @brief Sets the callback fired whenever the session list changes during a query.
    /// @param handler Callback receiving the full list, not a delta.
    static void OnSessions(std::function<void(const std::vector<SessionInfo> &)> handler) {
        Get()._onSessions = std::move(handler);
    }

    /// @brief Sets the callback fired when a peer joins the session and clears the handshake.
    /// @param handler Callback receiving the new peer.
    static void OnPeerJoined(std::function<void(Peer)> handler) { Get()._onPeerJoined = std::move(handler); }
    /// @brief Sets the callback fired when a peer leaves or is dropped.
    /// @param handler Callback receiving the departing peer.
    static void OnPeerLeft(std::function<void(Peer)> handler) { Get()._onPeerLeft = std::move(handler); }
    /// @brief Sets the callback fired when the player accepts an invite from outside the game.
    /// @param handler Callback receiving an endpoint ready to pass to Join.
    static void OnInvite(std::function<void(const Endpoint &)> handler) { Get()._onInvite = std::move(handler); }

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
        Get()._noteLayout(_typeId<T>(), (uint32_t)sizeof(T));
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
        /// @brief Binds a UDP socket that may send to the subnet's broadcast address.
        /// @param port Port to bind, or 0 for any.
        /// @return A socket handle (null on failure).
        /// @note Needed to receive broadcasts over IPv6 as well as to send them.
        static Socket OpenBroadcast(uint16_t port) { return Get()._openBroadcast(port); }
        /// @brief Closes a UDP socket.
        static void Close(Socket s) { Get()._close(s); }
        /// @brief Sends a datagram to every machine on the local subnet.
        /// @param s Socket to send on.
        /// @param port Destination port.
        /// @param data Payload bytes.
        /// @param len Payload length.
        /// @return True on success.
        /// @note Every device on the LAN pays for this whether it is playing or not. Use it to
        ///       ask a question, then talk to whoever answers directly.
        static bool Broadcast(Socket s, uint16_t port, const void *data, int len) { return Get()._broadcast(s, port, data, len); }
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
        Socket      _openBroadcast(uint16_t port);
        void        _close(Socket s);
        bool        _send(Socket s, const Address &to, const void *data, int len);
        bool        _broadcast(Socket s, uint16_t port, const void *data, int len);
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
    // Per-type id: the game's pinned value where it gave one, otherwise hashed from the
    // compiler's decorated function name — which only agrees between peers built by the same
    // compiler. See NetMessageId.
    template <typename T>
    static constexpr uint32_t _typeId() { // NOLINT(readability-identifier-naming) — private static; clang-tidy files statics as ClassMethod
        if constexpr (NetMessageId<T>::value != 0) {
            return NetMessageId<T>::value;
        } else {
#if defined(_MSC_VER)
            return _fnv1a(__FUNCSIG__);
#else
            return _fnv1a(__PRETTY_FUNCTION__);
#endif
        }
    }

    bool     _init(Broker preference);
    void     _shutdown();
    bool     _host(const HostConfig &cfg);
    bool     _join(const Endpoint &ep);
    void     _disconnect();
    void     _update();
    bool     _isServer();
    bool     _isClient();
    Peer     _getClientID();
    uint32_t _getPeerCount();
    uint32_t _getPing(Peer peer);
    bool     _can(Feature feature);
    void     _setBuildId(uint64_t id);
    void     _querySessions();
    PlayerId _localPlayer();
    PlayerId _idOf(Peer peer);
    Peer     _peerOf(const PlayerId &id);
    uint32_t _maxMessageSize();

public:
    /// @cond INTERNAL
    // Read by the WebRTC transport when it opens a peer connection. **By value**: the list can be
    // replaced from the signalling broker's socket thread, so handing out a reference would be a
    // reference into a vector somebody else may be reassigning.
    static std::vector<IceServer> IceServers() { return Get()._getIceServers(); }
    // Read by the signalling broker when it opens its socket.
    static const std::string              &SignalingCaCert() { return Get()._signalingCaCert; }
    /// @endcond

private:

    // Transport-agnostic primitives.
    void _sendRaw(Peer peer, uint32_t typeId, const void *data, uint32_t size, bool reliable);
    void _broadcastRaw(uint32_t typeId, const void *data, uint32_t size, bool reliable);
    void     _registerRaw(uint32_t typeId, std::function<void(Peer, const void *, uint32_t)> handler);
    void     _noteLayout(uint32_t typeId, uint32_t size);
    uint64_t _layoutDigest() const;

    bool _ensureTransport();
    // Runs the connect handshake and reports whether the other side accepted us.
    // A join settles over several frames, so each stage is a step that reports whether it
    // is still waiting. The blocking and callback overloads drive the same ones.
    enum class Step { Pending,
        Done,
        Failed };
    enum class JoinStage { None,
        Resolving,
        Connecting,
        Handshaking };

    bool _beginJoin(const Endpoint &ep);
    void _joinAsync(const Endpoint &ep, std::function<void(bool, NetError)> onResult);
    bool _startConnect();
    Step _advanceJoin();
    Step _failJoin();
    Step _stepResolve();
    Step _stepConnect();
    Step _stepHandshake();
    void _sendHello();
    void _pumpBroker();
    void _handleEvent(const struct TransportEvent &ev);
    void _handleHandshake(Peer peer, const void *data, uint32_t size);
    void _dispatch(Peer peer, uint32_t typeId, const void *data, uint32_t size);

    ITransport *_transport = nullptr;
    IBroker    *_broker    = nullptr;
    NetError    _lastError = NetError::None;
    uint64_t    _buildId   = 0;
    JoinStage                             _joinStage = JoinStage::None;
    Endpoint                              _joinTarget;
    std::chrono::steady_clock::time_point _joinDeadline{};
    std::function<void(bool, NetError)>   _onJoinResult;

    std::string _signalingUrl;
    std::string _signalingCaCert;
    std::string _roomCode; ///< See RoomCode(). Filled from BrokerEvent::LobbyCreated.

    /// See `SetIceServers`. Written from the broker's socket thread, read from the game's.
    mutable std::mutex     _iceMutex;
    std::vector<IceServer> _iceServers{ IceServer{ "stun:stun.l.google.com:19302" } };

    void _setIceServers(std::vector<IceServer> servers) {
        std::lock_guard<std::mutex> lock(_iceMutex);
        _iceServers = std::move(servers);
    }

    std::vector<IceServer> _getIceServers() const {
        std::lock_guard<std::mutex> lock(_iceMutex);
        return _iceServers;
    }

    std::unordered_map<uint32_t, std::function<void(Peer, const void *, uint32_t)>> _handlers;
    // Every registered packet's size, folded into the handshake. A struct that lays out
    // differently on the other side is caught before a single message is misread.
    std::map<uint32_t, uint32_t>                                                   _layouts;
    std::unordered_map<Peer, PlayerId>                                             _peerIds;
    std::function<void(Peer)>                                                      _onPeerJoined;
    std::function<void(Peer)>                                                      _onPeerLeft;
    std::function<void(const std::vector<SessionInfo> &)>                          _onSessions;
    std::function<void(const Endpoint &)>                                          _onInvite;

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
