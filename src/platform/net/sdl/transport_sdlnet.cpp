// Native networking backend (SDL3_net). Two parts:
//   1. Net::Udp — thin raw-datagram path (for protocols that bring their own reliability,
//      e.g. Quake's net_dgrm driver).
//   2. SdlNetTransport — the hybrid behind the typed Net:: API: TCP stream = reliable,
//      UDP datagram = unreliable. A tiny control handshake maps each client's UDP source
//      address to its TCP peer so unreliable traffic can be routed per peer.

#include "platform/net/net.h"
#include "platform/net/itransport.h"
#include "core/log/log.h"

#include <SDL3_net/SDL_net.h>

#include <unordered_map>
#include <vector>
#include <cstring>

namespace Net {

/// @cond INTERNAL
// SDL_net must be initialized before any socket/address call. Both the typed transport and
// the Net::Udp thin path go through this (Quake uses Udp without creating a transport).
static bool s_netInit = false;
static bool ensureNetInit() {
    if (!s_netInit) {
        if (!NET_Init()) { LOG_WARNING("Net: NET_Init failed: {}", SDL_GetError()); return false; }
        s_netInit = true;
    }
    return true;
}
/// @endcond

// ── Net::Udp: thin raw datagram path ──────────────────────────────────────────
namespace Udp {

Socket Open(uint16_t port) {
    if (!ensureNetInit()) return nullptr;
    return (Socket)NET_CreateDatagramSocket(nullptr, port, 0);   // any address; port 0 = ephemeral
}
void Close(Socket s) {
    if (s) NET_DestroyDatagramSocket((NET_DatagramSocket*)s);
}
bool Send(Socket s, const Address& to, const void* data, int len) {
    if (!s || !to.handle) return false;
    return NET_SendDatagram((NET_DatagramSocket*)s, (NET_Address*)to.handle, to.port, data, len);
}
int Recv(Socket s, Address& from, void* data, int maxLen) {
    if (!s) return -1;
    NET_Datagram* dg = nullptr;
    if (!NET_ReceiveDatagram((NET_DatagramSocket*)s, &dg)) return -1;
    if (!dg) return 0;                       // nothing waiting
    int n = dg->buflen < maxLen ? dg->buflen : maxLen;
    std::memcpy(data, dg->buf, n);
    from.handle = NET_RefAddress(dg->addr);
    from.port   = dg->port;
    NET_DestroyDatagram(dg);
    return n;
}
Address Resolve(const std::string& host, uint16_t port) {
    Address a;
    if (!ensureNetInit()) return a;
    NET_Address* addr = NET_ResolveHostname(host.c_str());
    if (!addr) return a;
    if (NET_WaitUntilResolved(addr, -1) == 1) { a.handle = addr; a.port = port; }
    else NET_UnrefAddress(addr);
    return a;
}
bool Valid(const Address& a) { return a.handle != nullptr; }
bool Equal(const Address& a, const Address& b) {
    if (!a.handle || !b.handle) return false;
    return a.port == b.port &&
           NET_CompareAddresses((NET_Address*)a.handle, (NET_Address*)b.handle) == 0;
}
std::string ToString(const Address& a) {
    if (!a.handle) return "?";
    const char* s = NET_GetAddressString((NET_Address*)a.handle);
    return std::string(s ? s : "?") + ":" + std::to_string(a.port);
}
void Free(Address& a) {
    if (a.handle) { NET_UnrefAddress((NET_Address*)a.handle); a.handle = nullptr; }
}

} // namespace Udp

// ── SdlNetTransport: hybrid TCP(reliable) + UDP(unreliable) ───────────────────
namespace {

// TCP frame:  [uint32 len][uint8 channel][payload(len-1)]   channel 0=control, 1=user
// UDP packet: [uint8 channel][payload]                      channel 0=hello,   1=user
enum : uint8_t { CH_CONTROL = 0, CH_USER = 1, CH_HELLO = 0 };

struct PeerConn {
    NET_StreamSocket* stream    = nullptr;
    NET_Address*      udpAddr   = nullptr;   // learned from the UDP hello
    uint16_t             udpPort   = 0;
    std::vector<uint8_t> rx;                     // TCP reassembly buffer
};

class SdlNetTransport : public ITransport {
public:
    ~SdlNetTransport() override { disconnect(); }

    bool host(uint16_t port) override {
        disconnect();
        m_server = NET_CreateServer(nullptr, port, 0);
        if (!m_server) { LOG_WARNING("Net: NET_CreateServer failed: {}", SDL_GetError()); return false; }
        m_udp = NET_CreateDatagramSocket(nullptr, port, 0);
        if (!m_udp) { LOG_WARNING("Net: server UDP socket failed: {}", SDL_GetError()); }
        m_isServer = true; m_port = port;
        return true;
    }

    bool connect(const std::string& address, uint16_t port) override {
        disconnect();
        NET_Address* addr = NET_ResolveHostname(address.c_str());
        if (!addr || NET_WaitUntilResolved(addr, -1) != 1) {
            LOG_WARNING("Net: resolve '{}' failed: {}", address, SDL_GetError());
            if (addr) NET_UnrefAddress(addr);
            return false;
        }
        m_clientConn.stream = NET_CreateClient(addr, port, 0);
        if (!m_clientConn.stream) {
            LOG_WARNING("Net: NET_CreateClient failed: {}", SDL_GetError());
            NET_UnrefAddress(addr); return false;
        }
        // Block briefly for the TCP handshake (simple first cut).
        NET_WaitUntilConnected(m_clientConn.stream, 5000);
        if (NET_GetConnectionStatus(m_clientConn.stream) != 1) {
            LOG_WARNING("Net: connect to {}:{} failed", address, port);
            NET_DestroyStreamSocket(m_clientConn.stream); m_clientConn.stream = nullptr;
            NET_UnrefAddress(addr); return false;
        }
        m_serverAddr = addr;                       // ref kept for UDP sends
        m_udp = NET_CreateDatagramSocket(nullptr, 0, 0);
        m_isClient = true; m_port = port;
        return true;
    }

    void disconnect() override {
        for (auto& [id, p] : m_peers) {
            if (p.stream)  NET_DestroyStreamSocket(p.stream);
            if (p.udpAddr) NET_UnrefAddress(p.udpAddr);
        }
        m_peers.clear();
        if (m_clientConn.stream) { NET_DestroyStreamSocket(m_clientConn.stream); m_clientConn.stream = nullptr; }
        m_clientConn.rx.clear();
        if (m_serverAddr) { NET_UnrefAddress(m_serverAddr); m_serverAddr = nullptr; }
        if (m_server) { NET_DestroyServer(m_server); m_server = nullptr; }
        if (m_udp)    { NET_DestroyDatagramSocket(m_udp); m_udp = nullptr; }
        m_isServer = m_isClient = false; m_selfId = 0; m_nextId = 1;
    }

    bool     isServer()  const override { return m_isServer; }
    bool     isClient()  const override { return m_isClient; }
    Peer     selfId()    const override { return m_selfId; }
    uint32_t peerCount() const override { return m_isServer ? (uint32_t)m_peers.size() : (m_clientConn.stream ? 1 : 0); }
    uint32_t ping(Peer)  const override { return 0; }   // TODO: RTT tracking

    void send(Peer peer, const void* data, uint32_t size, bool reliable) override {
        if (m_isServer) {
            auto it = m_peers.find(peer);
            if (it != m_peers.end()) sendTo(it->second, data, size, reliable);
        } else if (m_isClient) {
            sendToServer(data, size, reliable);
        }
    }

    void broadcast(const void* data, uint32_t size, bool reliable) override {
        if (m_isServer) for (auto& [id, p] : m_peers) sendTo(p, data, size, reliable);
        else if (m_isClient)                          sendToServer(data, size, reliable);
    }

    void poll(std::vector<TransportEvent>& out) override {
        if (m_isServer) pollServer(out);
        else if (m_isClient) pollClient(out);
    }

private:
    // ── framing helpers ──
    static void writeFrame(NET_StreamSocket* s, uint8_t channel, const void* data, uint32_t size) {
        if (!s) return;
        uint32_t len = size + 1;                 // channel byte + payload
        uint8_t hdr[5];
        std::memcpy(hdr, &len, 4); hdr[4] = channel;
        NET_WriteToStreamSocket(s, hdr, 5);
        if (size) NET_WriteToStreamSocket(s, data, (int)size);
    }

    void sendTo(PeerConn& p, const void* data, uint32_t size, bool reliable) {
        if (reliable) { writeFrame(p.stream, CH_USER, data, size); return; }
        if (m_udp && p.udpAddr) {
            std::vector<uint8_t> buf(size + 1); buf[0] = CH_USER;
            std::memcpy(buf.data() + 1, data, size);
            NET_SendDatagram(m_udp, p.udpAddr, p.udpPort, buf.data(), (int)buf.size());
        } else {
            writeFrame(p.stream, CH_USER, data, size);   // no UDP route yet → fall back to reliable
        }
    }

    void sendToServer(const void* data, uint32_t size, bool reliable) {
        if (reliable || !m_udp || !m_serverAddr) { writeFrame(m_clientConn.stream, CH_USER, data, size); return; }
        std::vector<uint8_t> buf(size + 1); buf[0] = CH_USER;
        std::memcpy(buf.data() + 1, data, size);
        NET_SendDatagram(m_udp, m_serverAddr, m_port, buf.data(), (int)buf.size());
    }

    // Pull bytes off a stream into rx, extract complete frames.
    void drainStream(PeerConn& p, Peer peer, std::vector<TransportEvent>& out, bool clientSide) {
        uint8_t tmp[4096];
        int n;
        while ((n = NET_ReadFromStreamSocket(p.stream, tmp, sizeof(tmp))) > 0)
            p.rx.insert(p.rx.end(), tmp, tmp + n);
        for (;;) {
            if (p.rx.size() < 4) break;
            uint32_t len; std::memcpy(&len, p.rx.data(), 4);
            if (p.rx.size() < 4 + len || len < 1) break;
            uint8_t channel = p.rx[4];
            const uint8_t* payload = p.rx.data() + 5;
            uint32_t plen = len - 1;
            if (channel == CH_USER) {
                TransportEvent ev; ev.type = TransportEvent::Receive; ev.peer = peer; ev.reliable = true;
                ev.data.assign(payload, payload + plen);
                out.push_back(std::move(ev));
            } else if (channel == CH_CONTROL && clientSide && plen >= 6) {
                std::memcpy(&m_selfId, payload, 4);                 // server-assigned id
                uint16_t serverUdpPort; std::memcpy(&serverUdpPort, payload + 4, 2);
                m_port = serverUdpPort;
                sendHello();                                       // open the UDP return path
            }
            p.rx.erase(p.rx.begin(), p.rx.begin() + 4 + len);
        }
    }

    void sendHello() {
        if (!m_udp || !m_serverAddr) return;
        uint8_t buf[5]; buf[0] = CH_HELLO; std::memcpy(buf + 1, &m_selfId, 4);
        NET_SendDatagram(m_udp, m_serverAddr, m_port, buf, sizeof(buf));
    }

    void pollServer(std::vector<TransportEvent>& out) {
        // accept new clients
        NET_StreamSocket* s = nullptr;
        while (NET_AcceptClient(m_server, &s) && s) {
            Peer id = m_nextId++;
            PeerConn pc; pc.stream = s;
            m_peers[id] = std::move(pc);
            // tell the client its id + our UDP port (control frame)
            uint8_t ctl[6]; std::memcpy(ctl, &id, 4); std::memcpy(ctl + 4, &m_port, 2);
            writeFrame(m_peers[id].stream, CH_CONTROL, ctl, sizeof(ctl));
            TransportEvent ev; ev.type = TransportEvent::Connect; ev.peer = id; out.push_back(ev);
            s = nullptr;
        }
        // read streams + detect disconnects
        std::vector<Peer> dead;
        for (auto& [id, p] : m_peers) {
            if (NET_GetConnectionStatus(p.stream) != 1) { dead.push_back(id); continue; }
            drainStream(p, id, out, false);
        }
        for (Peer id : dead) {
            TransportEvent ev; ev.type = TransportEvent::Disconnect; ev.peer = id; out.push_back(ev);
            auto& p = m_peers[id];
            if (p.stream)  NET_DestroyStreamSocket(p.stream);
            if (p.udpAddr) NET_UnrefAddress(p.udpAddr);
            m_peers.erase(id);
        }
        // UDP datagrams (hello → map addr to peer; user → receive)
        drainUdpServer(out);
    }

    void drainUdpServer(std::vector<TransportEvent>& out) {
        if (!m_udp) return;
        NET_Datagram* dg = nullptr;
        while (NET_ReceiveDatagram(m_udp, &dg) && dg) {
            if (dg->buflen >= 1) {
                uint8_t channel = dg->buf[0];
                if (channel == CH_HELLO && dg->buflen >= 5) {
                    Peer id; std::memcpy(&id, dg->buf + 1, 4);
                    auto it = m_peers.find(id);
                    if (it != m_peers.end()) {
                        if (it->second.udpAddr) NET_UnrefAddress(it->second.udpAddr);
                        it->second.udpAddr = NET_RefAddress(dg->addr);
                        it->second.udpPort = dg->port;
                    }
                } else if (channel == CH_USER) {
                    Peer from = peerByUdp(dg->addr, dg->port);
                    if (from) {
                        TransportEvent ev; ev.type = TransportEvent::Receive; ev.peer = from; ev.reliable = false;
                        ev.data.assign(dg->buf + 1, dg->buf + dg->buflen);
                        out.push_back(std::move(ev));
                    }
                }
            }
            NET_DestroyDatagram(dg); dg = nullptr;
        }
    }

    Peer peerByUdp(NET_Address* addr, uint16_t port) {
        for (auto& [id, p] : m_peers)
            if (p.udpAddr && p.udpPort == port && NET_CompareAddresses(p.udpAddr, addr) == 0) return id;
        return 0;
    }

    void pollClient(std::vector<TransportEvent>& out) {
        if (!m_clientConn.stream) return;
        if (NET_GetConnectionStatus(m_clientConn.stream) != 1) {
            TransportEvent ev; ev.type = TransportEvent::Disconnect; ev.peer = SERVER_PEER; out.push_back(ev);
            NET_DestroyStreamSocket(m_clientConn.stream); m_clientConn.stream = nullptr;
            return;
        }
        drainStream(m_clientConn, SERVER_PEER, out, true);
        // UDP from server
        if (m_udp) {
            NET_Datagram* dg = nullptr;
            while (NET_ReceiveDatagram(m_udp, &dg) && dg) {
                if (dg->buflen >= 1 && dg->buf[0] == CH_USER) {
                    TransportEvent ev; ev.type = TransportEvent::Receive; ev.peer = SERVER_PEER; ev.reliable = false;
                    ev.data.assign(dg->buf + 1, dg->buf + dg->buflen);
                    out.push_back(std::move(ev));
                }
                NET_DestroyDatagram(dg); dg = nullptr;
            }
        }
    }

    bool m_isServer = false, m_isClient = false;
    NET_Server*        m_server  = nullptr;
    NET_DatagramSocket* m_udp    = nullptr;
    NET_Address*       m_serverAddr = nullptr;   // client: server address for UDP
    uint16_t              m_port    = 0;
    Peer                  m_selfId  = 0;
    Peer                  m_nextId  = 1;
    PeerConn                       m_clientConn;     // client: connection to server
    std::unordered_map<Peer, PeerConn> m_peers;     // server: connected clients
};

} // namespace

/// @cond INTERNAL
ITransport* createTransport() {
    if (!ensureNetInit()) return nullptr;
    return new SdlNetTransport();
}
/// @endcond

} // namespace Net
