// Transport on ISteamNetworkingSockets — encrypted, reliable, and the same interface whether
// the library underneath is GameNetworkingSockets (BSD, direct IP) or the Steamworks SDK
// (SteamIDs, punch, SDR relay). Which one is decided at configure time, because both ship a
// header called steam/steamnetworkingtypes.h and only one may be on the include path.
//
// Under the Steamworks SDK it also speaks native P2P, addressed by SteamID rather than by
// address. That is the path that gets SDR relay, so a peer who cannot be punched through
// still connects instead of failing.

#include "platform/net/transports.h"
#include "core/log/log.h"
#include "util/helpers.h"

#ifdef STEAMNETWORKINGSOCKETS_STANDALONELIB
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingsockets.h>
#else
#include "isteamnetworkingsockets.h"
#include "isteamnetworkingutils.h"
#include "steam_api.h"
#endif

#include <chrono>
#include <thread>
#include <unordered_map>

// An SDK of the wrong vintage is an ABI mismatch on a vtable, which is the worst failure mode
// available, so it is caught here instead. The two libraries do not agree on this string —
// GameNetworkingSockets still ships 012 while the Steamworks SDK ships 013 — so each path
// checks its own. Both expose the calls this file uses; only the accessor name differs, and
// that is resolved by the unqualified SteamNetworkingSockets() each header declares.
constexpr bool sameInterfaceVersion(const char *a, const char *b) {
    return *a == *b && (*a == '\0' || sameInterfaceVersion(a + 1, b + 1));
}

#ifdef STEAMNETWORKINGSOCKETS_STANDALONELIB
static_assert(sameInterfaceVersion(STEAMNETWORKINGSOCKETS_INTERFACE_VERSION, "SteamNetworkingSockets012"),
    "This transport is written against GameNetworkingSockets' SteamNetworkingSockets012");
#else
static_assert(sameInterfaceVersion(STEAMNETWORKINGSOCKETS_INTERFACE_VERSION, "SteamNetworkingSockets013"),
    "This transport is written against the Steamworks SDK's SteamNetworkingSockets013");
#endif

namespace {

constexpr auto CONNECT_TIMEOUT = std::chrono::seconds(10);
constexpr auto CONNECT_POLL    = std::chrono::milliseconds(5);

class GnsTransport : public ITransport {
public:
    ~GnsTransport() override {
        Disconnect();
        _shutdownLibrary();
    }

    bool Host(const Net::HostConfig &cfg) override {
        Disconnect();
        if (!_startLibrary())
            return false;

        SteamNetworkingIPAddr local;
        local.Clear(); // any address; IPv6 here also accepts IPv4-mapped clients
        local.m_port = cfg.port;

        _listen = _sockets->CreateListenSocketIP(local, 0, nullptr);

#ifdef STEAMNETWORKINGSOCKETS_STEAMAPI
        // A second listen socket, not an alternative one: the same host can be reached by
        // address on the LAN and by SteamID from a friends list, and neither should exclude
        // the other. Connections from both land in the same poll group.
        _listenP2P = _sockets->CreateListenSocketP2P(0, 0, nullptr);
#endif

        if (_listen == k_HSteamListenSocket_Invalid && _listenP2P == k_HSteamListenSocket_Invalid) {
            LOG_WARNING("Net: GNS could not listen on port {}", cfg.port);
            _lastError = Net::NetError::NoTransport;
            return false;
        }
        _pollGroup = _sockets->CreatePollGroup();
        _isServer  = true;
        return true;
    }

    bool Connect(const Net::Endpoint &ep) override {
        Disconnect();
        // Lobbies and join codes are resolved to one of these two by Net before we see them.
        if (ep.kind != Net::Endpoint::Kind::Address && ep.kind != Net::Endpoint::Kind::Player) {
            LOG_WARNING("Net: GNS needs an address or a player, not a lobby");
            _lastError = Net::NetError::BrokerUnavailable;
            return false;
        }
        if (!_startLibrary())
            return false;

        _clientState = k_ESteamNetworkingConnectionState_Connecting;
        if (ep.kind == Net::Endpoint::Kind::Player) {
            _conn = _connectToPlayer(ep.player);
        } else {
            SteamNetworkingIPAddr remote;
            if (!_resolve(ep.address, ep.port, remote)) {
                LOG_WARNING("Net: could not resolve '{}'", ep.address);
                _lastError = Net::NetError::HostUnreachable;
                return false;
            }
            _conn = _sockets->ConnectByIPAddress(remote, 0, nullptr);
        }
        if (_conn == k_HSteamNetConnection_Invalid) {
            _lastError = Net::NetError::HostUnreachable;
            return false;
        }
        _isClient = true;

        // ITransport promises a connected socket on return, so block the way the SDL_net
        // backend does rather than making every caller cope with a half-open connection.
        const auto deadline = std::chrono::steady_clock::now() + CONNECT_TIMEOUT;
        while (_clientState == k_ESteamNetworkingConnectionState_Connecting) {
            _sockets->RunCallbacks();
            if (std::chrono::steady_clock::now() > deadline) {
                LOG_WARNING("Net: connect to {}:{} timed out", ep.address, ep.port);
                _lastError = Net::NetError::Timeout;
                Disconnect();
                return false;
            }
            std::this_thread::sleep_for(CONNECT_POLL);
        }
        if (_clientState != k_ESteamNetworkingConnectionState_Connected) {
            LOG_WARNING("Net: connect to {}:{} refused", ep.address, ep.port);
            _lastError = _closeReason;
            Disconnect();
            return false;
        }
        return true;
    }

    void Disconnect() override {
        if (_sockets) {
            for (const auto &[peer, conn] : _peers)
                _sockets->CloseConnection(conn, 0, "session closed", false);
            if (_conn != k_HSteamNetConnection_Invalid)
                _sockets->CloseConnection(_conn, 0, "session closed", false);
            if (_pollGroup != k_HSteamNetPollGroup_Invalid)
                _sockets->DestroyPollGroup(_pollGroup);
            if (_listen != k_HSteamListenSocket_Invalid)
                _sockets->CloseListenSocket(_listen);
            if (_listenP2P != k_HSteamListenSocket_Invalid)
                _sockets->CloseListenSocket(_listenP2P);
        }
        _peers.clear();
        _byConnection.clear();
        _pending.clear();
        _conn        = k_HSteamNetConnection_Invalid;
        _pollGroup   = k_HSteamNetPollGroup_Invalid;
        _listen      = k_HSteamListenSocket_Invalid;
        _listenP2P   = k_HSteamListenSocket_Invalid;
        _isServer    = _isClient = false;
        _nextId      = 1;
        _clientState = k_ESteamNetworkingConnectionState_None;
    }

    void DisconnectPeer(Net::Peer peer) override {
        auto it = _peers.find(peer);
        if (it == _peers.end())
            return;
        // Linger so a rejection written just before this actually reaches the other side.
        _sockets->CloseConnection(it->second, 0, "refused", true);
        _byConnection.erase(it->second);
        _peers.erase(it);
    }

    bool      IsServer() const override { return _isServer; }
    bool      IsClient() const override { return _isClient; }
    Net::Peer SelfId() const override { return _selfId; }
    uint32_t  PeerCount() const override {
        return _isServer ? (uint32_t)_peers.size() : (_conn != k_HSteamNetConnection_Invalid ? 1 : 0);
    }

    uint32_t Ping(Net::Peer peer) const override {
        const HSteamNetConnection conn = _connectionFor(peer);
        if (conn == k_HSteamNetConnection_Invalid)
            return 0;
        SteamNetConnectionRealTimeStatus_t status;
        if (_sockets->GetConnectionRealTimeStatus(conn, &status, 0, nullptr) != k_EResultOK)
            return 0;
        return status.m_nPing < 0 ? 0 : (uint32_t)status.m_nPing;
    }

    // GNS fragments and reassembles reliable messages itself, so this is its own cap rather
    // than anything to do with the path MTU.
    uint32_t      MaxMessageSize() const override { return k_cbMaxSteamNetworkingSocketsMessageSizeSend; }
    Net::NetError LastError() const override { return _lastError; }

    void Send(Net::Peer peer, const void *data, uint32_t size, bool reliable) override {
        const HSteamNetConnection conn = _connectionFor(peer);
        if (conn != k_HSteamNetConnection_Invalid)
            _send(conn, data, size, reliable);
    }

    void Broadcast(const void *data, uint32_t size, bool reliable) override {
        if (_isServer) {
            for (const auto &[peer, conn] : _peers)
                _send(conn, data, size, reliable);
        } else if (_conn != k_HSteamNetConnection_Invalid) {
            _send(_conn, data, size, reliable);
        }
    }

    void Poll(std::vector<TransportEvent> &out) override {
        if (!_sockets)
            return;
        // Under the Steamworks SDK this is also driven by SteamAPI_RunCallbacks; calling it
        // here as well costs an empty queue walk and means the transport works on its own.
        _sockets->RunCallbacks();

        for (TransportEvent &ev : _pending)
            out.push_back(std::move(ev));
        _pending.clear();

        SteamNetworkingMessage_t *messages[32];
        int                       count;
        do {
            count = _isServer ? _sockets->ReceiveMessagesOnPollGroup(_pollGroup, messages, 32)
                              : (_conn != k_HSteamNetConnection_Invalid
                                        ? _sockets->ReceiveMessagesOnConnection(_conn, messages, 32)
                                        : 0);
            for (int i = 0; i < count; ++i) {
                SteamNetworkingMessage_t *msg = messages[i];
                TransportEvent            ev;
                ev.type     = TransportEvent::Receive;
                ev.peer     = _peerFor(msg->m_conn);
                ev.reliable = (msg->m_nFlags & k_nSteamNetworkingSend_Reliable) != 0;
                const auto *bytes = (const uint8_t *)msg->m_pData;
                ev.data.assign(bytes, bytes + msg->m_cbSize);
                out.push_back(std::move(ev));
                msg->Release();
            }
        } while (count == 32);
    }

    // Called from the library's callback dispatch, which runs on whichever thread pumped it.
    void OnStatusChanged(SteamNetConnectionStatusChangedCallback_t *info) {
        switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting:
            // Only inbound connections arrive here with a listen socket attached.
            if (info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid)
                _acceptInbound(info->m_hConn);
            break;

        case k_ESteamNetworkingConnectionState_Connected:
            if (info->m_hConn == _conn)
                _clientState = k_ESteamNetworkingConnectionState_Connected;
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            _onClosed(info);
            break;

        default:
            break;
        }
    }

private:
    void _acceptInbound(HSteamNetConnection conn) {
        if (_sockets->AcceptConnection(conn) != k_EResultOK) {
            _sockets->CloseConnection(conn, 0, nullptr, false);
            return;
        }
        _sockets->SetConnectionPollGroup(conn, _pollGroup);

        const Net::Peer peer = _nextId++;
        _peers[peer]         = conn;
        _byConnection[conn]  = peer;

        TransportEvent ev;
        ev.type = TransportEvent::Connect;
        ev.peer = peer;
        _pending.push_back(std::move(ev));
    }

    void _onClosed(SteamNetConnectionStatusChangedCallback_t *info) {
        const HSteamNetConnection conn = info->m_hConn;

        if (conn == _conn) {
            // A connect still in progress reports why here, and nowhere else.
            _closeReason = info->m_info.m_eEndReason == k_ESteamNetConnectionEnd_Misc_Timeout
                                 ? Net::NetError::Timeout
                                 : Net::NetError::Refused;
            _clientState = info->m_info.m_eState;
            if (_isClient) {
                TransportEvent ev;
                ev.type = TransportEvent::Disconnect;
                ev.peer = Net::SERVER_PEER;
                _pending.push_back(std::move(ev));
            }
            _sockets->CloseConnection(conn, 0, nullptr, false);
            _conn = k_HSteamNetConnection_Invalid;
            return;
        }

        auto it = _byConnection.find(conn);
        if (it != _byConnection.end()) {
            TransportEvent ev;
            ev.type = TransportEvent::Disconnect;
            ev.peer = it->second;
            _pending.push_back(std::move(ev));
            _peers.erase(it->second);
            _byConnection.erase(it);
        }
        _sockets->CloseConnection(conn, 0, nullptr, false);
    }

    void _send(HSteamNetConnection conn, const void *data, uint32_t size, bool reliable) {
        const int flags = reliable ? k_nSteamNetworkingSend_Reliable
                                   : k_nSteamNetworkingSend_UnreliableNoDelay;
        _sockets->SendMessageToConnection(conn, data, size, flags, nullptr);
    }

    HSteamNetConnection _connectionFor(Net::Peer peer) const {
        if (_isClient)
            return _conn;
        auto it = _peers.find(peer);
        return it == _peers.end() ? k_HSteamNetConnection_Invalid : it->second;
    }

    Net::Peer _peerFor(HSteamNetConnection conn) const {
        auto it = _byConnection.find(conn);
        return it == _byConnection.end() ? Net::SERVER_PEER : it->second;
    }

    HSteamNetConnection _connectToPlayer(const PlayerId &player) {
#ifdef STEAMNETWORKINGSOCKETS_STEAMAPI
        if (player.provider != PlayerId::Provider::Steam || player.length != sizeof(uint64_t)) {
            _lastError = Net::NetError::BrokerUnavailable;
            return k_HSteamNetConnection_Invalid;
        }
        uint64_t raw = 0;
        for (int i = 0; i < 8; ++i)
            raw |= (uint64_t)player.bytes[i] << (i * 8);

        SteamNetworkingIdentity identity;
        identity.Clear();
        identity.SetSteamID64(raw);
        return _sockets->ConnectP2P(identity, 0, 0, nullptr);
#else
        // Standalone GNS can reach a peer by identity, but only with signalling behind it,
        // which is a later phase. Say so rather than failing as if the peer were offline.
        LUMI_UNUSED(player);
        LOG_WARNING("Net: connecting to a player needs a brokerage that carries signalling");
        _lastError = Net::NetError::BrokerUnavailable;
        return k_HSteamNetConnection_Invalid;
#endif
    }

    static bool _resolve(const std::string &address, uint16_t port, SteamNetworkingIPAddr &out) {
        out.Clear();
        if (out.ParseString(address.c_str())) {
            out.m_port = port;
            return true;
        }
        // GNS speaks addresses, not names, so borrow the resolver the raw UDP path already has.
        Net::Udp::Address resolved = Net::Udp::Resolve(address, port);
        if (!Net::Udp::Valid(resolved))
            return false;
        std::string text = Net::Udp::ToString(resolved);
        Net::Udp::Free(resolved);
        text = text.substr(0, text.rfind(':')); // ToString always appends ":port"
        if (!out.ParseString(text.c_str()))
            return false;
        out.m_port = port;
        return true;
    }

    bool _startLibrary();
    void _shutdownLibrary();

    ISteamNetworkingSockets *_sockets   = nullptr;
    HSteamListenSocket       _listen    = k_HSteamListenSocket_Invalid;
    HSteamListenSocket       _listenP2P = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup       _pollGroup = k_HSteamNetPollGroup_Invalid;
    HSteamNetConnection      _conn      = k_HSteamNetConnection_Invalid; // client: to the server

    std::unordered_map<Net::Peer, HSteamNetConnection> _peers;
    std::unordered_map<HSteamNetConnection, Net::Peer> _byConnection;
    std::vector<TransportEvent>                        _pending;

    bool          _isServer = false, _isClient = false;
    Net::Peer     _selfId    = 0;
    Net::Peer     _nextId    = 1;
    Net::NetError _lastError = Net::NetError::None;

    ESteamNetworkingConnectionState _clientState = k_ESteamNetworkingConnectionState_None;
    Net::NetError                   _closeReason = Net::NetError::Refused;
};

// The library hands status changes to a plain function pointer, so the one live transport
// has to be reachable from file scope.
GnsTransport *activeTransport = nullptr;

void onStatusChanged(SteamNetConnectionStatusChangedCallback_t *info) {
    if (activeTransport)
        activeTransport->OnStatusChanged(info);
}

bool GnsTransport::_startLibrary() {
    if (_sockets)
        return true;

#ifdef STEAMNETWORKINGSOCKETS_STANDALONELIB
    SteamNetworkingErrMsg errMsg;
    if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
        LOG_WARNING("Net: GameNetworkingSockets_Init failed: {}", errMsg);
        _lastError = Net::NetError::NoTransport;
        return false;
    }
#endif

    _sockets = SteamNetworkingSockets();
    if (!_sockets) {
        // Under the Steamworks SDK this means Steam::Init has not run or Steam is not there.
        LOG_WARNING("Net: no ISteamNetworkingSockets — is Steam running and initialised?");
        _lastError = Net::NetError::NoTransport;
        return false;
    }

    activeTransport = this;
    SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(onStatusChanged);
    return true;
}

void GnsTransport::_shutdownLibrary() {
    if (activeTransport == this)
        activeTransport = nullptr;
    if (!_sockets)
        return;
    _sockets = nullptr;
#ifdef STEAMNETWORKINGSOCKETS_STANDALONELIB
    GameNetworkingSockets_Kill();
#endif
}

} // namespace

/// @cond INTERNAL
ITransport *createGnsTransport() {
    return new GnsTransport();
}
/// @endcond
