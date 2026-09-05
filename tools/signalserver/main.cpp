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
        send(client, { { "op", SignalProtocol::Op::Welcome } });

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
        auto target = clientById(message.value("to", ""));
        if (!target || target->room != client->room || client->room.empty())
            return;
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
        if (std::string(argv[i]) == "--port" && i + 1 < argc)
            port = (uint16_t)std::atoi(argv[++i]);
    }

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
