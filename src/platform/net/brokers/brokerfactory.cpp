#include "platform/net/brokers/brokers.h"
#include "core/log/log.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#include <chrono>
#include <thread>
#endif

#include <random>

PlayerId mintProcessIdentity() {
    std::random_device                      rd;
    std::mt19937_64                         gen(((uint64_t)rd() << 32) | rd());
    std::uniform_int_distribution<uint64_t> dist;
    return PlayerId::From(PlayerId::Provider::Address, dist(gen));
}

void brokerYield() {
#ifdef __EMSCRIPTEN__
    emscripten_sleep(10);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
}

/// @cond INTERNAL
IBroker *createBroker(Net::Broker preference, const std::string &signalingUrl) {
    // A signalling service is the only brokerage that can introduce a browser to a native
    // peer, so it wins whenever the game has configured one.
#ifdef LUMINOVEAU_WITH_WEBRTC
    if ((preference == Net::Broker::Auto || preference == Net::Broker::Signaling) &&
        !signalingUrl.empty()) {
        IBroker *signal = createSignalBroker(signalingUrl);
        if (signal->Init())
            return signal;
        delete signal;
        LOG_WARNING("Net: signalling service unavailable, falling back");
    }
#else
    (void)signalingUrl;
#endif

    // Steam first when it is actually running: it is the only brokerage here that reaches
    // past the local subnet, and it brings relay with it.
    if (preference == Net::Broker::Auto || preference == Net::Broker::Steam) {
        if (IBroker *steam = createSteamBroker()) {
            if (steam->Init())
                return steam;
            delete steam;
            if (preference == Net::Broker::Steam)
                LOG_WARNING("Net: Steam brokerage asked for but Steam is not ready");
        }
    }

    // EOS is not built yet, so asking for it lands on LAN and Can() reports the truth.
    const bool wantsDiscovery = preference != Net::Broker::Direct;

    if (wantsDiscovery) {
        IBroker *lan = createLanBroker();
        if (lan->Init())
            return lan;
        // No UDP on this platform, so there is nothing to discover with.
        delete lan;
        LOG_WARNING("Net: LAN discovery unavailable, falling back to direct addresses");
    }

    IBroker *local = createLocalBroker();
    if (local->Init())
        return local;
    delete local;
    return nullptr;
}
/// @endcond
