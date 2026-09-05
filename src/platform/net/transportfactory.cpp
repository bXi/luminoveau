// Picks the one transport this build will use. On the web there is only ever one candidate.
// Natively, GNS wins when it is compiled in — encrypted, fragments large messages, and the
// same code path that reaches Steam P2P — with SDL_net as the fallback. WebRTC is not chosen
// automatically: it is the only transport that cannot connect without a signalling service,
// so a build that has it still has to ask. LUMI_NET_TRANSPORT forces any of them.

#include "platform/net/transports.h"
#include "core/log/log.h"

#include <cstdlib>
#include <cstring>

/// @cond INTERNAL
ITransport *createTransport(bool wantsSignaling) {
#ifdef LUMINOVEAU_WITH_WEBRTC
    // The only transport that reaches a browser, so on the web it is this or nothing.
    if (wantsSignaling)
        return createWebRtcTransport();
#else
    (void)wantsSignaling;
#endif


#if defined(__EMSCRIPTEN__)
    return createWebTransport();
#else
    const char *forced = std::getenv("LUMI_NET_TRANSPORT");
    if (forced) {
        if (std::strcmp(forced, "sdlnet") == 0)
            return createSdlNetTransport();
#ifdef LUMINOVEAU_WITH_GNS
        if (std::strcmp(forced, "gns") == 0)
            return createGnsTransport();
#endif
#ifdef LUMINOVEAU_WITH_WEBRTC
        if (std::strcmp(forced, "webrtc") == 0)
            return createWebRtcTransport();
#endif
        LOG_WARNING("Net: LUMI_NET_TRANSPORT='{}' is not a transport in this build", forced);
    }

#ifdef LUMINOVEAU_WITH_GNS
    return createGnsTransport();
#else
    return createSdlNetTransport();
#endif
#endif
}
/// @endcond
