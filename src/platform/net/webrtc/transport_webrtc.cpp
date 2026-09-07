// WebRTC data channels — the only transport a browser can use to reach a native peer, and
// the only one here that carries unreliable traffic to the web.
//
// One implementation serves both sides: libdatachannel natively and datachannel-wasm in the
// browser, which mirrors its API deliberately. Anything that differs between them is behind
// __EMSCRIPTEN__ below, and there is very little of it.
//
// WebRTC has no addresses and no listening socket, so peers cannot find each other on their
// own — a broker relays the offer, the answer and the ICE candidates until the direct path
// is up. That is IBroker::SendSignal and BrokerEvent::SignalReceived; Net wires the two
// together and this file never learns which brokerage is behind them.
//
// Two channels per peer, "r" and "u", so the reliable flag maps onto the wire rather than
// being flattened away. The engine's own [typeId][payload] blobs ride inside a one-byte
// channel tag, the same shape the SDL_net backend uses, so the server can hand a client its
// peer id before the typed layer starts.

#include "platform/net/transports.h"
#include "core/log/log.h"

#include <rtc/rtc.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <unordered_map>

// Sockets, for `_defaultRouteAddress` only — asking the routing table which interface reaches the
// internet. Emscripten has no such choice to make, and the browser filters its own candidates.
#ifndef __EMSCRIPTEN__
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#endif

namespace {

// Datagram tags, mirroring the SDL_net backend so both speak the same shape.
enum : uint8_t { TagControl = 0,
    TagUser                 = 1 };

// Signal payloads. Compact and hand-rolled: the browser half has no JSON library and this
// is three fields.
enum : uint8_t { SignalDescription = 0,
    SignalCandidate               = 1 };

void putString(std::vector<uint8_t> &out, const std::string &text) {
    const auto length = (uint32_t)text.size();
    out.insert(out.end(), (const uint8_t *)&length, (const uint8_t *)&length + sizeof(length));
    out.insert(out.end(), text.begin(), text.end());
}

bool takeString(const uint8_t *&cursor, const uint8_t *end, std::string &out) {
    uint32_t length = 0;
    if ((size_t)(end - cursor) < sizeof(length))
        return false;
    std::memcpy(&length, cursor, sizeof(length));
    cursor += sizeof(length);
    if ((size_t)(end - cursor) < length)
        return false;
    out.assign((const char *)cursor, length);
    cursor += length;
    return true;
}

class WebRtcTransport : public ITransport {
public:
    ~WebRtcTransport() override { Disconnect(); }

    bool Host(const Net::HostConfig &) override {
        Disconnect();
        // Nothing to open. Peers arrive when the broker hands us their offers.
        _isServer  = true;
        _lastError = Net::NetError::None;
        return true;
    }

    bool Connect(const Net::Endpoint &ep) override {
        Disconnect();
        if (ep.kind != Net::Endpoint::Kind::Player || !ep.player.valid()) {
            LOG_WARNING("Net: WebRTC needs the host's identity, which the broker supplies");
            _lastError = Net::NetError::BrokerUnavailable;
            return false;
        }
        if (!_sendSignal) {
            LOG_WARNING("Net: WebRTC has no signalling; set a brokerage that provides it");
            _lastError = Net::NetError::BrokerUnavailable;
            return false;
        }

        _isClient  = true;
        _lastError = Net::NetError::None;

        // The joining side offers, so both data channels are created here and arrive on the
        // host through onDataChannel.
        PeerLink &link = _peers[Net::SERVER_PEER];
        link.id        = ep.player;
        _openPeerConnection(Net::SERVER_PEER, link);

        // Only the reliability differs between the two; datachannel-wasm's DataChannelInit
        // carries nothing else, which is the one place the two APIs are not the same shape.
        link.reliable = link.pc->createDataChannel("r", rtc::DataChannelInit{});

        rtc::DataChannelInit unreliable;
        unreliable.reliability.unordered      = true;
        unreliable.reliability.maxRetransmits = 0;
        link.unreliable                       = link.pc->createDataChannel("u", unreliable);

        _watchChannel(Net::SERVER_PEER, link.reliable, true);
        _watchChannel(Net::SERVER_PEER, link.unreliable, false);

        // Started, not finished. The offer has to reach the host and the answer come back,
        // and both travel through the broker — which only Net can pump. It waits for the
        // Connect event this raises once the channels are open.
        return true;
    }

    void Disconnect() override {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _incoming.clear();
        }
        // Closing a peer connection can fire callbacks, so the map is emptied first and the
        // objects released afterwards.
        auto peers = std::move(_peers);
        _peers.clear();
        for (auto &[peer, link] : peers) {
            if (link.pc)
                link.pc->close();
        }
        _isServer = _isClient = false;
        _failed               = false;
        _selfId               = 0;
        _nextId               = 1;
    }

    void DisconnectPeer(Net::Peer peer) override {
        auto it = _peers.find(peer);
        if (it == _peers.end())
            return;
        if (it->second.pc)
            it->second.pc->close();
        _peers.erase(it);
    }

    bool      IsServer() const override { return _isServer; }
    bool      IsClient() const override { return _isClient; }
    Net::Peer SelfId() const override { return _selfId; }
    uint32_t  PeerCount() const override { return (uint32_t)_peers.size(); }

    uint32_t Ping(Net::Peer peer) const override {
#ifdef __EMSCRIPTEN__
        // The browser exposes RTT only through getStats(), which is asynchronous.
        (void)peer;
        return 0;
#else
        auto it = _peers.find(peer);
        if (it == _peers.end() || !it->second.pc)
            return 0;
        if (auto rtt = it->second.pc->rtt())
            return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(*rtt).count();
        return 0;
#endif
    }

    // SCTP fragments reliable messages, so this is the library's own cap rather than an MTU.
    uint32_t      MaxMessageSize() const override { return 256 * 1024; }
    Net::NetError LastError() const override { return _lastError; }

    void Send(Net::Peer peer, const void *data, uint32_t size, bool reliable) override {
        auto it = _peers.find(peer);
        if (it != _peers.end())
            _sendOn(it->second, TagUser, data, size, reliable);
    }

    void Broadcast(const void *data, uint32_t size, bool reliable) override {
        for (auto &[peer, link] : _peers)
            _sendOn(link, TagUser, data, size, reliable);
    }

    void Poll(std::vector<TransportEvent> &out) override {
#ifdef __EMSCRIPTEN__
        // **The escape hatch for a gathering that never says it is done.** Remote candidates are
        // held for it (see `PeerLink::gatheringDone`), so a connection whose gathering state
        // never reaches `complete` would sit on them forever and never connect at all. Waiting
        // out the grace period and applying them anyway is strictly better than that: the worst
        // case is the truncated gathering this was avoiding.
        //
        // Emscripten only, and that is what makes reaching into `_peers` here safe — the browser
        // runs every callback on the one thread this is polled from.
        const auto now = std::chrono::steady_clock::now();
        for (auto &[peer, link] : _peers) {
            if (link.gatheringDone || link.pending.empty() || !link.remoteReady)
                continue;
            if (now - link.openedAt < kGatheringGrace)
                continue;
            LOG_WARNING("Net: peer {} gathered for {} ms without finishing — applying its "
                        "{} held candidate(s) anyway",
                        peer, kGatheringGrace.count(), link.pending.size());
            link.gatheringDone = true;
            _flushRemoteCandidates(peer, link);
        }
#endif

        std::vector<TransportEvent> drained;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            drained.swap(_incoming);
        }
        for (TransportEvent &ev : drained)
            out.push_back(std::move(ev));
    }

    void SetSignalSender(SignalSender sender) override { _sendSignal = std::move(sender); }
    bool NeedsSignaling() const override { return true; }

    // A signal from a peer we have not seen before is someone joining: the offer arrives
    // first and the peer connection is built around it.
    void DeliverSignal(const PlayerId &from, const void *data, uint32_t size) override {
        const auto *cursor = (const uint8_t *)data;
        const auto *end    = cursor + size;
        if (cursor == end)
            return;
        const uint8_t kind = *cursor++;

        // **Every signal that arrives, before anything can discard it.** What this side sends is
        // logged and what it does with a description is logged, but the moment of *arrival* was
        // not — so "the answer never came" and "the answer came and was dropped" looked
        // identical, and telling them apart meant reading the other machine's console. One line
        // here makes a single log self-sufficient.
        LOG_INFO("Net: signal from {} kind={} ({} bytes)", from.toString(),
                 kind == SignalDescription ? "description"
                 : kind == SignalCandidate ? "candidate"
                                           : "?",
                 size);

        Net::Peer peer = _peerFor(from);
        if (peer == 0 && _isServer) {
            peer           = _nextId++;
            PeerLink &link = _peers[peer];
            link.id        = from;
            _openPeerConnection(peer, link);
            // The offering side made the channels; they surface through onDataChannel.
            link.pc->onDataChannel([this, peer](std::shared_ptr<rtc::DataChannel> channel) {
                _adoptChannel(peer, std::move(channel));
            });
        }
        if (peer == 0 && !_isServer)
            peer = Net::SERVER_PEER;

        auto it = _peers.find(peer);
        if (it == _peers.end() || !it->second.pc) {
            // **The last silent drop in the negotiation.** A client that has not opened its
            // connection yet, or a peer torn down between the signal being posted and arriving,
            // discards an answer here — and the sender has already logged that it sent one, so
            // the two logs disagree with no way to tell which is lying.
            LOG_WARNING("Net: dropping an incoming signal for peer {} — no connection open", peer);
            return;
        }

        if (kind == SignalDescription) {
            std::string type, sdp;
            if (!takeString(cursor, end, type) || !takeString(cursor, end, sdp))
                return;

            // **A second offer means the peer gave up and started again, and it needs a new
            // connection.** libdatachannel does not renegotiate: applying a fresh offer to a
            // connection that already has a remote description fails with "Invalid ICE settings
            // from remote SDP", because the ufrag and password in the new SDP do not match the
            // ICE session already running. The peer is then stuck forever — its first attempt
            // timed out and every retry is rejected on arrival, which looks like a network that
            // never works rather than one that failed once.
            if (_isServer && type == "offer" && it->second.remoteReady) {
                LOG_INFO("Net: peer {} is offering again — rebuilding its connection", peer);

                if (it->second.announced)
                    _queueDisconnect(peer);

                it->second.reliable.reset();
                it->second.unreliable.reset();
                if (it->second.pc)
                    it->second.pc->close();
                it->second.pc.reset();

                _openPeerConnection(peer, it->second);
                it->second.pc->onDataChannel([this, peer](std::shared_ptr<rtc::DataChannel> ch) {
                    _adoptChannel(peer, std::move(ch));
                });
            }

            try {
                it->second.pc->setRemoteDescription(rtc::Description(sdp, type));
            } catch (const std::exception &e) {
                LOG_WARNING("Net: rejected a remote description from peer {}: {}", peer, e.what());
                return;
            }

            it->second.remoteReady = true;
            _flushRemoteCandidates(peer, it->second);

        } else if (kind == SignalCandidate) {
            std::string mid, candidate;
            if (!takeString(cursor, end, mid) || !takeString(cursor, end, candidate))
                return;

            // **The other half of the picture.** A relay's log shows allocations and permissions
            // but never the candidate lists, and a local-only log shows one side of a negotiation
            // that needs two. A peer offering nothing but `typ host` addresses, or no `typ relay`
            // at all, cannot be reached however well this end is configured — and from here that
            // is indistinguishable from packets being dropped in flight.
            LOG_INFO("Net: remote candidate from peer {}: {}", peer, candidate);

            it->second.pending.emplace_back(mid, candidate);
            _flushRemoteCandidates(peer, it->second);
        }
    }

private:
    struct PeerLink {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel>    reliable;
        std::shared_ptr<rtc::DataChannel>    unreliable;
        PlayerId                             id;
        bool                                 announced = false;

        /// **Candidates that arrived before the description they belong to.**
        ///
        /// Trickle ICE sends candidates as they are found rather than waiting for the answer, so
        /// they routinely overtake it — the local ones here are produced within a tenth of a
        /// second of the offer, and both travel the same signalling socket with no ordering
        /// between them. `addRemoteCondidate` *throws* when no remote description is set, so an
        /// early candidate was not merely dropped: the exception left the rest of that message
        /// unhandled. Losing the relay candidate that way is silent and looks exactly like a
        /// network that will not carry the traffic.
        bool                                             remoteReady = false;
        std::vector<std::pair<std::string, std::string>> pending; ///< (mid, candidate)

        /// **Local gathering has finished, so a remote candidate can safely be applied.**
        ///
        /// A browser stops gathering the moment one of its pairs connects, and a remote candidate
        /// delivered the instant it arrives is early enough to cause exactly that. The peer's LAN
        /// host candidate lands about six milliseconds in — before the first STUN reply, which
        /// takes around twenty — so the check succeeds against it, Chrome abandons the rest of the
        /// allocation session, and the connection is left holding nothing but its own mDNS host
        /// candidate. No relay is ever allocated and nothing reports an error: the gathering state
        /// goes `complete` two milliseconds after the candidate is added, so every diagnostic
        /// shows a correctly configured connection that simply gathered one candidate.
        ///
        /// On a LAN it is invisible, because the pair it settled on is a real path. Only over the
        /// internet, where that host candidate is unreachable, does the missing relay matter — so
        /// it presents as "works at home, never works online".
        ///
        /// Holding remote candidates until gathering completes costs the two hundred milliseconds
        /// gathering takes, and ICE accepts candidates at any point in a session.
        ///
        /// Emscripten only: libjuice does not prune this way, and native has always gathered its
        /// relay. `openedAt` is the deadline for a gathering that never reports itself finished,
        /// which would otherwise hold every remote candidate forever.
        bool                                  gatheringDone = false;
        std::chrono::steady_clock::time_point openedAt{};

        /// ICE reached `connected` at some point, so a path was found whatever happened after.
        bool iceConnected = false;
    };

    /// How long remote candidates wait for local gathering before being applied regardless.
    ///
    /// Gathering takes about 200 ms against a reachable relay, so this only fires when something
    /// has gone wrong — and a connection that tries a late candidate beats one that never tries.
    static constexpr std::chrono::milliseconds kGatheringGrace{ 4000 };

    /// The local address the operating system would use to reach the internet.
    ///
    /// **Because a developer machine offers a pile of addresses that are not paths.** WSL, VPN
    /// clients and VM tooling each add an interface, and ICE gathers a host candidate for every
    /// one of them. Host candidates outrank both server-reflexive and relay by priority, so when
    /// the two peers happen to be on the same machine — a browser and a native client, which is
    /// how this gets tested — a check against the WSL adapter *succeeds*, ICE nominates it, and
    /// the connection then dies because that address is not a two-way path. The relay is never
    /// tried at all. Observed as `checking → connected → disconnected` on a pair against
    /// `172.25.208.1`.
    ///
    /// Found by asking the routing table rather than by guessing at address ranges: a UDP socket
    /// is *connected* to a public address, which sends nothing but makes the kernel choose an
    /// interface, and its local name is then the answer. `172.16/12` and `192.168/16` are real
    /// private networks as well as VM defaults, so no filter on the addresses themselves can tell
    /// a virtual adapter from a LAN.
#ifndef __EMSCRIPTEN__
    static std::string _defaultRouteAddress() {
#ifdef _WIN32
        using SocketHandle = SOCKET;
        using NameLength   = int;
        const SocketHandle invalid = INVALID_SOCKET;
#else
        using SocketHandle = int;
        using NameLength   = socklen_t;
        const SocketHandle invalid = -1;
#endif

        const SocketHandle sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == invalid)
            return {};

        sockaddr_in probe{};
        probe.sin_family      = AF_INET;
        probe.sin_port        = htons(53);
        probe.sin_addr.s_addr = inet_addr("192.0.2.1"); // TEST-NET-1: routable, never answers

        std::string address;
        if (::connect(sock, (const sockaddr *) &probe, sizeof(probe)) == 0) {
            sockaddr_in local{};
            NameLength  size = sizeof(local);
            if (::getsockname(sock, (sockaddr *) &local, &size) == 0) {
                char text[INET_ADDRSTRLEN] = {};
                if (::inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text)))
                    address = text;
            }
        }

#ifdef _WIN32
        ::closesocket(sock);
#else
        ::close(sock);
#endif
        return address;
    }
#endif

    /// Applies one remote candidate, surviving a bad one.
    ///
    /// A candidate this build cannot parse — an address family it was not compiled for, a form a
    /// newer peer emits — must cost that one candidate and nothing else. Letting it throw takes
    /// the whole signalling message with it, including candidates that would have worked.
    void _addRemoteCandidate(Net::Peer peer, PeerLink &link, const std::string &mid,
                             const std::string &candidate) {
        try {
            link.pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
        } catch (const std::exception &e) {
            LOG_WARNING("Net: ignoring a remote candidate from peer {}: {}", peer, e.what());
        }
    }

    /// Applies every queued remote candidate, once it is safe to.
    ///
    /// Two conditions, and both are the reason a candidate is queued rather than applied on
    /// arrival: the remote description has to exist, or `addRemoteCandidate` throws; and, in a
    /// browser, local gathering has to have finished, or applying one cuts the gathering short.
    /// See `PeerLink::gatheringDone` for what that costs.
    void _flushRemoteCandidates(Net::Peer peer, PeerLink &link) {
        if (!link.remoteReady || link.pending.empty())
            return;
#ifdef __EMSCRIPTEN__
        if (!link.gatheringDone)
            return;
#endif
        for (const auto &[mid, candidate] : link.pending)
            _addRemoteCandidate(peer, link, mid, candidate);
        link.pending.clear();
    }

    /// Builds libdatachannel's own ICE server from ours.
    ///
    /// **Constructed field by field rather than from a URL string.** `rtc::IceServer(url)` parses
    /// `turn:user:password@host` on native and, in datachannel-wasm, does not parse at all: it
    /// keeps the string as a `Dummy` server and passes the browser an empty username and
    /// password. Going through the typed constructor is what makes an authenticated relay work in
    /// a browser, which is the build that most needs one.
    static rtc::IceServer _makeIceServer(const Net::IceServer &server) {
        const size_t colon = server.url.find(':');
        if (colon == std::string::npos)
            throw std::invalid_argument("no scheme");

        const std::string scheme = server.url.substr(0, colon);
        std::string       rest   = server.url.substr(colon + 1);

        // `?transport=tcp` decides the relay type rather than being part of the host.
        rtc::IceServer::RelayType relay = rtc::IceServer::RelayType::TurnUdp;
        if (const size_t query = rest.find('?'); query != std::string::npos) {
            if (rest.find("transport=tcp", query) != std::string::npos)
                relay = rtc::IceServer::RelayType::TurnTcp;
            rest = rest.substr(0, query);
        }
        if (scheme == "turns")
            relay = rtc::IceServer::RelayType::TurnTls;

        // Last colon, so a bare IPv6 literal fails the port parse rather than being cut in half.
        std::string host = rest;
        std::string port;
        if (const size_t mark = rest.rfind(':'); mark != std::string::npos) {
            host = rest.substr(0, mark);
            port = rest.substr(mark + 1);
        }
        if (host.empty())
            throw std::invalid_argument("no host");

        const bool turn = scheme == "turn" || scheme == "turns";
        if (!turn) {
            if (scheme != "stun" && scheme != "stuns")
                throw std::invalid_argument("unknown scheme '" + scheme + "'");
            return port.empty() ? rtc::IceServer(host, uint16_t(3478)) : rtc::IceServer(host, port);
        }

        if (port.empty())
            port = scheme == "turns" ? "5349" : "3478";
        return rtc::IceServer(host, port, server.username, server.credential, relay);
    }

    void _openPeerConnection(Net::Peer peer, PeerLink &link) {
        // A fresh connection has no remote description, so anything queued against the previous
        // one belongs to a negotiation that is over.
        link.remoteReady   = false;
        link.gatheringDone = false;
        link.openedAt      = std::chrono::steady_clock::now();
        link.pending.clear();

        rtc::Configuration config;
        // Whatever the game configured: a STUN server only discovers an address, while a
        // TURN server is what carries a peer that cannot be punched through at all.
        int relays = 0;
        for (const Net::IceServer &server : Net::IceServers()) {
            try {
                const rtc::IceServer built = _makeIceServer(server);

                // **What is actually handed to the stack, not what arrived.** A relay that works
                // when the same URL and credential are typed into a test page, and does nothing
                // here, differs somewhere between the two — and the only place that can be seen
                // is after this translation. The credential is never printed, only whether one is
                // present, since it is a working secret for as long as it lives.
                LOG_INFO("Net: ICE server host={} port={} type={} relay={} user='{}' cred={}",
                         built.hostname, built.port,
                         built.type == rtc::IceServer::Type::Turn ? "turn" : "stun",
                         built.relayType == rtc::IceServer::RelayType::TurnTls   ? "tls"
                         : built.relayType == rtc::IceServer::RelayType::TurnTcp ? "tcp"
                                                                                 : "udp",
                         built.username, built.password.empty() ? "none" : "present");

                config.iceServers.push_back(built);
                relays += server.url.rfind("turn", 0) == 0 ? 1 : 0;
            } catch (const std::exception &e) {
                LOG_WARNING("Net: ignoring ICE server '{}': {}", server.url, e.what());
            }
        }

        // **Counts, never the credentials.** Which servers a peer connection was given is the
        // first thing worth knowing when a join fails, and it is invisible otherwise — the list
        // arrives from the signalling service, so it differs between deployments and cannot be
        // read off the build.
        LOG_INFO("Net: opening a peer connection with {} ICE server(s), {} of them relays",
                 config.iceServers.size(), relays);

        // **`LUMI_FORCE_RELAY=1` throws away every candidate but the relay ones.** A diagnostic,
        // not a setting: normal play should prefer a direct path and fall back. But "the relay
        // works and something else was chosen" and "the relay itself cannot carry traffic"
        // produce the same silence, and they have opposite fixes. Forcing it separates them in a
        // single run — if this connects, the relay is sound; if it does not, the relay path is
        // broken and no amount of candidate tuning will help.
        //
        // Native only: datachannel-wasm's `Configuration` has no transport policy, and a browser
        // has no environment to read anyway.
#ifndef __EMSCRIPTEN__
        if (const char *force = std::getenv("LUMI_FORCE_RELAY"); force && *force == '1') {
            config.iceTransportPolicy = rtc::TransportPolicy::Relay;
            LOG_WARNING("Net: LUMI_FORCE_RELAY is set — using relay candidates only");
        }

        // **One interface, the one that reaches the internet.** See `_defaultRouteAddress`: left
        // to itself ICE offers a host candidate per adapter, and a virtual one can win the
        // priority contest and then fail to carry traffic. A browser has no equivalent problem —
        // it does this filtering itself — so this is native-only.
        //
        // Empty means the probe failed, in which case binding to nothing is right: every
        // interface is still better than no connection at all.
        if (const std::string bind = _defaultRouteAddress(); !bind.empty()) {
            config.bindAddress = bind;
            LOG_INFO("Net: binding ICE to {}", bind);
        } else {
            LOG_WARNING("Net: could not determine the default route — ICE will use every adapter");
        }
#endif

        link.pc = std::make_shared<rtc::PeerConnection>(config);

        link.pc->onLocalDescription([this, peer](rtc::Description description) {
            std::string sdp = std::string(description);

            // **This side asks to be the DTLS client, and it has to.**
            //
            // Mbed TLS cannot accept a fragmented ClientHello. Its own source says so — "For now
            // we don't support fragmentation" in `ssl_parse_client_hello` — and it rejects one on
            // the length check before parsing anything, with `MBEDTLS_ERR_SSL_DECODE_ERROR`.
            // Chrome's ClientHello is around 1450 bytes and therefore arrives in two records, so
            // a browser can never complete a handshake against Mbed TLS *as the server*.
            //
            // The role is decided by `a=setup`. libdatachannel writes `actpass` into every offer
            // (hardcoded in `IceTransport::getDescription`, so there is no API to ask), a browser
            // offered `actpass` always answers `active`, and `active` means it sends the
            // ClientHello. Asking for `active` here forces the browser to answer `passive`
            // instead: it becomes the DTLS server, BoringSSL parses our ClientHello — which is
            // small and never fragments — and libdatachannel works its own role out from the
            // answer (`IceTransport::setRemoteDescription`, which maps a `passive` answer onto
            // the active role), so nothing downstream needs telling.
            //
            // This is ordinary SDP rather than a trick: an offerer may choose `active`, and the
            // answerer is then required to be `passive`. It costs nothing between two native
            // peers, where the answering side parses a ClientHello that was never large.
            //
            // **The symptom this cures names nothing.** ICE succeeds, nominates a pair, reaches
            // `completed`, and only then does the connection fail — which every layer above
            // reports as a path that could not be found, and sends you to the TURN server.
            //
            // Native only. In a browser the whole question belongs to the browser.
#ifndef __EMSCRIPTEN__
            if (description.type() == rtc::Description::Type::Offer) {
                if (const size_t at = sdp.find("a=setup:actpass"); at != std::string::npos)
                    sdp.replace(at, std::strlen("a=setup:actpass"), "a=setup:active");
            }
#endif

            // **The half of the negotiation nothing has ever shown.** Remote descriptions and
            // both sets of candidates are logged; what this side *sends* is not — so an answer
            // that is never produced and an answer that is produced but never delivered look
            // identical from here, and from the other end both look like a peer that went quiet.
            LOG_INFO("Net: sending local description to peer {}: type={} ({} bytes)", peer,
                     description.typeString(), sdp.size());

            std::vector<uint8_t> payload{ SignalDescription };
            putString(payload, description.typeString());
            putString(payload, sdp);
            _signal(peer, payload);
        });
        link.pc->onLocalCandidate([this, peer](rtc::Candidate candidate) {
            // **What each side actually offers is the one thing a relay's own log cannot show.**
            // coturn sees allocations and permissions; it never sees which address the client put
            // in its candidate list. A `typ relay` line carrying a private address means
            // `external-ip` is not being applied and no peer can reach it — indistinguishable,
            // from the server, from a firewall that drops the traffic.
            LOG_INFO("Net: local candidate {}", candidate.candidate());

            std::vector<uint8_t> payload{ SignalCandidate };
            putString(payload, candidate.mid());
            putString(payload, candidate.candidate());
            _signal(peer, payload);
        });
        link.pc->onGatheringStateChange([this, peer](rtc::PeerConnection::GatheringState state) {
            if (state != rtc::PeerConnection::GatheringState::Complete)
                return;

            // **The point every queued remote candidate has been waiting for.** Until this fires
            // a browser is still allocating, and a candidate applied now would end that early —
            // see `PeerLink::gatheringDone`.
            auto it = _peers.find(peer);
            if (it == _peers.end())
                return;
            it->second.gatheringDone = true;
            if (!it->second.pending.empty())
                LOG_INFO("Net: local gathering finished for peer {} — applying {} held "
                         "candidate(s)",
                         peer, it->second.pending.size());
            _flushRemoteCandidates(peer, it->second);
        });
        // **Remembered, because a failed connection cannot be asked what it was doing.** By the
        // time `State::Failed` arrives the ICE state has already been overwritten with `Closed`,
        // so the one fact that separates "no path" from "a path that carried nothing" is gone.
        link.pc->onIceStateChange([this, peer](rtc::PeerConnection::IceState state) {
            auto it = _peers.find(peer);
            if (it == _peers.end())
                return;
            it->second.iceConnected |= state == rtc::PeerConnection::IceState::Connected ||
                                       state == rtc::PeerConnection::IceState::Completed;
        });
        link.pc->onStateChange([this, peer](rtc::PeerConnection::State state) {
            if (state == rtc::PeerConnection::State::Failed) {
                // **A failed connection is not necessarily a failed *path*.** libdatachannel
                // reports one `Failed` for everything past the offer, so a DTLS handshake that
                // dies on a nominated pair looks exactly like ICE finding nowhere to go — and
                // "the relay could not carry the connection" then sends you to the TURN server
                // for a fault that is in the crypto library. That cost most of a day once.
                //
                // ICE having reached `connected` or `completed` is what tells the two apart:
                // the peers found each other and exchanged traffic, so nothing about NAT,
                // firewalls or relays is at issue and no amount of candidate tuning will help.
                const auto it       = _peers.find(peer);
                const bool hadPath  = it != _peers.end() && it->second.iceConnected;

                _failed = true;
                if (hadPath) {
                    _lastError = Net::NetError::HandshakeFailed;
                    LOG_WARNING("Net: peer {} failed *after* ICE connected — a path was found and "
                                "the handshake over it did not complete. This is not a NAT or "
                                "relay fault; look at the DTLS log above.",
                                peer);
                } else {
                    // **Which failure this is depends on whether a relay was even offered**, and
                    // saying `NatBlockedNoRelay` either way is how an hour gets spent looking for
                    // a missing TURN server that was configured all along. With no relay this is
                    // the expected outcome behind symmetric NAT or CGNAT; *with* one it means the
                    // relay itself did not work — unreachable port, rejected credential, expired
                    // ticket — which is a server problem and not the player's network.
                    bool relay = false;
                    for (const Net::IceServer &server : Net::IceServers())
                        relay |= server.url.rfind("turn", 0) == 0;

                    _lastError =
                        relay ? Net::NetError::RelayFailed : Net::NetError::NatBlockedNoRelay;
                    if (relay)
                        LOG_WARNING("Net: ICE failed with a relay configured — check the TURN "
                                    "server is reachable and its credentials are accepted");
                }
                _queueDisconnect(peer);
            } else if (state == rtc::PeerConnection::State::Closed ||
                       state == rtc::PeerConnection::State::Disconnected) {
                _queueDisconnect(peer);
            }
        });
    }

    void _adoptChannel(Net::Peer peer, std::shared_ptr<rtc::DataChannel> channel) {
        auto it = _peers.find(peer);
        if (it == _peers.end())
            return;
        const bool reliable = channel->label() != "u";
        if (reliable)
            it->second.reliable = channel;
        else
            it->second.unreliable = channel;
        _watchChannel(peer, channel, reliable);
    }

    void _watchChannel(Net::Peer peer, std::shared_ptr<rtc::DataChannel> channel, bool reliable) {
        if (!channel)
            return;
        channel->onOpen([this, peer]() { _announceIfReady(peer); });
        channel->onMessage([this, peer, reliable](rtc::message_variant message) {
            if (!std::holds_alternative<rtc::binary>(message))
                return; // the engine only ever sends binary
            const rtc::binary &bytes = std::get<rtc::binary>(message);
            if (bytes.empty())
                return;
            _receive(peer, (const uint8_t *)bytes.data(), bytes.size(), reliable);
        });
        channel->onClosed([this, peer]() { _queueDisconnect(peer); });
    }

    void _receive(Net::Peer peer, const uint8_t *bytes, size_t size, bool reliable) {
        const uint8_t tag = bytes[0];
        if (tag == TagControl) {
            // The host hands a client its peer id before anything typed is exchanged.
            if (size >= 1 + sizeof(Net::Peer))
                std::memcpy(&_selfId, bytes + 1, sizeof(Net::Peer));
            return;
        }
        if (tag != TagUser)
            return;

        TransportEvent ev;
        ev.type     = TransportEvent::Receive;
        ev.peer     = peer;
        ev.reliable = reliable;
        ev.data.assign(bytes + 1, bytes + size);
        _queue(std::move(ev));
    }

    // Both channels have to be open before a peer is usable, or an unreliable send made from
    // the join callback would be dropped on the floor.
    void _announceIfReady(Net::Peer peer) {
        auto it = _peers.find(peer);
        if (it == _peers.end() || it->second.announced)
            return;
        if (!_readyFor(peer))
            return;
        it->second.announced = true;

        if (_isServer) {
            uint8_t control[1 + sizeof(Net::Peer)];
            control[0] = TagControl;
            std::memcpy(control + 1, &peer, sizeof(peer));
            _sendOn(it->second, TagControl, control + 1, sizeof(Net::Peer), true);
        }

        TransportEvent ev;
        ev.type = TransportEvent::Connect;
        ev.peer = peer;
        _queue(std::move(ev));
    }

    bool _readyFor(Net::Peer peer) const {
        auto it = _peers.find(peer);
        return it != _peers.end() && it->second.reliable && it->second.unreliable &&
               it->second.reliable->isOpen() && it->second.unreliable->isOpen();
    }

    void _sendOn(PeerLink &link, uint8_t tag, const void *data, uint32_t size, bool reliable) {
        auto &channel = reliable ? link.reliable : link.unreliable;
        if (!channel || !channel->isOpen())
            return;
        std::vector<uint8_t> framed(size + 1);
        framed[0] = tag;
        if (size)
            std::memcpy(framed.data() + 1, data, size);
        // A peer that left between the check above and this line takes the socket down with
        // it, and libdatachannel reports that by throwing. Losing a message to someone who
        // has gone is not a reason to lose the session everyone else is still in.
        try {
            channel->send((const rtc::byte *)framed.data(), framed.size());
        } catch (const std::exception &e) {
            LOG_WARNING("Net: send to peer failed: {}", e.what());
        }
    }

    void _signal(Net::Peer peer, const std::vector<uint8_t> &payload) {
        auto it = _peers.find(peer);
        if (it == _peers.end() || !_sendSignal) {
            // **Dropped silently until now, and there are two quite different reasons for it.**
            // No `_sendSignal` means the brokerage never installed one; a missing peer means the
            // link was torn down between generating this and sending it. Either way the other
            // side waits for an answer that was never posted, which from there is
            // indistinguishable from a network that ate it.
            LOG_WARNING("Net: dropping a signal for peer {} — {}", peer,
                        it == _peers.end() ? "no such peer" : "no signal sender installed");
            return;
        }
        _sendSignal(it->second.id, payload.data(), (uint32_t)payload.size());
    }

    Net::Peer _peerFor(const PlayerId &id) const {
        for (const auto &[peer, link] : _peers)
            if (link.id == id)
                return peer;
        return 0;
    }

    void _queueDisconnect(Net::Peer peer) {
        TransportEvent ev;
        ev.type = TransportEvent::Disconnect;
        ev.peer = peer;
        _queue(std::move(ev));
    }

    // Natively these arrive on libdatachannel's own threads; in the browser they arrive on
    // the main one. Everything is funnelled through here so the game only ever sees events
    // inside its own Poll.
    void _queue(TransportEvent &&ev) {
        std::lock_guard<std::mutex> lock(_mutex);
        _incoming.push_back(std::move(ev));
    }

    std::unordered_map<Net::Peer, PeerLink> _peers;
    std::vector<TransportEvent>             _incoming;
    mutable std::mutex                      _mutex;

    SignalSender _sendSignal;

    bool          _isServer = false, _isClient = false;
    bool          _failed    = false;
    Net::Peer     _selfId    = 0;
    Net::Peer     _nextId    = 1;
    Net::NetError _lastError = Net::NetError::None;
};

} // namespace

/// @cond INTERNAL
ITransport *createWebRtcTransport() {
    return new WebRtcTransport();
}
/// @endcond
