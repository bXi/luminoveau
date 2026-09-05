// Native networking backend (SDL3_net). Two parts:
//   1. Net::Udp — thin raw-datagram path (for protocols that bring their own reliability,
//      e.g. Quake's net_dgrm driver).
//   2. SdlNetTransport — the hybrid behind the typed Net:: API: TCP stream = reliable,
//      UDP datagram = unreliable. A tiny control handshake maps each client's UDP source
//      address to its TCP peer so unreliable traffic can be routed per peer.

#include "platform/net/net.h"
#include "platform/net/transports.h"
#include "core/log/log.h"

#include <SDL3_net/SDL_net.h>
#include <SDL3/SDL_properties.h>

#include <unordered_map>
#include <vector>
#include <cstring>

/// @cond INTERNAL
// SDL_net must be initialized before any socket/address call. Both the typed transport and
// the Net::Udp thin path go through this (Quake uses Udp without creating a transport).
static bool netInit = false;
static bool ensureNetInit() {
    if (!netInit) {
        if (!NET_Init()) {
            LOG_WARNING("Net: NET_Init failed: {}", SDL_GetError());
            return false;
        }
        netInit = true;
    }
    return true;
}
/// @endcond

// ── Net::Udp: thin raw datagram path ──────────────────────────────────────────

// ── SdlNetTransport: hybrid TCP(reliable) + UDP(unreliable) ───────────────────
namespace {

// TCP frame:  [uint32 len][uint8 channel][payload(len-1)]   channel 0=control, 1=user
// UDP packet: [uint8 channel][payload]                      channel 0=hello,   1=user
enum : uint8_t { ChControl = 0,
    ChUser                 = 1,
    ChHello                = 0 };

struct PeerConn {
    NET_StreamSocket    *stream  = nullptr;
    NET_Address         *udpAddr = nullptr; // learned from the UDP hello
    uint16_t             udpPort = 0;
    std::vector<uint8_t> rx; // TCP reassembly buffer
};

class SdlNetTransport : public ITransport {
public:
    ~SdlNetTransport() override { Disconnect(); }

    bool Host(const Net::HostConfig &cfg) override {
        Disconnect();
        _lastError = Net::NetError::None;
        _server    = NET_CreateServer(nullptr, cfg.port, 0);
        if (!_server) {
            LOG_WARNING("Net: NET_CreateServer failed: {}", SDL_GetError());
            _lastError = Net::NetError::NoTransport;
            return false;
        }
        _udp = NET_CreateDatagramSocket(nullptr, cfg.port, 0);
        if (!_udp) {
            LOG_WARNING("Net: server UDP socket failed: {}", SDL_GetError());
        }
        _isServer = true;
        _port     = cfg.port;
        return true;
    }

    bool Connect(const Net::Endpoint &ep) override {
        Disconnect();
        _lastError = Net::NetError::None;
        if (ep.kind != Net::Endpoint::Kind::Address) {
            // Lobbies, invites and join codes all need a brokerage this backend has no idea about.
            LOG_WARNING("Net: SDL_net can only connect to an address");
            _lastError = Net::NetError::BrokerUnavailable;
            return false;
        }
        NET_Address *addr = NET_ResolveHostname(ep.address.c_str());
        if (!addr || NET_WaitUntilResolved(addr, -1) != 1) {
            LOG_WARNING("Net: resolve '{}' failed: {}", ep.address, SDL_GetError());
            if (addr)
                NET_UnrefAddress(addr);
            _lastError = Net::NetError::HostUnreachable;
            return false;
        }
        _clientConn.stream = NET_CreateClient(addr, ep.port, 0);
        if (!_clientConn.stream) {
            LOG_WARNING("Net: NET_CreateClient failed: {}", SDL_GetError());
            NET_UnrefAddress(addr);
            _lastError = Net::NetError::HostUnreachable;
            return false;
        }
        // Block briefly for the TCP handshake (simple first cut).
        NET_WaitUntilConnected(_clientConn.stream, 5000);
        if (NET_GetConnectionStatus(_clientConn.stream) != 1) {
            LOG_WARNING("Net: connect to {}:{} failed", ep.address, ep.port);
            NET_DestroyStreamSocket(_clientConn.stream);
            _clientConn.stream = nullptr;
            NET_UnrefAddress(addr);
            _lastError = Net::NetError::Timeout;
            return false;
        }
        _serverAddr = addr; // ref kept for UDP sends
        _udp        = NET_CreateDatagramSocket(nullptr, 0, 0);
        _isClient   = true;
        _port       = ep.port;
        return true;
    }

    void DisconnectPeer(Net::Peer peer) override {
        auto it = _peers.find(peer);
        if (it == _peers.end())
            return;
        if (it->second.stream)
            NET_DestroyStreamSocket(it->second.stream);
        if (it->second.udpAddr)
            NET_UnrefAddress(it->second.udpAddr);
        _peers.erase(it);
    }

    void Disconnect() override {
        for (auto &[id, p] : _peers) {
            if (p.stream)
                NET_DestroyStreamSocket(p.stream);
            if (p.udpAddr)
                NET_UnrefAddress(p.udpAddr);
        }
        _peers.clear();
        if (_clientConn.stream) {
            NET_DestroyStreamSocket(_clientConn.stream);
            _clientConn.stream = nullptr;
        }
        _clientConn.rx.clear();
        if (_serverAddr) {
            NET_UnrefAddress(_serverAddr);
            _serverAddr = nullptr;
        }
        if (_server) {
            NET_DestroyServer(_server);
            _server = nullptr;
        }
        if (_udp) {
            NET_DestroyDatagramSocket(_udp);
            _udp = nullptr;
        }
        _isServer = _isClient = false;
        _selfId               = 0;
        _nextId               = 1;
    }

    bool      IsServer() const override { return _isServer; }
    bool      IsClient() const override { return _isClient; }
    Net::Peer SelfId() const override { return _selfId; }
    uint32_t  PeerCount() const override { return _isServer ? (uint32_t)_peers.size() : (_clientConn.stream ? 1 : 0); }
    uint32_t  Ping(Net::Peer) const override { return 0; } // TODO: RTT tracking

    // The TCP path frames with a uint32 length, so this is a buffering choice rather than a
    // protocol limit. Unreliable sends still have to fit a datagram — keep those near the MTU.
    uint32_t      MaxMessageSize() const override { return 64 * 1024; }
    Net::NetError LastError() const override { return _lastError; }

    void Send(Net::Peer peer, const void *data, uint32_t size, bool reliable) override {
        if (_isServer) {
            auto it = _peers.find(peer);
            if (it != _peers.end())
                _sendTo(it->second, data, size, reliable);
        } else if (_isClient) {
            _sendToServer(data, size, reliable);
        }
    }

    void Broadcast(const void *data, uint32_t size, bool reliable) override {
        if (_isServer)
            for (auto &[id, p] : _peers)
                _sendTo(p, data, size, reliable);
        else if (_isClient)
            _sendToServer(data, size, reliable);
    }

    void Poll(std::vector<TransportEvent> &out) override {
        if (_isServer)
            _pollServer(out);
        else if (_isClient)
            _pollClient(out);
    }

private:
    // ── framing helpers ──
    void _writeFrame(NET_StreamSocket *s, uint8_t channel, const void *data, uint32_t size) {
        if (!s)
            return;
        uint32_t len = size + 1; // channel byte + payload
        uint8_t  hdr[5];
        std::memcpy(hdr, &len, 4);
        hdr[4] = channel;
        NET_WriteToStreamSocket(s, hdr, 5);
        if (size)
            NET_WriteToStreamSocket(s, data, (int)size);
    }

    void _sendTo(PeerConn &p, const void *data, uint32_t size, bool reliable) {
        if (reliable) {
            _writeFrame(p.stream, ChUser, data, size);
            return;
        }
        if (_udp && p.udpAddr) {
            std::vector<uint8_t> buf(size + 1);
            buf[0] = ChUser;
            std::memcpy(buf.data() + 1, data, size);
            NET_SendDatagram(_udp, p.udpAddr, p.udpPort, buf.data(), (int)buf.size());
        } else {
            _writeFrame(p.stream, ChUser, data, size); // no UDP route yet → fall back to reliable
        }
    }

    void _sendToServer(const void *data, uint32_t size, bool reliable) {
        if (reliable || !_udp || !_serverAddr) {
            _writeFrame(_clientConn.stream, ChUser, data, size);
            return;
        }
        std::vector<uint8_t> buf(size + 1);
        buf[0] = ChUser;
        std::memcpy(buf.data() + 1, data, size);
        NET_SendDatagram(_udp, _serverAddr, _port, buf.data(), (int)buf.size());
    }

    // Pull bytes off a stream into rx, extract complete frames.
    void _drainStream(PeerConn &p, Net::Peer peer, std::vector<TransportEvent> &out, bool clientSide) {
        uint8_t tmp[4096];
        int     n;
        while ((n = NET_ReadFromStreamSocket(p.stream, tmp, sizeof(tmp))) > 0)
            p.rx.insert(p.rx.end(), tmp, tmp + n);
        for (;;) {
            if (p.rx.size() < 4)
                break;
            uint32_t len;
            std::memcpy(&len, p.rx.data(), 4);
            if (p.rx.size() < 4 + len || len < 1)
                break;
            uint8_t        channel = p.rx[4];
            const uint8_t *payload = p.rx.data() + 5;
            uint32_t       plen    = len - 1;
            if (channel == ChUser) {
                TransportEvent ev;
                ev.type     = TransportEvent::Receive;
                ev.peer     = peer;
                ev.reliable = true;
                ev.data.assign(payload, payload + plen);
                out.push_back(std::move(ev));
            } else if (channel == ChControl && clientSide && plen >= 6) {
                std::memcpy(&_selfId, payload, 4); // server-assigned id
                uint16_t serverUdpPort;
                std::memcpy(&serverUdpPort, payload + 4, 2);
                _port = serverUdpPort;
                _sendHello(); // open the UDP return path
            }
            p.rx.erase(p.rx.begin(), p.rx.begin() + 4 + len);
        }
    }

    void _sendHello() {
        if (!_udp || !_serverAddr)
            return;
        uint8_t buf[5];
        buf[0] = ChHello;
        std::memcpy(buf + 1, &_selfId, 4);
        NET_SendDatagram(_udp, _serverAddr, _port, buf, sizeof(buf));
    }

    void _pollServer(std::vector<TransportEvent> &out) {
        // accept new clients
        NET_StreamSocket *s = nullptr;
        while (NET_AcceptClient(_server, &s) && s) {
            Net::Peer id = _nextId++;
            PeerConn  pc;
            pc.stream  = s;
            _peers[id] = std::move(pc);
            // tell the client its id + our UDP port (control frame)
            uint8_t ctl[6];
            std::memcpy(ctl, &id, 4);
            std::memcpy(ctl + 4, &_port, 2);
            _writeFrame(_peers[id].stream, ChControl, ctl, sizeof(ctl));
            TransportEvent ev;
            ev.type = TransportEvent::Connect;
            ev.peer = id;
            out.push_back(ev);
            s = nullptr;
        }
        // read streams + detect disconnects
        std::vector<Net::Peer> dead;
        for (auto &[id, p] : _peers) {
            if (NET_GetConnectionStatus(p.stream) != 1) {
                dead.push_back(id);
                continue;
            }
            _drainStream(p, id, out, false);
        }
        for (Net::Peer id : dead) {
            TransportEvent ev;
            ev.type = TransportEvent::Disconnect;
            ev.peer = id;
            out.push_back(ev);
            auto &p = _peers[id];
            if (p.stream)
                NET_DestroyStreamSocket(p.stream);
            if (p.udpAddr)
                NET_UnrefAddress(p.udpAddr);
            _peers.erase(id);
        }
        // UDP datagrams (hello → map addr to peer; user → receive)
        _drainUdpServer(out);
    }

    void _drainUdpServer(std::vector<TransportEvent> &out) {
        if (!_udp)
            return;
        NET_Datagram *dg = nullptr;
        while (NET_ReceiveDatagram(_udp, &dg) && dg) {
            if (dg->buflen >= 1) {
                uint8_t channel = dg->buf[0];
                if (channel == ChHello && dg->buflen >= 5) {
                    Net::Peer id;
                    std::memcpy(&id, dg->buf + 1, 4);
                    auto it = _peers.find(id);
                    if (it != _peers.end()) {
                        if (it->second.udpAddr)
                            NET_UnrefAddress(it->second.udpAddr);
                        it->second.udpAddr = NET_RefAddress(dg->addr);
                        it->second.udpPort = dg->port;
                    }
                } else if (channel == ChUser) {
                    Net::Peer from = _peerByUdp(dg->addr, dg->port);
                    if (from) {
                        TransportEvent ev;
                        ev.type     = TransportEvent::Receive;
                        ev.peer     = from;
                        ev.reliable = false;
                        ev.data.assign(dg->buf + 1, dg->buf + dg->buflen);
                        out.push_back(std::move(ev));
                    }
                }
            }
            NET_DestroyDatagram(dg);
            dg = nullptr;
        }
    }

    Net::Peer _peerByUdp(NET_Address *addr, uint16_t port) {
        for (auto &[id, p] : _peers)
            if (p.udpAddr && p.udpPort == port && NET_CompareAddresses(p.udpAddr, addr) == 0)
                return id;
        return 0;
    }

    void _pollClient(std::vector<TransportEvent> &out) {
        if (!_clientConn.stream)
            return;
        if (NET_GetConnectionStatus(_clientConn.stream) != 1) {
            TransportEvent ev;
            ev.type = TransportEvent::Disconnect;
            ev.peer = Net::SERVER_PEER;
            out.push_back(ev);
            NET_DestroyStreamSocket(_clientConn.stream);
            _clientConn.stream = nullptr;
            return;
        }
        _drainStream(_clientConn, Net::SERVER_PEER, out, true);
        // UDP from server
        if (_udp) {
            NET_Datagram *dg = nullptr;
            while (NET_ReceiveDatagram(_udp, &dg) && dg) {
                if (dg->buflen >= 1 && dg->buf[0] == ChUser) {
                    TransportEvent ev;
                    ev.type     = TransportEvent::Receive;
                    ev.peer     = Net::SERVER_PEER;
                    ev.reliable = false;
                    ev.data.assign(dg->buf + 1, dg->buf + dg->buflen);
                    out.push_back(std::move(ev));
                }
                NET_DestroyDatagram(dg);
                dg = nullptr;
            }
        }
    }

    Net::NetError                           _lastError = Net::NetError::None;
    bool                                    _isServer = false, _isClient = false;
    NET_Server                             *_server     = nullptr;
    NET_DatagramSocket                     *_udp        = nullptr;
    NET_Address                            *_serverAddr = nullptr; // client: server address for UDP
    uint16_t                                _port       = 0;
    Net::Peer                               _selfId     = 0;
    Net::Peer                               _nextId     = 1;
    PeerConn                                _clientConn; // client: connection to server
    std::unordered_map<Net::Peer, PeerConn> _peers;      // server: connected clients
};

} // namespace

/// @cond INTERNAL
ITransport *createSdlNetTransport() {
    if (!ensureNetInit())
        return nullptr;
    return new SdlNetTransport();
}
/// @endcond

// ── Net::Udp: raw datagram path ───────────────────────────────────────────────
Net::Udp::Socket Net::Udp::_open(uint16_t port) {
    if (!ensureNetInit())
        return nullptr;
    return (Net::Udp::Socket)NET_CreateDatagramSocket(nullptr, port, 0); // any address; port 0 = ephemeral
}
Net::Udp::Socket Net::Udp::_openBroadcast(uint16_t port) {
    if (!ensureNetInit())
        return nullptr;

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, NET_PROP_DATAGRAM_SOCKET_ALLOW_BROADCAST_BOOLEAN, true);

    // A null address asks SDL_net for one socket per family, which is what we want: IPv4 gets a
    // real broadcast and IPv6 gets SDL_net's stand-in for one.
    NET_DatagramSocket *sock = NET_CreateDatagramSocket(nullptr, port, props);

    // **The IPv6 half fails outright on BSD, and takes the IPv4 half down with it.**
    //
    // SDL_net fakes IPv6 broadcast by joining the all-nodes multicast group ff02::1 with
    // `ipv6mr_interface = 0`, meaning "pick a default interface". That group is *link-local*, and
    // BSD requires a link-local join to name its interface explicitly — so the setsockopt fails
    // on macOS where Linux and Windows accept it. SDL_net treats that as fatal for the whole
    // socket, so the perfectly good IPv4 handle is destroyed alongside it and this returns null.
    //
    // The symptom is a mac that cannot browse or advertise on a LAN while direct connections work
    // perfectly, since those never touch a datagram socket.
    //
    // So: fall back to a single IPv4 interface, which skips the IPv6 handle and the multicast
    // join with it. Nothing is lost — discovery is IPv4 broadcast on every platform, and the IPv6
    // path exists solely to stand in for a broadcast that IPv6 does not have.
    //
    // **A real interface address, not `0.0.0.0`.** Naming an address makes SDL_net insist on
    // finding that interface's broadcast address, and `INADDR_ANY` has none — which is the very
    // case the null-address path exists to handle, so asking for it explicitly fails the same
    // way. The first non-loopback IPv4 address is the LAN this machine is on.
    if (!sock) {
        int           count     = 0;
        NET_Address **addresses = NET_GetLocalAddresses(&count);

        for (int i = 0; i < count && !sock; ++i) {
            const char *text = NET_GetAddressString(addresses[i]);
            if (!text)
                continue;
            // IPv4, and not the loopback: a socket bound to 127.0.0.1 broadcasts to nobody.
            if (std::strchr(text, ':') || std::strncmp(text, "127.", 4) == 0)
                continue;
            sock = NET_CreateDatagramSocket(addresses[i], port, props);
            if (sock)
                LOG_INFO("Net: IPv6 broadcast unavailable here, using IPv4 on {}", text);
        }

        NET_FreeLocalAddresses(addresses);
    }

    SDL_DestroyProperties(props);
    return (Net::Udp::Socket)sock;
}
void Net::Udp::_close(Net::Udp::Socket s) {
    if (s)
        NET_DestroyDatagramSocket((NET_DatagramSocket *)s);
}
bool Net::Udp::_send(Net::Udp::Socket s, const Net::Udp::Address &to, const void *data, int len) {
    if (!s || !to.handle)
        return false;
    return NET_SendDatagram((NET_DatagramSocket *)s, (NET_Address *)to.handle, to.port, data, len);
}
bool Net::Udp::_broadcast(Net::Udp::Socket s, uint16_t port, const void *data, int len) {
    if (!s)
        return false;
    // SDL_net reads a null address as "send this to the whole subnet".
    return NET_SendDatagram((NET_DatagramSocket *)s, nullptr, port, data, len);
}
int Net::Udp::_recv(Net::Udp::Socket s, Net::Udp::Address &from, void *data, int maxLen) {
    if (!s)
        return -1;
    NET_Datagram *dg = nullptr;
    if (!NET_ReceiveDatagram((NET_DatagramSocket *)s, &dg))
        return -1;
    if (!dg)
        return 0; // nothing waiting
    int n = dg->buflen < maxLen ? dg->buflen : maxLen;
    std::memcpy(data, dg->buf, n);
    from.handle = NET_RefAddress(dg->addr);
    from.port   = dg->port;
    NET_DestroyDatagram(dg);
    return n;
}
Net::Udp::Address Net::Udp::_resolve(const std::string &host, uint16_t port) {
    Net::Udp::Address a;
    if (!ensureNetInit())
        return a;
    NET_Address *addr = NET_ResolveHostname(host.c_str());
    if (!addr)
        return a;
    if (NET_WaitUntilResolved(addr, -1) == 1) {
        a.handle = addr;
        a.port   = port;
    } else
        NET_UnrefAddress(addr);
    return a;
}
bool Net::Udp::_valid(const Net::Udp::Address &a) { return a.handle != nullptr; }
bool Net::Udp::_equal(const Net::Udp::Address &a, const Net::Udp::Address &b) {
    if (!a.handle || !b.handle)
        return false;
    return a.port == b.port && NET_CompareAddresses((NET_Address *)a.handle, (NET_Address *)b.handle) == 0;
}
std::string Net::Udp::_toString(const Net::Udp::Address &a) {
    if (!a.handle)
        return "?";
    const char *s = NET_GetAddressString((NET_Address *)a.handle);
    return std::string(s ? s : "?") + ":" + std::to_string(a.port);
}
void Net::Udp::_free(Net::Udp::Address &a) {
    if (a.handle) {
        NET_UnrefAddress((NET_Address *)a.handle);
        a.handle = nullptr;
    }
}
