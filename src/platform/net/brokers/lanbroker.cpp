// Broker 2: LAN discovery over UDP broadcast. Needs no service and no accounts, so it is
// always compiled in and is what Auto settles on when no platform brokerage is running.
//
// Clients broadcast a probe and hosts answer it directly. The other way round — hosts
// broadcasting a beacon on a timer — puts traffic on every machine on the subnet whether it
// is playing or not, which is the failure mode SDL_net's own docs warn about.
//
// Wire format, little-endian, one datagram either way:
//
//   header    [u32 magic]["LNAN"] [u16 version] [u8 kind]
//   probe     [u64 buildId]
//   announce  [u64 buildId] [u8 idProvider][u8 idLen][id] [u16 gamePort]
//             [u16 players] [u16 slots] [u8 nameLen][name] [u16 blobLen][blob]

#include "platform/net/brokers/brokers.h"
#include "core/log/log.h"

#include <chrono>
#include <cstring>

namespace {

constexpr uint16_t DISCOVERY_PORT = 28770;
constexpr uint32_t ANNOUNCE_MAGIC = 0x4C4E414E; // 'LNAN'
constexpr uint16_t ANNOUNCE_VERSION = 1;

enum : uint8_t { KindProbe = 0,
    KindAnnounce         = 1 };

constexpr size_t MAX_PACKET = 512;
constexpr size_t MAX_NAME   = 64;
constexpr size_t MAX_BLOB   = 256;

// How long a query keeps re-probing, and how often, so a host that starts late is still found.
constexpr auto QUERY_WINDOW   = std::chrono::seconds(5);
constexpr auto PROBE_INTERVAL = std::chrono::seconds(1);

// A host with several interfaces answers one probe from each of them, so the same session
// arrives under several addresses. They are ranked and the best-looking one is kept.
int addressRank(const std::string &address) {
    if (address.find('%') != std::string::npos || address.rfind("fe80", 0) == 0)
        return 0; // link-local, only reachable from the interface it was learned on
    if (address.find(':') != std::string::npos)
        return 1; // some other IPv6
    return 2;     // IPv4
}

// Writes little-endian fields into a fixed buffer, refusing to run past the end.
class Writer {
public:
    Writer(uint8_t *data, size_t capacity) : _data(data), _capacity(capacity) { }

    template <typename T>
    void put(T value) {
        if (_size + sizeof(T) > _capacity) {
            _ok = false;
            return;
        }
        std::memcpy(_data + _size, &value, sizeof(T));
        _size += sizeof(T);
    }

    void putBytes(const void *src, size_t count) {
        if (_size + count > _capacity) {
            _ok = false;
            return;
        }
        std::memcpy(_data + _size, src, count);
        _size += count;
    }

    bool   ok() const { return _ok; }
    size_t size() const { return _size; }

private:
    uint8_t *_data;
    size_t   _capacity;
    size_t   _size = 0;
    bool     _ok   = true;
};

// Reads the same fields back, refusing to run past the end of the datagram.
class Reader {
public:
    Reader(const uint8_t *data, size_t size) : _data(data), _size(size) { }

    template <typename T>
    T get() {
        T value{};
        if (_pos + sizeof(T) > _size) {
            _ok = false;
            return value;
        }
        std::memcpy(&value, _data + _pos, sizeof(T));
        _pos += sizeof(T);
        return value;
    }

    std::vector<uint8_t> getBytes(size_t count) {
        if (_pos + count > _size) {
            _ok = false;
            return {};
        }
        std::vector<uint8_t> out(_data + _pos, _data + _pos + count);
        _pos += count;
        return out;
    }

    bool ok() const { return _ok; }

private:
    const uint8_t *_data;
    size_t         _size;
    size_t         _pos = 0;
    bool           _ok  = true;
};

class LanBroker : public IBroker {
public:
    ~LanBroker() override { Shutdown(); }

    const char *Name() const override { return "lan"; }

    bool Init() override {
        // Doubles as the platform check: no datagram socket means nothing to discover with.
        _probe = Net::Udp::OpenBroadcast(0);
        if (!_probe)
            return false;
        _self = mintProcessIdentity();
        return true;
    }

    void Shutdown() override {
        LeaveLobby();
        if (_probe) {
            Net::Udp::Close(_probe);
            _probe = nullptr;
        }
        _sessions.clear();
        _events.clear();
    }

    void SetBuildId(uint64_t id) override { _buildId = id; }

    bool Has(Net::Feature feature) const override { return feature == Net::Feature::Browse; }

    PlayerId LocalPlayer() const override { return _self; }

    // Hosting here means "answer probes", so the beacon socket is the whole of it.
    bool CreateLobby(const Net::HostConfig &cfg) override {
        LeaveLobby();
        _beacon = Net::Udp::OpenBroadcast(DISCOVERY_PORT);
        if (!_beacon) {
            LOG_WARNING("Net: LAN discovery port {} unavailable, session will not be listed", DISCOVERY_PORT);
            return false;
        }
        _name     = cfg.name.substr(0, MAX_NAME);
        _gamePort = cfg.port;
        _slots    = cfg.slots;
        _blob     = cfg.blob;
        if (_blob.size() > MAX_BLOB)
            _blob.resize(MAX_BLOB);
        return true;
    }

    bool JoinLobby(const Net::Endpoint &) override { return false; }

    void LeaveLobby() override {
        if (_beacon) {
            Net::Udp::Close(_beacon);
            _beacon = nullptr;
        }
    }

    // Player counts change without the session list changing, so the host keeps this current.
    void SetPlayerCount(int players) override { _players = players; }

    void SetLobbyData(const char *, const char *) override { }

    bool QueryLobbies(const LobbyFilter &filter) override {
        _sessions.clear();
        _maxResults  = filter.maxResults;
        _queryUntil  = std::chrono::steady_clock::now() + QUERY_WINDOW;
        _nextProbe   = std::chrono::steady_clock::time_point{};
        return true;
    }

    bool SendSignal(const PlayerId &, const void *, uint32_t) override { return false; }

    void Tick() override {
        if (_beacon)
            _answerProbes();
        if (std::chrono::steady_clock::now() < _queryUntil)
            _runQuery();
    }

    void Poll(std::vector<BrokerEvent> &out) override {
        for (BrokerEvent &ev : _events)
            out.push_back(std::move(ev));
        _events.clear();
    }

private:
    void _answerProbes() {
        uint8_t           buf[MAX_PACKET];
        Net::Udp::Address from;
        int               size;
        while ((size = Net::Udp::Recv(_beacon, from, buf, sizeof(buf))) > 0) {
            Reader reader(buf, (size_t)size);
            if (reader.get<uint32_t>() == ANNOUNCE_MAGIC &&
                reader.get<uint16_t>() == ANNOUNCE_VERSION &&
                reader.get<uint8_t>() == KindProbe &&
                reader.get<uint64_t>() == _buildId && reader.ok()) {
                _sendAnnounce(from);
            }
            Net::Udp::Free(from);
        }
    }

    void _sendAnnounce(const Net::Udp::Address &to) {
        uint8_t buf[MAX_PACKET];
        Writer  writer(buf, sizeof(buf));
        writer.put<uint32_t>(ANNOUNCE_MAGIC);
        writer.put<uint16_t>(ANNOUNCE_VERSION);
        writer.put<uint8_t>(KindAnnounce);
        writer.put<uint64_t>(_buildId);
        writer.put<uint8_t>((uint8_t)_self.provider);
        writer.put<uint8_t>(_self.length);
        writer.putBytes(_self.bytes.data(), _self.length);
        writer.put<uint16_t>(_gamePort);
        writer.put<uint16_t>((uint16_t)_players);
        writer.put<uint16_t>((uint16_t)_slots);
        writer.put<uint8_t>((uint8_t)_name.size());
        writer.putBytes(_name.data(), _name.size());
        writer.put<uint16_t>((uint16_t)_blob.size());
        writer.putBytes(_blob.data(), _blob.size());
        if (writer.ok())
            Net::Udp::Send(_beacon, to, buf, (int)writer.size());
    }

    void _runQuery() {
        const auto now = std::chrono::steady_clock::now();
        if (now >= _nextProbe) {
            _nextProbe = now + PROBE_INTERVAL;
            uint8_t buf[16];
            Writer  writer(buf, sizeof(buf));
            writer.put<uint32_t>(ANNOUNCE_MAGIC);
            writer.put<uint16_t>(ANNOUNCE_VERSION);
            writer.put<uint8_t>(KindProbe);
            writer.put<uint64_t>(_buildId);
            Net::Udp::Broadcast(_probe, DISCOVERY_PORT, buf, (int)writer.size());
        }

        uint8_t           buf[MAX_PACKET];
        Net::Udp::Address from;
        int               size;
        bool              changed = false;
        while ((size = Net::Udp::Recv(_probe, from, buf, sizeof(buf))) > 0) {
            changed |= _readAnnounce(buf, (size_t)size, from);
            Net::Udp::Free(from);
        }
        if (changed) {
            BrokerEvent ev;
            ev.type     = BrokerEvent::LobbyList;
            ev.sessions = _sessions;
            _events.push_back(std::move(ev));
        }
    }

    bool _readAnnounce(const uint8_t *data, size_t size, const Net::Udp::Address &from) {
        Reader reader(data, size);
        if (reader.get<uint32_t>() != ANNOUNCE_MAGIC || reader.get<uint16_t>() != ANNOUNCE_VERSION ||
            reader.get<uint8_t>() != KindAnnounce || reader.get<uint64_t>() != _buildId)
            return false;

        Net::SessionInfo info;
        info.host.provider        = (PlayerId::Provider)reader.get<uint8_t>();
        info.host.length          = reader.get<uint8_t>();
        const auto hostBytes      = reader.getBytes(info.host.length);
        const uint16_t   gamePort = reader.get<uint16_t>();
        info.players              = reader.get<uint16_t>();
        info.slots                = reader.get<uint16_t>();
        const auto name           = reader.getBytes(reader.get<uint8_t>());
        info.blob                 = reader.getBytes(reader.get<uint16_t>());
        if (!reader.ok() || info.host.length > info.host.bytes.size())
            return false;
        std::memcpy(info.host.bytes.data(), hostBytes.data(), info.host.length);

        info.name          = std::string(name.begin(), name.end());
        info.endpoint.kind = Net::Endpoint::Kind::Address;
        // The address the reply came from is the one that can reach the host, whatever the
        // host believes its own address to be.
        info.endpoint.address = Net::Udp::ToString(from);
        info.endpoint.address = info.endpoint.address.substr(0, info.endpoint.address.rfind(':'));
        info.endpoint.port    = gamePort;

        for (Net::SessionInfo &known : _sessions) {
            if (!(known.host == info.host))
                continue;
            // Same host on a second interface: keep whichever address looks more useful.
            if (addressRank(info.endpoint.address) <= addressRank(known.endpoint.address))
                info.endpoint = known.endpoint;
            const bool same = known.players == info.players && known.name == info.name &&
                              known.endpoint.address == info.endpoint.address;
            known = info;
            return !same;
        }
        if ((int)_sessions.size() >= _maxResults)
            return false;
        _sessions.push_back(std::move(info));
        return true;
    }

    PlayerId          _self;
    uint64_t          _buildId = 0;
    Net::Udp::Socket  _probe   = nullptr; // client: broadcasts probes, collects announces
    Net::Udp::Socket  _beacon  = nullptr; // host: listens on the discovery port, replies directly

    std::string          _name;
    std::vector<uint8_t> _blob;
    uint16_t             _gamePort = 0;
    int                  _players  = 0;
    int                  _slots    = 0;

    std::vector<Net::SessionInfo>         _sessions;
    std::vector<BrokerEvent>              _events;
    int                                   _maxResults = 50;
    std::chrono::steady_clock::time_point _queryUntil{};
    std::chrono::steady_clock::time_point _nextProbe{};
};

} // namespace

/// @cond INTERNAL
IBroker *createLanBroker() {
    return new LanBroker();
}
/// @endcond
