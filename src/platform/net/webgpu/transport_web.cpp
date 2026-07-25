// Web (emscripten) networking stub. Browsers can't do raw TCP/UDP, so the SDL_net backend
// isn't built here. This provides no-op implementations so the engine links on web; a real
// WebSocket/WebRTC transport can replace it later.

#include "platform/net/net.h"
#include "platform/net/itransport.h"
#include "core/log/log.h"

namespace {
class WebTransport : public ITransport {
public:
    bool Host(uint16_t) override {
        LOG_WARNING("Net: hosting unsupported on web");
        return false;
    }
    bool Connect(const std::string &, uint16_t) override {
        LOG_WARNING("Net: connect unsupported on web");
        return false;
    }
    void      Disconnect() override { }
    bool      IsServer() const override { return false; }
    bool      IsClient() const override { return false; }
    Net::Peer SelfId() const override { return 0; }
    uint32_t  PeerCount() const override { return 0; }
    uint32_t  Ping(Net::Peer) const override { return 0; }
    void      Send(Net::Peer, const void *, uint32_t, bool) override { }
    void      Broadcast(const void *, uint32_t, bool) override { }
    void      Poll(std::vector<TransportEvent> &) override { }
};
} // namespace

/// @cond INTERNAL
ITransport *createTransport() { return new WebTransport(); }
/// @endcond

// Thin UDP path — unavailable on web.

// ── Net::Udp: raw datagram path ───────────────────────────────────────────────
Net::Udp::Socket  Net::Udp::_open(uint16_t) { return nullptr; }
void              Net::Udp::_close(Net::Udp::Socket) { }
bool              Net::Udp::_send(Net::Udp::Socket, const Net::Udp::Address &, const void *, int) { return false; }
int               Net::Udp::_recv(Net::Udp::Socket, Net::Udp::Address &, void *, int) { return -1; }
Net::Udp::Address Net::Udp::_resolve(const std::string &, uint16_t) { return {}; }
bool              Net::Udp::_valid(const Net::Udp::Address &a) { return a.handle != nullptr; }
bool              Net::Udp::_equal(const Net::Udp::Address &, const Net::Udp::Address &) { return false; }
std::string       Net::Udp::_toString(const Net::Udp::Address &) { return "<no-net>"; }
void              Net::Udp::_free(Net::Udp::Address &) { }
