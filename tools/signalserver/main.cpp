// The signalling service. Introduces peers to each other and then gets out of the way.
//
// It knows three things: which rooms exist, who hosts each one, and how to pass a blob from
// one peer to another. It never sees game traffic and never parses SDP — once the data
// channels open it is idle, which is why one small box serves a lot of sessions.
//
// Terminate TLS in front of it. Browsers require wss:// from an https page, and nginx
// already holds the certificate:
//
//     location /signal {
//         proxy_pass http://127.0.0.1:9000;
//         proxy_http_version 1.1;
//         proxy_set_header Upgrade $http_upgrade;
//         proxy_set_header Connection "upgrade";
//         proxy_read_timeout 1h;    # signalling connections idle once a session is up
//     }
//
// Build:  cmake -B build -DLUMINOVEAU_WITH_WEBRTC=ON && cmake --build build --target lumi-signalserver

#include "platform/net/signalprotocol.h"

#include <rtc/rtc.hpp>

#include <nlohmann/json.hpp>

// For the TURN credential HMAC. libdatachannel already brings mbedtls in here.
#include <mbedtls/md.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>

namespace {

using json = nlohmann::json;

struct Client {
    std::shared_ptr<rtc::WebSocket> socket;
    std::string                     id;    // the peer's PlayerId, hex
    std::string                     build; // refuses to introduce mismatched builds
    std::string                     room;  // empty unless in one
};

struct Room {
    std::string code;
    std::string host; // client id
    std::string name;
    std::string build;
    std::string blob;
    int         slots   = 0;
    int         players = 1;
};

std::mutex                                          mutex;
std::map<rtc::WebSocket *, std::shared_ptr<Client>> clients;
std::map<std::string, Room>                         rooms;

std::string makeRoomCode() {
    static std::mt19937 gen{ std::random_device{}() };
    const std::string   alphabet = SignalProtocol::ROOM_ALPHABET;
    std::uniform_int_distribution<size_t> pick(0, alphabet.size() - 1);

    for (int attempt = 0; attempt < 64; ++attempt) {
        std::string code;
        for (int i = 0; i < SignalProtocol::ROOM_LENGTH; ++i)
            code += alphabet[pick(gen)];
        if (rooms.find(code) == rooms.end())
            return code;
    }
    return {}; // the alphabet is exhausted long before this is reachable in practice
}

void send(const std::shared_ptr<Client> &client, const json &message) {
    if (client && client->socket && client->socket->isOpen())
        client->socket->send(message.dump());
}

void fail(const std::shared_ptr<Client> &client, const char *reason) {
    send(client, { { "op", SignalProtocol::Op::Error }, { "reason", reason } });
}

std::shared_ptr<Client> clientById(const std::string &id) {
    for (auto &[socket, client] : clients)
        if (client->id == id)
            return client;
    return nullptr;
}

// Leaving is the same whether it was asked for or the socket simply died.
void leaveRoom(const std::shared_ptr<Client> &client) {
    if (client->room.empty())
        return;
    auto it = rooms.find(client->room);
    const std::string room = client->room;
    client->room.clear();
    if (it == rooms.end())
        return;

    if (it->second.host == client->id) {
        // The host left, so the room is over. Everyone still in it is told, rather than
        // being left waiting on a peer that will never answer.
        for (auto &[socket, other] : clients) {
            if (other->room == room) {
                other->room.clear();
                send(other, { { "op", SignalProtocol::Op::Peer },
                    { "id", client->id },
                    { "state", "left" } });
            }
        }
        rooms.erase(it);
        return;
    }

    if (it->second.players > 1)
        --it->second.players;
    if (auto host = clientById(it->second.host))
        send(host, { { "op", SignalProtocol::Op::Peer },
            { "id", client->id },
            { "state", "left" } });
}

// ── Relay credentials ────────────────────────────────────────────────────────
//
// Set from the command line; empty means "no relay", and then this service only advertises the
// STUN servers (or nothing at all, and the client keeps its own default).
std::string turnUrls;   ///< Comma-separated, e.g. "turn:relay.example.org:3478,turns:relay.example.org:5349".
std::string turnSecret; ///< coturn's `static-auth-secret`. Never leaves this process.
std::string stunUrls;   ///< Comma-separated STUN servers to advertise alongside.
int         turnTtl = 12 * 60 * 60;

std::vector<std::string> split(const std::string &text, char separator) {
    std::vector<std::string> parts;
    size_t                   start = 0;
    while (start <= text.size()) {
        const size_t mark = text.find(separator, start);
        const size_t end  = mark == std::string::npos ? text.size() : mark;
        if (end > start)
            parts.push_back(text.substr(start, end - start));
        if (mark == std::string::npos)
            break;
        start = mark + 1;
    }
    return parts;
}

/// The ICE servers to hand a client, with fresh TURN credentials.
///
/// **coturn's REST scheme, which exists so a secret never has to reach a client.** The username is
/// an expiry timestamp; the password is `base64(HMAC-SHA1(secret, username))`. coturn recomputes
/// the same HMAC from its own copy of the secret, so no user list is configured anywhere and a
/// leaked credential stops working by itself.
json iceServers() {
    json list = json::array();

    for (const std::string &url : split(stunUrls, ','))
        list.push_back({ { "urls", url } });

    if (turnUrls.empty() || turnSecret.empty())
        return list;

    const auto expiry = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count() +
                        turnTtl;
    const std::string username = std::to_string(expiry);

    // mbedtls rather than OpenSSL: it is what libdatachannel already pulls in here, so this adds
    // no dependency.
    unsigned char            digest[20] = {};
    const mbedtls_md_info_t *sha1       = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (sha1 == nullptr ||
        mbedtls_md_hmac(sha1, reinterpret_cast<const unsigned char *>(turnSecret.data()),
                        turnSecret.size(),
                        reinterpret_cast<const unsigned char *>(username.data()), username.size(),
                        digest) != 0) {
        std::printf("warning: could not sign a TURN credential; no relay advertised\n");
        return list;
    }

    const std::string credential = SignalProtocol::base64Encode(digest, sizeof(digest));

    for (const std::string &url : split(turnUrls, ','))
        list.push_back({ { "urls", url }, { "username", username }, { "credential", credential } });

    return list;
}

void handle(const std::shared_ptr<Client> &client, const std::string &text) {
    json message = json::parse(text, nullptr, false);
    if (message.is_discarded() || !message.contains("op"))
        return;
    const std::string op = message.value("op", "");

    std::lock_guard<std::mutex> lock(mutex);

    if (op == SignalProtocol::Op::Hello) {
        if (message.value("version", 0) != SignalProtocol::VERSION) {
            fail(client, "unsupported protocol version");
            return;
        }
        client->id    = message.value("id", "");
        client->build = message.value("build", "");
        if (client->id.empty()) {
            fail(client, "missing id");
            return;
        }

        // **Two clients claiming one id must not both be admitted.** Signals are routed by id
        // (`clientById`), so a duplicate does not collide loudly — it *misroutes silently*.
        // Everything addressed to the second client is handed to the first, which then receives
        // its own offer, its own answer and its own candidates while the second hears nothing at
        // all. From inside the game that reads as a peer echoing itself: an answer applied in the
        // `stable` signalling state, and remote candidate lists carrying one's own ufrag.
        //
        // Ids are minted by clients, so this service cannot assume they are unique however
        // carefully that is done at the other end — one platform whose random source is not
        // random is enough, and a browser is exactly where that happens. Refusing the second
        // turns a silent misroute into something somebody can act on.
        for (const auto &[socket, other] : clients) {
            if (other == client || other->id != client->id)
                continue;
            std::printf("rejected a second client claiming id %s\n", client->id.c_str());
            client->id.clear();
            fail(client, "that id is already connected");
            return;
        }

        send(client, { { "op", SignalProtocol::Op::Welcome }, { "ice", iceServers() } });

    } else if (client->id.empty()) {
        fail(client, "say hello first");

    } else if (op == SignalProtocol::Op::Host) {
        if (message.contains("players") && !client->room.empty()) {
            auto it = rooms.find(client->room);
            if (it != rooms.end() && it->second.host == client->id)
                it->second.players = message.value("players", it->second.players);
            return;
        }
        leaveRoom(client);
        Room room;
        room.code  = makeRoomCode();
        room.host  = client->id;
        room.build = client->build;
        room.name  = message.value("name", "");
        room.slots = message.value("slots", 0);
        room.blob  = message.value("blob", "");
        if (room.code.empty()) {
            fail(client, "no room codes available");
            return;
        }
        client->room    = room.code;
        const auto code = room.code;
        rooms[code]     = std::move(room);
        send(client, { { "op", SignalProtocol::Op::Hosting }, { "room", code } });

    } else if (op == SignalProtocol::Op::List) {
        // Only rooms this client could actually join: a session it would be refused from is
        // worse than no session at all.
        json sessions = json::array();
        for (const auto &[code, room] : rooms) {
            if (room.build != client->build)
                continue;
            sessions.push_back({ { "room", code },
                { "host", room.host },
                { "name", room.name },
                { "players", room.players },
                { "slots", room.slots },
                { "blob", room.blob } });
        }
        send(client, { { "op", SignalProtocol::Op::Sessions }, { "sessions", sessions } });

    } else if (op == SignalProtocol::Op::Join) {
        auto it = rooms.find(message.value("room", ""));
        if (it == rooms.end()) {
            fail(client, "no such room");
            return;
        }
        if (it->second.build != client->build) {
            fail(client, "different build");
            return;
        }
        if (it->second.slots > 0 && it->second.players >= it->second.slots) {
            fail(client, "room is full");
            return;
        }
        leaveRoom(client);
        client->room = it->first;
        ++it->second.players;
        send(client, { { "op", SignalProtocol::Op::Joined }, { "host", it->second.host } });
        if (auto host = clientById(it->second.host))
            send(host, { { "op", SignalProtocol::Op::Peer },
                { "id", client->id },
                { "state", "joined" } });

    } else if (op == SignalProtocol::Op::Signal) {
        // Routed verbatim. The payload is the transport's business, not this service's.
        const std::string to     = message.value("to", "");
        auto              target = clientById(to);

        // **A dropped signal is silent on both ends, and that is the worst place for silence.**
        // The sender has already logged that it sent an offer; the recipient logs nothing at all,
        // because nothing arrived. From either side it looks like the peer went quiet, and the
        // negotiation simply times out with no indication that the service was the one that
        // discarded it.
        if (!target) {
            std::printf("signal from %s to %s dropped: no such client\n", client->id.c_str(),
                        to.c_str());
            return;
        }
        if (client->room.empty() || target->room != client->room) {
            std::printf("signal from %s to %s dropped: rooms differ (%s vs %s)\n",
                        client->id.c_str(), to.c_str(),
                        client->room.empty() ? "(none)" : client->room.c_str(),
                        target->room.empty() ? "(none)" : target->room.c_str());
            return;
        }
        send(target, { { "op", SignalProtocol::Op::Signal },
            { "from", client->id },
            { "data", message.value("data", "") } });

    } else if (op == SignalProtocol::Op::Leave) {
        leaveRoom(client);
    }
}

} // namespace

int main(int argc, char **argv) {
    uint16_t port = 9000;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--port" && i + 1 < argc)
            port = (uint16_t)std::atoi(argv[++i]);
        else if (flag == "--stun" && i + 1 < argc)
            stunUrls = argv[++i];
        else if (flag == "--turn" && i + 1 < argc)
            turnUrls = argv[++i];
        else if (flag == "--turn-ttl" && i + 1 < argc)
            turnTtl = std::atoi(argv[++i]);
        // **Read from the environment rather than the command line**, because a command line is
        // visible to every process on the box and this is coturn's shared secret.
        else if (flag == "--turn-secret-env" && i + 1 < argc) {
            if (const char *value = std::getenv(argv[++i]))
                turnSecret = value;
        }
    }

    if (!turnUrls.empty() && turnSecret.empty())
        std::printf("warning: --turn given with no secret, so no relay will be advertised\n");

    rtc::WebSocketServerConfiguration config;
    config.port      = port;
    config.enableTls = false; // nginx holds the certificate

    rtc::WebSocketServer server(config);
    std::printf("signalling on 127.0.0.1:%u — put nginx in front of it for wss://\n", port);

    server.onClient([](std::shared_ptr<rtc::WebSocket> socket) {
        auto client    = std::make_shared<Client>();
        client->socket = socket;
        {
            std::lock_guard<std::mutex> lock(mutex);
            clients[socket.get()] = client;
        }

        socket->onMessage([client](rtc::message_variant message) {
            if (std::holds_alternative<std::string>(message))
                handle(client, std::get<std::string>(message));
        });

        auto forget = [client, raw = socket.get()]() {
            std::lock_guard<std::mutex> lock(mutex);
            leaveRoom(client);
            clients.erase(raw);
        };
        socket->onClosed(forget);
        socket->onError([forget](std::string) { forget(); });
    });

    // Nothing to do on the main thread; the server has its own.
    for (;;)
        std::this_thread::sleep_for(std::chrono::seconds(1));
}
