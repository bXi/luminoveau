// Broker 3: a signalling service. The only brokerage that introduces a browser to a native
// peer, because it is the only one whose transport a browser can speak.
//
// It carries three things and no more: the room list, who is hosting a room, and the blobs
// the WebRTC transport needs to exchange before its direct path is up. Game traffic never
// touches it — once the data channels open the service is out of the loop, which is why a
// small box can host a lot of sessions.
//
// rtc::WebSocket exists in both libdatachannel and datachannel-wasm, so this file, like the
// transport beside it, is written once and compiled for both.

#include "platform/net/brokers/brokers.h"
#include "platform/net/signalprotocol.h"
#include "core/log/log.h"

#include <rtc/rtc.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <exception>
#include <filesystem>
#include <mutex>

namespace {

using json = nlohmann::json;

// Long enough to cross an ocean and back, short enough that a wrong URL is not mistaken for
// a slow one.
constexpr auto CONNECT_TIMEOUT = std::chrono::seconds(10);

std::string toHex(const PlayerId &id) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string           out;
    out.reserve(2 + id.length * 2);
    out += digits[((uint8_t)id.provider >> 4) & 0x0f];
    out += digits[(uint8_t)id.provider & 0x0f];
    for (uint8_t i = 0; i < id.length; ++i) {
        out += digits[id.bytes[i] >> 4];
        out += digits[id.bytes[i] & 0x0f];
    }
    return out;
}

PlayerId fromHex(const std::string &text) {
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    };

    PlayerId id;
    if (text.size() < 2 || text.size() % 2 != 0)
        return id;

    const int hi = value(text[0]), lo = value(text[1]);
    if (hi < 0 || lo < 0)
        return id;
    id.provider = (PlayerId::Provider)((hi << 4) | lo);

    const size_t count = (text.size() - 2) / 2;
    if (count > id.bytes.size())
        return PlayerId{};
    for (size_t i = 0; i < count; ++i) {
        const int a = value(text[2 + i * 2]), b = value(text[3 + i * 2]);
        if (a < 0 || b < 0)
            return PlayerId{};
        id.bytes[i] = (uint8_t)((a << 4) | b);
    }
    id.length = (uint8_t)count;
    return id;
}

class SignalBroker : public IBroker {
public:
    ~SignalBroker() override { Shutdown(); }

    const char *Name() const override { return "signal"; }

    void SetUrl(std::string url) { _url = std::move(url); }

    bool Init() override {
        if (_url.empty()) {
            LOG_WARNING("Net: no signalling service configured — see Net::SetSignalingUrl");
            return false;
        }

        _self = mintProcessIdentity();
        _self.provider = PlayerId::Provider::Custom;

        _socket = _openSocket();
        _socket->onOpen([this]() { _connected = true; });
        _socket->onClosed([this]() { _connected = false; });
        _socket->onError([this](std::string error) {
            LOG_WARNING("Net: signalling error: {}", error);
            _connected = false;
            _failed    = true;
        });
        _socket->onMessage([this](rtc::message_variant message) {
            if (std::holds_alternative<std::string>(message))
                _receive(std::get<std::string>(message));
        });
        _socket->open(_url);

        const auto deadline = std::chrono::steady_clock::now() + CONNECT_TIMEOUT;
        while (!_connected && !_failed && std::chrono::steady_clock::now() < deadline)
            brokerYield();

        if (!_connected) {
            LOG_WARNING("Net: could not reach the signalling service at {}", _url);
            _socket.reset();
            return false;
        }

        _send({ { "op", SignalProtocol::Op::Hello },
            { "version", SignalProtocol::VERSION },
            { "id", toHex(_self) },
            { "build", std::to_string(_buildId) } });
        return true;
    }

    void Shutdown() override {
        if (_socket) {
            _socket->close();
            _socket.reset();
        }
        _connected = false;
        _events.clear();
    }

    void Tick() override { } // the socket delivers on its own; Poll drains what it left

    void SetBuildId(uint64_t id) override {
        _buildId = id;
        if (_connected)
            _send({ { "op", SignalProtocol::Op::Hello },
                { "version", SignalProtocol::VERSION },
                { "id", toHex(_self) },
                { "build", std::to_string(_buildId) } });
    }

    bool Has(Net::Feature feature) const override {
        if (feature == Net::Feature::Relay) {
            // Only a TURN server can carry a peer that cannot be punched through. Saying yes
            // without one would promise a connection this cannot deliver.
            for (const std::string &server : Net::IceServers())
                if (server.rfind("turn:", 0) == 0 || server.rfind("turns:", 0) == 0)
                    return true;
            return false;
        }
        return feature == Net::Feature::Browse || feature == Net::Feature::Crossplay;
    }

    PlayerId LocalPlayer() const override { return _self; }

    bool CarriesSignals() const override { return true; }

    bool CreateLobby(const Net::HostConfig &cfg) override {
        if (!_connected)
            return false;
        _send({ { "op", SignalProtocol::Op::Host },
            { "name", cfg.name },
            { "slots", cfg.slots },
            { "blob", SignalProtocol::base64Encode(cfg.blob.data(), cfg.blob.size()) } });
        return true;
    }

    bool JoinLobby(const Net::Endpoint &ep) override {
        if (!_connected)
            return false;
        const std::string room = ep.kind == Net::Endpoint::Kind::JoinCode ? ep.code : ep.address;
        if (room.empty())
            return false;
        _send({ { "op", SignalProtocol::Op::Join }, { "room", room } });
        return true;
    }

    void LeaveLobby() override {
        if (_connected)
            _send({ { "op", SignalProtocol::Op::Leave } });
    }

    void SetPlayerCount(int players) override {
        if (_connected && players != _players) {
            _players = players;
            _send({ { "op", SignalProtocol::Op::Host }, { "players", players } });
        }
    }

    void SetLobbyData(const char *, const char *) override { }

    bool QueryLobbies(const LobbyFilter &) override {
        if (!_connected)
            return false;
        _send({ { "op", SignalProtocol::Op::List } });
        return true;
    }

    bool SendSignal(const PlayerId &to, const void *data, uint32_t size) override {
        if (!_connected)
            return false;
        _send({ { "op", SignalProtocol::Op::Signal },
            { "to", toHex(to) },
            { "data", SignalProtocol::base64Encode((const uint8_t *)data, size) } });
        return true;
    }

    void Poll(std::vector<BrokerEvent> &out) override {
        std::vector<BrokerEvent> drained;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            drained.swap(_events);
        }
        for (BrokerEvent &ev : drained)
            out.push_back(std::move(ev));
    }

    // Where the room code lands once the service has minted one, so a game can show it.
    std::string Room() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _room;
    }

private:
    // The browser brings its own trust store and its own keepalive; natively both have to be
    // supplied, because Mbed TLS trusts nothing until it is told to.
    std::shared_ptr<rtc::WebSocket> _openSocket() const {
#ifdef __EMSCRIPTEN__
        return std::make_shared<rtc::WebSocket>();
#else
        rtc::WebSocketConfiguration config;

        std::string cacert = Net::SignalingCaCert();
        if (cacert.empty())
            cacert = _findSystemCaBundle();
        if (!cacert.empty())
            config.caCertificatePemFile = cacert;
        else
            LOG_WARNING("Net: no CA certificates found — a wss:// service will be refused; "
                        "see Net::SetSignalingCaCert");

        // A signalling connection goes quiet the moment a session is negotiated, and a proxy
        // or a NAT will drop a silent socket long before the race ends. The host needs its
        // one alive to admit anybody who joins later.
        config.pingInterval = std::chrono::seconds(30);

        return std::make_shared<rtc::WebSocket>(std::move(config));
#endif
    }

#ifndef __EMSCRIPTEN__
    // Where the usual Unix distributions keep their trust store. Windows keeps its somewhere
    // Mbed TLS cannot read, so a game shipping there supplies its own.
    static std::string _findSystemCaBundle() {
        static constexpr const char *candidates[] = {
            "/etc/ssl/certs/ca-certificates.crt",  // Debian, Ubuntu, Alpine
            "/etc/pki/tls/certs/ca-bundle.crt",    // Fedora, RHEL
            "/etc/ssl/ca-bundle.pem",              // openSUSE
            "/etc/ssl/cert.pem",                   // macOS, FreeBSD
            "/usr/local/share/certs/ca-root-nss.crt",
        };
        for (const char *path : candidates)
            if (std::filesystem::exists(path))
                return path;
        return {};
    }
#endif

    void _send(const json &message) {
        if (!_socket)
            return;
        // The service going away must not take the game with it — a session already
        // negotiated keeps running without it.
        try {
            _socket->send(message.dump());
        } catch (const std::exception &e) {
            LOG_WARNING("Net: signalling send failed: {}", e.what());
            _connected = false;
        }
    }

    void _receive(const std::string &text) {
        json message = json::parse(text, nullptr, false);
        if (message.is_discarded() || !message.contains("op"))
            return;
        const std::string op = message.value("op", "");

        if (op == SignalProtocol::Op::Hosting) {
            std::lock_guard<std::mutex> lock(_mutex);
            _room = message.value("room", "");
            BrokerEvent ev;
            ev.type           = BrokerEvent::LobbyCreated;
            ev.from           = _self;
            ev.endpoint.kind  = Net::Endpoint::Kind::JoinCode;
            ev.endpoint.code  = _room;
            _events.push_back(std::move(ev));

        } else if (op == SignalProtocol::Op::Sessions) {
            BrokerEvent ev;
            ev.type = BrokerEvent::LobbyList;
            for (const auto &entry : message.value("sessions", json::array())) {
                Net::SessionInfo info;
                info.endpoint.kind = Net::Endpoint::Kind::JoinCode;
                info.endpoint.code = entry.value("room", "");
                info.host          = fromHex(entry.value("host", ""));
                info.name          = entry.value("name", "");
                info.players       = entry.value("players", 0);
                info.slots         = entry.value("slots", 0);
                info.blob          = SignalProtocol::base64Decode(entry.value("blob", ""));
                ev.sessions.push_back(std::move(info));
            }
            _queue(std::move(ev));

        } else if (op == SignalProtocol::Op::Joined) {
            // The room was only an introduction; what the transport dials is the host.
            BrokerEvent ev;
            ev.type            = BrokerEvent::LobbyJoined;
            ev.from            = fromHex(message.value("host", ""));
            ev.endpoint.kind   = Net::Endpoint::Kind::Player;
            ev.endpoint.player = ev.from;
            _queue(std::move(ev));

        } else if (op == SignalProtocol::Op::Signal) {
            BrokerEvent ev;
            ev.type = BrokerEvent::SignalReceived;
            ev.from = fromHex(message.value("from", ""));
            ev.data = SignalProtocol::base64Decode(message.value("data", ""));
            _queue(std::move(ev));

        } else if (op == SignalProtocol::Op::Peer) {
            BrokerEvent ev;
            ev.type = BrokerEvent::LobbyMemberChanged;
            ev.from = fromHex(message.value("id", ""));
            _queue(std::move(ev));

        } else if (op == SignalProtocol::Op::Error) {
            LOG_WARNING("Net: signalling refused: {}", message.value("reason", "unknown"));
            BrokerEvent ev;
            ev.type  = BrokerEvent::Error;
            ev.error = Net::NetError::Refused;
            _queue(std::move(ev));
        }
    }

    void _queue(BrokerEvent &&ev) {
        std::lock_guard<std::mutex> lock(_mutex);
        _events.push_back(std::move(ev));
    }

    std::shared_ptr<rtc::WebSocket> _socket;
    std::string                     _url;
    std::string                     _room;
    PlayerId                        _self;
    uint64_t                        _buildId  = 0;
    int                             _players  = 0;
    bool                            _connected = false;
    bool                            _failed    = false;

    std::vector<BrokerEvent> _events;
    mutable std::mutex       _mutex;
};

} // namespace

/// @cond INTERNAL
IBroker *createSignalBroker(const std::string &url) {
    auto *broker = new SignalBroker();
    broker->SetUrl(url);
    return broker;
}
/// @endcond
