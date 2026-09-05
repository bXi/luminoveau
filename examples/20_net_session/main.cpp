// Example 20 — Net Session
// ---------------------------------------------------------------------------
// The manual test for the networking stack. It shows:
//   * hosting and joining, and printing why either one failed
//   * the opaque PlayerId the protocol addresses peers by
//   * asking the active brokerage what it can do instead of assuming
//   * typed messages going both ways, reliable and unreliable
//
// Run one copy with `20_net_session host`, another with `20_net_session join <address>`,
// or start it with no arguments and press H to host, B to look for sessions on the LAN and
// 1-9 to join one of them. Two machines beats two windows: a loopback join will pass while
// the real path is broken.

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <cmath>
#include <string>
#include <vector>

constexpr uint16_t PORT = 28777;

// Any value both sides agree on. A real game would hash whatever makes two builds
// incompatible — the protocol, the content, the version tag.
constexpr uint64_t BUILD_ID = 0x20250829;

// Client → server, unreliable: whatever the client is doing this frame.
struct Heartbeat {
    uint32_t frame;
    float    wobble;
};

// Server → everyone, reliable: how full the session is.
struct Roster {
    uint32_t peers;
};

FontAsset *font = nullptr;

int  windowWidth  = 800;
int  windowHeight = 600;
bool connected    = false;

std::string                   status = "H to host, B to browse the LAN, J to join 127.0.0.1";
std::vector<Net::Peer>        peers;
std::vector<Net::SessionInfo> sessions;
uint32_t             frameCount   = 0;
uint32_t             lastRoster   = 0;
float                heartbeatAcc = 0.0f;

const char *ErrorName(Net::NetError error) {
    switch (error) {
    case Net::NetError::None: return "None";
    case Net::NetError::NoTransport: return "NoTransport";
    case Net::NetError::BrokerUnavailable: return "BrokerUnavailable";
    case Net::NetError::HostUnreachable: return "HostUnreachable";
    case Net::NetError::NatBlockedNoRelay: return "NatBlockedNoRelay";
    case Net::NetError::VersionMismatch: return "VersionMismatch";
    case Net::NetError::LobbyFull: return "LobbyFull";
    case Net::NetError::Refused: return "Refused";
    case Net::NetError::Timeout: return "Timeout";
    }
    return "?";
}

void StartHosting() {
    Net::HostConfig cfg;
    cfg.name  = "Luminoveau example session";
    cfg.port  = PORT;
    cfg.slots = 8;
    if (!Net::Host(cfg)) {
        status = std::string("host failed: ") + ErrorName(Net::LastError());
        return;
    }
    connected = true;
    status    = "hosting on port " + std::to_string(PORT);
}

void StartJoining(const std::string &address) {
    Net::Endpoint ep;
    ep.address = address;
    ep.port    = PORT;
    if (!Net::Join(ep)) {
        status = std::string("join failed: ") + ErrorName(Net::LastError());
        return;
    }
    connected = true;
    status    = "joined " + address + ", server is " + Net::IdOf(Net::SERVER_PEER).toString();
}

Lumi::Result AppInit(void **appstate, int argc, char *argv[]) {
    Window::InitWindow("Luminoveau Example — Net Session",
        windowWidth, windowHeight, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({ 18, 18, 24, 255 });
    font = &AssetHandler::GetDefaultFont();

    if (!Net::Init()) {
        status = std::string("net init failed: ") + ErrorName(Net::LastError());
        return Lumi::Result::Continue;
    }
    Net::SetBuildId(BUILD_ID);

    Net::OnPeerJoined([](Net::Peer peer) {
        peers.push_back(peer);
        Net::BroadcastReliable(Roster{ (uint32_t)peers.size() });
    });
    Net::OnPeerLeft([](Net::Peer peer) {
        std::erase(peers, peer);
        Net::BroadcastReliable(Roster{ (uint32_t)peers.size() });
    });

    Net::RegisterMessage<Heartbeat>([](Net::Peer peer, const Heartbeat &beat) {
        LUMI_UNUSED(peer, beat);
    });
    Net::RegisterMessage<Roster>([](Net::Peer, const Roster &roster) {
        lastRoster = roster.peers;
    });

    Net::OnSessions([](const std::vector<Net::SessionInfo> &found) {
        sessions = found;
        status   = std::to_string(sessions.size()) + " session(s) found — press 1-9 to join";
    });

    // Both roles can be picked from the command line so two terminals need no keystrokes.
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "host")
            StartHosting();
        else if (arg == "join")
            StartJoining(i + 1 < argc ? argv[i + 1] : "127.0.0.1");
    }
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void *appstate) {
    Net::Update();
    ++frameCount;

    if (!connected) {
        if (Input::KeyPressed(SDLK_H))
            StartHosting();
        if (Input::KeyPressed(SDLK_J))
            StartJoining("127.0.0.1");
        if (Input::KeyPressed(SDLK_B) && Net::Can(Net::Feature::Browse)) {
            sessions.clear();
            Net::QuerySessions();
            status = "looking for sessions on the LAN...";
        }
        for (int i = 0; i < 9 && i < (int)sessions.size(); ++i) {
            if (Input::KeyPressed(SDLK_1 + i)) {
                if (!Net::Join(sessions[i].endpoint))
                    status = std::string("join failed: ") + ErrorName(Net::LastError());
                else {
                    connected = true;
                    status    = "joined " + sessions[i].name;
                }
            }
        }
    } else if (Net::IsClient()) {
        // Unreliable on purpose: a dropped heartbeat is replaced by the next one.
        heartbeatAcc += Window::GetFrameTime();
        if (heartbeatAcc > 0.1f) {
            heartbeatAcc = 0.0f;
            Net::Send(Net::SERVER_PEER, Heartbeat{ frameCount, (float)std::sin(frameCount * 0.05f) });
        }
    }

    Window::StartFrame();

    float y = 16.0f;
    auto  line = [&](const std::string &text, Color color = WHITE) {
        Text::DrawText(*font, { 16.0f, y }, text, color, 20.0f);
        y += 24.0f;
    };

    line(status, connected ? Color{ 140, 220, 150, 255 } : Color{ 230, 200, 120, 255 });
    line("you: " + Net::LocalPlayer().toString());
    line(std::string("role: ") + (Net::IsServer() ? "server" : Net::IsClient() ? "client" : "idle") +
         "   peers: " + std::to_string(Net::GetPeerCount()) +
         "   max message: " + std::to_string(Net::MaxMessageSize()));

    // What the active brokerage can do decides what a session UI is allowed to offer.
    std::string caps;
    const std::pair<Net::Feature, const char *> features[] = {
        { Net::Feature::Browse, "browse" },
        { Net::Feature::Invite, "invite" },
        { Net::Feature::Friends, "friends" },
        { Net::Feature::Relay, "relay" },
        { Net::Feature::Crossplay, "crossplay" },
        { Net::Feature::Lobby, "lobby" },
    };
    for (const auto &[feature, name] : features)
        caps += std::string(name) + (Net::Can(feature) ? ": yes  " : ": no  ");
    line(caps, { 160, 170, 190, 255 });

    y += 12.0f;
    if (!connected && !sessions.empty()) {
        line("sessions on this network", { 160, 170, 190, 255 });
        for (size_t i = 0; i < sessions.size() && i < 9; ++i) {
            const Net::SessionInfo &found = sessions[i];
            line("  " + std::to_string(i + 1) + ")  " + found.name + "  " +
                 found.endpoint.address + ":" + std::to_string(found.endpoint.port) + "  " +
                 std::to_string(found.players) + "/" + std::to_string(found.slots));
        }
    } else if (Net::IsServer()) {
        line("connected players", { 160, 170, 190, 255 });
        for (Net::Peer peer : peers)
            line("  " + std::to_string(peer) + "  " + Net::IdOf(peer).toString() +
                 "  " + std::to_string(Net::GetPing(peer)) + " ms");
    } else if (Net::IsClient()) {
        line("session holds " + std::to_string(lastRoster) + " player(s)");
    }

    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void *appstate, SDL_Event *event) {
    return Lumi::Result::Continue;
}

void AppQuit(void *appstate, Lumi::Result result) {
    Net::Shutdown();
    Window::Close();
}
