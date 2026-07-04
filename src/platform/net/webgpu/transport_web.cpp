// Web (emscripten) networking stub. Browsers can't do raw TCP/UDP, so the SDL_net backend
// isn't built here. This provides no-op implementations so the engine links on web; a real
// WebSocket/WebRTC transport can replace it later.

#include "platform/net/net.h"
#include "platform/net/itransport.h"
#include "core/log/log.h"

namespace Net {

namespace {
class WebTransport : public ITransport {
public:
    bool host(uint16_t)                       override { LOG_WARNING("Net: hosting unsupported on web"); return false; }
    bool connect(const std::string&, uint16_t) override { LOG_WARNING("Net: connect unsupported on web"); return false; }
    void disconnect()                         override {}
    bool     isServer()  const override { return false; }
    bool     isClient()  const override { return false; }
    Peer     selfId()    const override { return 0; }
    uint32_t peerCount() const override { return 0; }
    uint32_t ping(Peer)  const override { return 0; }
    void send(Peer, const void*, uint32_t, bool)  override {}
    void broadcast(const void*, uint32_t, bool)   override {}
    void poll(std::vector<TransportEvent>&)       override {}
};
}

/// @cond INTERNAL
ITransport* createTransport() { return new WebTransport(); }
/// @endcond

// Thin UDP path — unavailable on web.
namespace Udp {
    Socket Open(uint16_t)                                  { return nullptr; }
    void   Close(Socket)                                  {}
    bool   Send(Socket, const Address&, const void*, int) { return false; }
    int    Recv(Socket, Address&, void*, int)             { return -1; }
    Address     Resolve(const std::string&, uint16_t)     { return {}; }
    bool        Valid(const Address& a)                   { return a.handle != nullptr; }
    bool        Equal(const Address&, const Address&)     { return false; }
    std::string ToString(const Address&)                  { return "<no-net>"; }
    void        Free(Address&)                            {}
}

} // namespace Net
