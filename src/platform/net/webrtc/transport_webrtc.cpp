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

            // Anything that overtook the description is applied now, in arrival order.
            it->second.remoteReady = true;
            for (const auto &[mid, candidate] : it->second.pending)
                _addRemoteCandidate(peer, it->second, mid, candidate);
            it->second.pending.clear();

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

            if (!it->second.remoteReady) {
                it->second.pending.emplace_back(mid, candidate);
                return;
            }
            _addRemoteCandidate(peer, it->second, mid, candidate);
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
    };

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
        link.remoteReady = false;
        link.pending.clear();

        rtc::Configuration config;
        // Whatever the game configured: a STUN server only discovers an address, while a
        // TURN server is what carries a peer that cannot be punched through at all.
        int relays = 0;
        for (const Net::IceServer &server : Net::IceServers()) {
            try {
                config.iceServers.push_back(_makeIceServer(server));
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
#endif

        link.pc = std::make_shared<rtc::PeerConnection>(config);

        link.pc->onLocalDescription([this, peer](rtc::Description description) {
            const std::string sdp = std::string(description);

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
        link.pc->onStateChange([this, peer](rtc::PeerConnection::State state) {
            if (state == rtc::PeerConnection::State::Failed) {
                // ICE found no path.
                //
                // **Which failure this is depends on whether a relay was even offered**, and
                // saying `NatBlockedNoRelay` either way is how an hour gets spent looking for a
                // missing TURN server that was configured all along. With no relay this is the
                // expected outcome behind symmetric NAT or CGNAT; *with* one it means the relay
                // itself did not work — unreachable port, rejected credential, expired ticket —
                // which is a server problem and not the player's network.
                bool relay = false;
                for (const Net::IceServer &server : Net::IceServers())
                    relay |= server.url.rfind("turn", 0) == 0;

                _failed = true;
                _lastError =
                    relay ? Net::NetError::RelayFailed : Net::NetError::NatBlockedNoRelay;
                if (relay)
                    LOG_WARNING("Net: ICE failed with a relay configured — check the TURN server "
                                "is reachable and its credentials are accepted");
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
