#include "platform/net/brokers/brokers.h"
#include "core/log/log.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#include <thread>
#endif

#include <chrono>
#include <random>

/// The identity this process answers to for the length of its run.
///
/// **It has to be unique across machines, because a signalling service routes by it.** Two clients
/// arriving with the same id do not collide loudly: every signal addressed to the second is handed
/// to the first, which then receives its own offer, its own answer and its own candidates while
/// the second hears nothing. The symptom is a peer that appears to be echoing itself — an answer
/// rejected as "Called in wrong state: stable", and remote candidates carrying one's own ufrag.
///
/// `std::random_device` alone was not enough for that. It is *permitted to be deterministic*: the
/// standard allows an implementation with no random source to return a fixed sequence, and under
/// Emscripten that is what a build can end up doing — so every browser mints the same id and only
/// a web-to-web session ever shows it. Any session with a native peer at one end works, which is
/// what makes it so easy to miss.
///
/// The clock and the address are mixed in for that reason: they are weak entropy individually but
/// they differ between two machines and between two runs, and it costs nothing to fold them in.
PlayerId mintProcessIdentity() {
    std::random_device rd;

    std::seed_seq seed{
        (uint32_t) rd(),
        (uint32_t) rd(),
        // Nanoseconds since the epoch: two machines do not agree to that resolution, and two runs
        // on one machine certainly do not.
        (uint32_t) std::chrono::high_resolution_clock::now().time_since_epoch().count(),
        (uint32_t) (std::chrono::high_resolution_clock::now().time_since_epoch().count() >> 32),
        // Where this process happened to put a stack object, which differs under any address
        // layout randomisation and is at worst a constant.
        (uint32_t) (uintptr_t) &rd,
    };

    std::mt19937_64                         gen(seed);
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
