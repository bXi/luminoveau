// Net — transport-agnostic core: lifecycle, typed-message framing + dispatch. The actual
// sockets live behind ITransport (see sdl/ and webgpu/), and who introduces peers lives
// behind IBroker (see brokers/). Wire format for a typed message is
// [uint32 typeId][payload bytes]; net.cpp adds/peels the prefix and routes to handlers.

#include "platform/net/net.h"
#include "platform/net/ibroker.h"
#include "platform/net/itransport.h"
#include "core/log/log.h"

#include <chrono>
#include <map>
#include <unordered_map>
#include <vector>

namespace {

// Reserved type id for the connect handshake. Type ids for user packets are FNV-1a hashes
// of a decorated function name, so this only collides by astronomical accident.
constexpr uint32_t HANDSHAKE_TYPE_ID = 0x4C4E4831; // 'LNH1'
constexpr uint32_t HANDSHAKE_MAGIC   = 0x4C4E4554; // 'LNET'
constexpr uint16_t PROTOCOL_VERSION  = 1;

constexpr auto HANDSHAKE_TIMEOUT = std::chrono::seconds(5);
constexpr auto HANDSHAKE_POLL    = std::chrono::milliseconds(5);
// Entering a lobby is a round trip to a platform service, so it gets longer than a handshake.
constexpr auto BROKER_TIMEOUT = std::chrono::seconds(15);

struct Handshake {
    enum Kind : uint8_t { Hello,
        Accept,
        Reject };

    uint32_t magic    = HANDSHAKE_MAGIC;
    uint16_t protocol = PROTOCOL_VERSION;
    uint8_t  kind     = Hello;
    uint8_t  reason   = (uint8_t)Net::NetError::None;
    uint64_t buildId  = 0;
    // Folded from every registered packet's type id and size. Serialization is a memcpy of
    // the game's own structs, so a peer built by another compiler — or for another word
    // size, which is exactly what a browser is — can lay the same struct out differently.
    // Comparing this refuses that up front instead of letting every message be misread.
    uint64_t layouts = 0;
    PlayerId player;
};

// Clients that have connected but not yet cleared the handshake. Kept out of Net so the
// public header stays free of chrono and transport types.
std::unordered_map<Net::Peer, std::chrono::steady_clock::time_point> pendingPeers;

// Events that arrived while _join was blocked on the handshake, dispatched on the next Update.
std::vector<TransportEvent> deferredEvents;

// Both sides log what they registered, so one diff of the two logs names the packet that
// disagrees — the digest alone can only say that something does.
void logLayouts(const std::map<uint32_t, uint32_t> &layouts) {
    for (const auto &[typeId, size] : layouts)
        LOG_WARNING("Net:   packet {:#010x} is {} bytes here", typeId, size);
}

} // namespace

bool Net::_ensureTransport() {
    if (_transport)
        return true;

    // A brokerage that carries signals is what makes the WebRTC transport usable, and it is
    // the only transport a browser has. Ask for it when the active broker can relay.
    const bool wantsSignaling = _broker && _broker->CarriesSignals();
    _transport                = createTransport(wantsSignaling);
    if (!_transport)
        return false;

    if (_transport->NeedsSignaling()) {
        IBroker *broker = _broker;
        _transport->SetSignalSender([broker](const PlayerId &to, const void *data, uint32_t size) {
            return broker && broker->SendSignal(to, data, size);
        });
    }
    return true;
}

bool Net::_init(Broker preference) {
    if (!_broker)
        _broker = createBroker(preference, _signalingUrl);
    if (!_broker) {
        LOG_WARNING("Net::Init — no brokerage available");
        _lastError = NetError::BrokerUnavailable;
        return false;
    }
    _broker->SetBuildId(_buildId);
    if (!_ensureTransport()) {
        LOG_WARNING("Net::Init — no transport backend available");
        _lastError = NetError::NoTransport;
        return false;
    }
    return true;
}

void Net::_shutdown() {
    if (_transport) {
        _transport->Disconnect();
        delete _transport;
        _transport = nullptr;
    }
    if (_broker) {
        _broker->Shutdown();
        delete _broker;
        _broker = nullptr;
    }
    _handlers.clear();
    _peerIds.clear();
    _roomCode.clear();
    pendingPeers.clear();
    deferredEvents.clear();
}

bool Net::_host(const HostConfig &cfg) {
    if (!_ensureTransport()) {
        _lastError = NetError::NoTransport;
        return false;
    }
    if (!_transport->Host(cfg)) {
        _lastError = _transport->LastError();
        return false;
    }
    // Advertising is the broker's half of hosting; a broker that cannot list says so and the
    // session simply stays unlisted.
    if (cfg.publicListing && _broker)
        _broker->CreateLobby(cfg);
    _lastError = NetError::None;
    return true;
}

// Joining is a sequence — resolve the introduction, negotiate a path, agree on the build —
// and each step waits on something that arrives asynchronously. Both the blocking Join and
// the callback one drive the same steps; only who does the waiting differs.
Net::Step Net::_stepResolve() {
    std::vector<BrokerEvent> events;
    _broker->Tick();
    _broker->Poll(events);
    for (const BrokerEvent &ev : events) {
        if (ev.type == BrokerEvent::SignalReceived && _transport)
            _transport->DeliverSignal(ev.from, ev.data.data(), (uint32_t)ev.data.size());
        if (ev.type == BrokerEvent::LobbyJoined) {
            _joinTarget = ev.endpoint;
            return Step::Done;
        }
        if (ev.type == BrokerEvent::Error) {
            _lastError = ev.error;
            return Step::Failed;
        }
    }
    return Step::Pending;
}

Net::Step Net::_stepConnect() {
    _pumpBroker();

    std::vector<TransportEvent> events;
    _transport->Poll(events);
    for (TransportEvent &ev : events) {
        if (ev.type == TransportEvent::Connect && ev.peer == SERVER_PEER)
            return Step::Done; // joining is its own announcement; the handshake follows
        if (ev.type == TransportEvent::Disconnect && ev.peer == SERVER_PEER) {
            _lastError = _transport->LastError();
            return Step::Failed;
        }
        deferredEvents.push_back(std::move(ev));
    }
    return Step::Pending;
}

void Net::_sendHello() {
    Handshake hello;
    hello.buildId = _buildId;
    hello.layouts = _layoutDigest();
    hello.player  = _localPlayer();
    _sendRaw(SERVER_PEER, HANDSHAKE_TYPE_ID, &hello, sizeof(hello), true);
}

// Anything that is not the server's verdict is put aside rather than dropped, so a host that
// starts talking immediately does not lose its first messages.
Net::Step Net::_stepHandshake() {
    std::vector<TransportEvent> events;
    _transport->Poll(events);
    for (TransportEvent &ev : events) {
        if (ev.type == TransportEvent::Disconnect) {
            _lastError = NetError::Refused;
            return Step::Failed;
        }
        if (ev.type != TransportEvent::Receive || ev.data.size() < sizeof(uint32_t)) {
            deferredEvents.push_back(std::move(ev));
            continue;
        }
        uint32_t typeId;
        std::memcpy(&typeId, ev.data.data(), sizeof(uint32_t));
        if (typeId != HANDSHAKE_TYPE_ID) {
            deferredEvents.push_back(std::move(ev));
            continue;
        }
        if (ev.data.size() != sizeof(uint32_t) + sizeof(Handshake)) {
            _lastError = NetError::VersionMismatch;
            return Step::Failed;
        }
        Handshake reply;
        std::memcpy(&reply, ev.data.data() + sizeof(uint32_t), sizeof(reply));
        if (reply.magic != HANDSHAKE_MAGIC || reply.kind != Handshake::Accept) {
            _lastError = reply.magic == HANDSHAKE_MAGIC ? (NetError)reply.reason
                                                        : NetError::VersionMismatch;
            if (reply.magic == HANDSHAKE_MAGIC && reply.layouts != _layoutDigest()) {
                LOG_WARNING("Net: the host lays its packets out differently");
                logLayouts(_layouts);
            }
            return Step::Failed;
        }
        _peerIds[SERVER_PEER] = reply.player;
        return Step::Done;
    }
    return Step::Pending;
}

bool Net::_beginJoin(const Endpoint &ep) {
    if (!_ensureTransport()) {
        _lastError = NetError::NoTransport;
        return false;
    }
    _joinTarget = ep;

    // A lobby or a room code is an introduction, not a route: the broker turns either into
    // the host to dial.
    if (ep.kind == Endpoint::Kind::Lobby || ep.kind == Endpoint::Kind::JoinCode) {
        if (!_broker || !_broker->JoinLobby(ep)) {
            _lastError = NetError::BrokerUnavailable;
            return false;
        }
        _joinStage = JoinStage::Resolving;
    } else {
        _joinStage = JoinStage::Connecting;
        if (!_startConnect())
            return false;
    }
    _joinDeadline = std::chrono::steady_clock::now() + BROKER_TIMEOUT;
    return true;
}

bool Net::_startConnect() {
    if (!_transport->Connect(_joinTarget)) {
        _lastError = _transport->LastError();
        return false;
    }
    // A transport that needs signalling has only started the attempt; the rest of the
    // negotiation arrives through the broker. One that does not is already connected.
    if (_transport->NeedsSignaling()) {
        _joinStage = JoinStage::Connecting;
    } else {
        _sendHello();
        _joinStage = JoinStage::Handshaking;
    }
    return true;
}

// One slice of the join. Returns Pending until it settles either way.
Net::Step Net::_advanceJoin() {
    if (_joinStage == JoinStage::None)
        return Step::Done;

    if (std::chrono::steady_clock::now() > _joinDeadline) {
        _lastError = NetError::Timeout;
        return _failJoin();
    }

    Step step = Step::Pending;
    switch (_joinStage) {
    case JoinStage::Resolving:
        step = _stepResolve();
        if (step == Step::Done) {
            if (!_startConnect())
                return _failJoin();
            _joinDeadline = std::chrono::steady_clock::now() + BROKER_TIMEOUT;
            return Step::Pending;
        }
        break;

    case JoinStage::Connecting:
        step = _stepConnect();
        if (step == Step::Done) {
            _sendHello();
            _joinStage    = JoinStage::Handshaking;
            _joinDeadline = std::chrono::steady_clock::now() + HANDSHAKE_TIMEOUT;
            return Step::Pending;
        }
        break;

    case JoinStage::Handshaking:
        step = _stepHandshake();
        if (step == Step::Done) {
            _joinStage = JoinStage::None;
            _lastError = NetError::None;
            if (_onJoinResult) {
                auto callback = std::move(_onJoinResult);
                _onJoinResult = nullptr;
                callback(true, NetError::None);
            }
            return Step::Done;
        }
        break;

    default:
        break;
    }

    return step == Step::Failed ? _failJoin() : Step::Pending;
}

Net::Step Net::_failJoin() {
    // **Which stage gave up, because the error alone cannot tell you.**
    //
    // "Timed out" and "refused" each mean something different depending on how far the join got,
    // and the difference decides where to look. Stuck *connecting* is the network — ICE never
    // found a path, which is the NAT case and wants a relay. Failing while *handshaking* means a
    // path was found and the two sides then disagreed about the build or the packet layout, which
    // is a code problem and no amount of TURN will help it. Without this the two are
    // indistinguishable from the outside, and the obvious guess is the wrong one.
    const char *stage = "";
    switch (_joinStage) {
    case JoinStage::Resolving:   stage = "while asking the brokerage where to go"; break;
    case JoinStage::Connecting:  stage = "while negotiating a path (ICE) — this is the NAT case"; break;
    case JoinStage::Handshaking: stage = "during the handshake — a path was found, so this is "
                                         "build id or packet layout, not the network"; break;
    case JoinStage::None:        stage = "after the join had already finished"; break;
    }
    LOG_WARNING("Net: join failed {}", stage);

    _joinStage = JoinStage::None;
    if (_transport)
        _transport->Disconnect();
    if (_onJoinResult) {
        auto callback = std::move(_onJoinResult);
        _onJoinResult = nullptr;
        callback(false, _lastError);
    }
    return Step::Failed;
}

bool Net::_join(const Endpoint &ep) {
    if (!_beginJoin(ep)) {
        _joinStage = JoinStage::None;
        return false;
    }
    // Blocking, as it always has been. The waiting yields rather than sleeps, because on the
    // web nothing else runs — including the negotiation being waited on.
    for (;;) {
        const Step step = _advanceJoin();
        if (step == Step::Done)
            return true;
        if (step == Step::Failed)
            return false;
        brokerYield();
    }
}

void Net::_joinAsync(const Endpoint &ep, std::function<void(bool, NetError)> onResult) {
    _onJoinResult = std::move(onResult);
    if (!_beginJoin(ep)) {
        _joinStage = JoinStage::None;
        if (_onJoinResult) {
            auto callback = std::move(_onJoinResult);
            _onJoinResult = nullptr;
            callback(false, _lastError);
        }
    }
}

void Net::_disconnect() {
    if (_broker)
        _broker->LeaveLobby();
    if (_transport)
        _transport->Disconnect();
    _peerIds.clear();
    pendingPeers.clear();
    deferredEvents.clear();

    // The room belonged to the session that just ended. Left set, a lobby would go on showing a
    // code that no longer reaches anything.
    _roomCode.clear();
}

void Net::_pumpBroker() {
    if (!_broker)
        return;
    if (_transport && _transport->IsServer())
        _broker->SetPlayerCount((int)_peerIds.size() + 1); // the peers plus whoever is hosting
    _broker->Tick();

    std::vector<BrokerEvent> events;
    _broker->Poll(events);
    for (const BrokerEvent &ev : events) {
        if (ev.type == BrokerEvent::LobbyList && _onSessions)
            _onSessions(ev.sessions);
        else if (ev.type == BrokerEvent::InviteAccepted && _onInvite)
            _onInvite(ev.endpoint);
        else if (ev.type == BrokerEvent::SignalReceived && _transport)
            _transport->DeliverSignal(ev.from, ev.data.data(), (uint32_t)ev.data.size());
        else if (ev.type == BrokerEvent::LobbyCreated)
            // Kept so the host can show it. There is nowhere else to learn it: an Endpoint::code
            // describes somebody else's session, and a host is not in its own session list.
            _roomCode = ev.endpoint.code;
    }
}

void Net::_update() {
    // A join started through the callback overload is still in flight; drive it here rather
    // than blocking the frame the way the synchronous one does.
    if (_joinStage != JoinStage::None) {
        _advanceJoin();
        return;
    }

    _pumpBroker();
    if (!_transport)
        return;

    for (const TransportEvent &ev : deferredEvents)
        _handleEvent(ev);
    deferredEvents.clear();

    static std::vector<TransportEvent> events;
    events.clear();
    _transport->Poll(events);
    for (const TransportEvent &ev : events)
        _handleEvent(ev);

    // Drop anyone who connected and then never said who they were.
    const auto now = std::chrono::steady_clock::now();
    for (auto it = pendingPeers.begin(); it != pendingPeers.end();) {
        if (now - it->second > HANDSHAKE_TIMEOUT) {
            _transport->DisconnectPeer(it->first);
            it = pendingPeers.erase(it);
        } else {
            ++it;
        }
    }
}

void Net::_handleEvent(const TransportEvent &ev) {
    switch (ev.type) {
    case TransportEvent::Connect:
        pendingPeers[ev.peer] = std::chrono::steady_clock::now();
        break;

    case TransportEvent::Disconnect:
        pendingPeers.erase(ev.peer);
        if (_peerIds.erase(ev.peer) && _onPeerLeft)
            _onPeerLeft(ev.peer);
        break;

    case TransportEvent::Receive: {
        if (ev.data.size() < sizeof(uint32_t))
            break;
        uint32_t typeId;
        std::memcpy(&typeId, ev.data.data(), sizeof(uint32_t));
        const void *payload = ev.data.data() + sizeof(uint32_t);
        const auto  size    = (uint32_t)(ev.data.size() - sizeof(uint32_t));

        if (typeId == HANDSHAKE_TYPE_ID) {
            // The client resolves its own handshake inside _join; only the server vets one here.
            if (_transport->IsServer())
                _handleHandshake(ev.peer, payload, size);
            break;
        }
        // Nothing a peer says before it has been accepted is worth acting on.
        if (_peerIds.find(ev.peer) == _peerIds.end())
            break;
        _dispatch(ev.peer, typeId, payload, size);
        break;
    }
    }
}

// Server side of the handshake: vet the build, then either admit the peer or say why not.
void Net::_handleHandshake(Peer peer, const void *data, uint32_t size) {
    Handshake reply;
    reply.player  = _localPlayer();
    reply.layouts = _layoutDigest();

    Handshake   hello;
    const char *why = nullptr;
    if (size != sizeof(Handshake)) {
        why = "handshake is a different size";
    } else {
        std::memcpy(&hello, data, sizeof(hello));
        if (hello.magic != HANDSHAKE_MAGIC || hello.protocol != PROTOCOL_VERSION)
            why = "different engine protocol";
        else if (hello.buildId != _buildId)
            why = "different build id";
        else if (hello.layouts != reply.layouts)
            why = "packets are laid out differently";
    }

    if (why) {
        reply.kind   = Handshake::Reject;
        reply.reason = (uint8_t)NetError::VersionMismatch;
        _sendRaw(peer, HANDSHAKE_TYPE_ID, &reply, sizeof(reply), true);
        LOG_WARNING("Net: refused peer {} — {}", peer, why);
        if (hello.layouts != reply.layouts)
            logLayouts(_layouts);
        pendingPeers.erase(peer);
        _transport->DisconnectPeer(peer);
        return;
    }

    reply.kind = Handshake::Accept;
    _sendRaw(peer, HANDSHAKE_TYPE_ID, &reply, sizeof(reply), true);
    pendingPeers.erase(peer);
    _peerIds[peer] = hello.player;
    if (_onPeerJoined)
        _onPeerJoined(peer);
}

void Net::_dispatch(Peer peer, uint32_t typeId, const void *data, uint32_t size) {
    auto it = _handlers.find(typeId);
    if (it != _handlers.end())
        it->second(peer, data, size);
}

bool      Net::_isServer() { return _transport && _transport->IsServer(); }
bool      Net::_isClient() { return _transport && _transport->IsClient(); }
Net::Peer Net::_getClientID() { return _transport ? _transport->SelfId() : 0; }
uint32_t  Net::_getPeerCount() { return _transport ? _transport->PeerCount() : 0; }
uint32_t  Net::_getPing(Peer peer) { return _transport ? _transport->Ping(peer) : 0; }
uint32_t  Net::_maxMessageSize() { return _transport ? _transport->MaxMessageSize() : 0; }
bool      Net::_can(Feature feature) { return _broker && _broker->Has(feature); }

void Net::_setBuildId(uint64_t id) {
    _buildId = id;
    if (_broker)
        _broker->SetBuildId(id);
}

void Net::_querySessions() {
    if (_can(Feature::Browse))
        _broker->QueryLobbies(LobbyFilter{});
}
PlayerId  Net::_localPlayer() { return _broker ? _broker->LocalPlayer() : PlayerId{}; }

PlayerId Net::_idOf(Peer peer) {
    auto it = _peerIds.find(peer);
    return it == _peerIds.end() ? PlayerId{} : it->second;
}

Net::Peer Net::_peerOf(const PlayerId &id) {
    for (const auto &[peer, other] : _peerIds)
        if (other == id)
            return peer;
    return 0;
}

// Build [typeId][payload] once, reused by send + broadcast.
static std::vector<uint8_t> frame(uint32_t typeId, const void *data, uint32_t size) {
    std::vector<uint8_t> buf(sizeof(uint32_t) + size);
    std::memcpy(buf.data(), &typeId, sizeof(uint32_t));
    if (size)
        std::memcpy(buf.data() + sizeof(uint32_t), data, size);
    return buf;
}

void Net::_sendRaw(Peer peer, uint32_t typeId, const void *data, uint32_t size, bool reliable) {
    if (!_transport)
        return;
    auto buf = frame(typeId, data, size);
    _transport->Send(peer, buf.data(), (uint32_t)buf.size(), reliable);
}

void Net::_broadcastRaw(uint32_t typeId, const void *data, uint32_t size, bool reliable) {
    if (!_transport)
        return;
    auto buf = frame(typeId, data, size);
    _transport->Broadcast(buf.data(), (uint32_t)buf.size(), reliable);
}

void Net::_registerRaw(uint32_t typeId, std::function<void(Peer, const void *, uint32_t)> handler) {
    _handlers[typeId] = std::move(handler);
}

void Net::_noteLayout(uint32_t typeId, uint32_t size) {
    _layouts[typeId] = size;
}

// FNV-1a over the sorted (type id, size) pairs. Ordered, so two peers that registered the
// same packets in a different order still agree.
uint64_t Net::_layoutDigest() const {
    uint64_t hash = 1469598103934665603ull;
    auto     fold = [&hash](uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            hash ^= (uint8_t)(value >> (i * 8));
            hash *= 1099511628211ull;
        }
    };
    for (const auto &[typeId, size] : _layouts) {
        fold(typeId);
        fold(size);
    }
    return hash;
}

