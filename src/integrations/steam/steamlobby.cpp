#include "steamlobby.h"

#include "steam.h"
#include "steamabi.h"

#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>

namespace {

/// The shape of Valve's own convention, so the connect string a friend's client hands back is one
/// Steam's UI understands. The Steam broker uses the same prefix.
constexpr const char *kConnectPrefix = "+connect_lobby";

uint64_t parseLobby(const char *text) {
    if (text == nullptr) return 0;
    return std::strtoull(text, nullptr, 10);
}

} // namespace

uint64_t SteamLobby::LobbyFromCommandLine(int argc, char *argv[]) {
    // Steam passes it as two arguments, `+connect_lobby` then the id; the joined form is accepted
    // too, since a rich presence connect string pasted by hand arrives that way.
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) continue;
        const size_t prefix = std::strlen(kConnectPrefix);
        if (std::strncmp(argv[i], kConnectPrefix, prefix) != 0) continue;

        if (argv[i][prefix] == ' ') return parseLobby(argv[i] + prefix + 1);
        if (argv[i][prefix] == '\0' && i + 1 < argc) return parseLobby(argv[i + 1]);
    }
    return 0;
}

#ifdef LUMINOVEAU_WITH_STEAM

namespace {

/// The callbacks need an object, and `STEAM_CALLBACK` registers in its constructor — so it is made
/// on first use once Steam is up, rather than as a static that would register before `SteamAPI_Init`.
class LobbyState {
public:
    LobbyState() = default;

    void create(SteamLobby::Visibility visibility, int maxMembers) {
        leave();
        const ELobbyType type =
            visibility == SteamLobby::Visibility::Public ? k_ELobbyTypePublic : k_ELobbyTypeFriendsOnly;
        _createResult.Set(SteamMatchmaking()->CreateLobby(type, maxMembers), this, &LobbyState::onCreated);
    }

    void join(uint64_t lobby) {
        leave();
        _enterResult.Set(SteamMatchmaking()->JoinLobby(CSteamID((uint64)lobby)), this, &LobbyState::onEntered);
    }

    void leave() {
        if (_lobby.IsValid()) SteamMatchmaking()->LeaveLobby(_lobby);
        _lobby = CSteamID();
    }

    CSteamID lobby() const { return _lobby; }

    bool poll(SteamLobby::Event &out) {
        if (_events.empty()) return false;
        out = _events.front();
        _events.pop_front();
        return true;
    }

private:
    void push(SteamLobby::Event::Type type, uint64_t lobby) {
        SteamLobby::Event ev;
        ev.type  = type;
        ev.lobby = lobby;
        _events.push_back(ev);
    }

    void onCreated(LobbyCreated_t *result, bool ioFailure) {
        if (ioFailure || result == nullptr || result->m_eResult != k_EResultOK) {
            LOG_WARNING("SteamLobby: could not create a lobby");
            push(SteamLobby::Event::Type::CreateFailed, 0);
            return;
        }
        _lobby = CSteamID(result->m_ulSteamIDLobby);
        push(SteamLobby::Event::Type::Created, _lobby.ConvertToUint64());
    }

    void onEntered(LobbyEnter_t *result, bool ioFailure) {
        if (ioFailure || result == nullptr ||
            result->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
            LOG_WARNING("SteamLobby: could not enter the lobby");
            push(SteamLobby::Event::Type::EnterFailed, result != nullptr ? result->m_ulSteamIDLobby : 0);
            return;
        }
        _lobby = CSteamID(result->m_ulSteamIDLobby);
        push(SteamLobby::Event::Type::Entered, _lobby.ConvertToUint64());
    }

    // An invite accepted, or Join Game on a friend whose presence carries a lobby.
    STEAM_CALLBACK(LobbyState, onJoinRequested, GameLobbyJoinRequested_t);
    STEAM_CALLBACK(LobbyState, onRichPresenceJoin, GameRichPresenceJoinRequested_t);

    CCallResult<LobbyState, LobbyCreated_t> _createResult;
    CCallResult<LobbyState, LobbyEnter_t>   _enterResult;

    CSteamID                      _lobby;
    std::deque<SteamLobby::Event> _events;
};

void LobbyState::onJoinRequested(GameLobbyJoinRequested_t *info) {
    push(SteamLobby::Event::Type::JoinRequested, info->m_steamIDLobby.ConvertToUint64());
}

void LobbyState::onRichPresenceJoin(GameRichPresenceJoinRequested_t *info) {
    const char  *connect = info->m_rgchConnect;
    const size_t prefix  = std::strlen(kConnectPrefix);
    if (std::strncmp(connect, kConnectPrefix, prefix) != 0) return;

    const uint64_t lobby = parseLobby(connect + prefix);
    if (lobby != 0) push(SteamLobby::Event::Type::JoinRequested, lobby);
}

std::unique_ptr<LobbyState> g_state;

/// Null until Steam is up, and made the first time anything asks after that.
LobbyState *state() {
    if (!Steam::IsReady() || SteamMatchmaking() == nullptr) return nullptr;
    if (!g_state) g_state = std::make_unique<LobbyState>();
    return g_state.get();
}

} // namespace

void SteamLobby::Create(Visibility visibility, int maxMembers) {
    if (LobbyState *s = state()) s->create(visibility, maxMembers);
}

void SteamLobby::Join(uint64_t lobby) {
    if (lobby == 0) return;
    if (LobbyState *s = state()) s->join(lobby);
}

void SteamLobby::Leave() {
    if (LobbyState *s = state()) s->leave();
}

bool SteamLobby::InLobby() {
    LobbyState *s = state();
    return s != nullptr && s->lobby().IsValid();
}

uint64_t SteamLobby::Id() {
    LobbyState *s = state();
    return s != nullptr && s->lobby().IsValid() ? s->lobby().ConvertToUint64() : 0;
}

bool SteamLobby::IsOwner() {
    LobbyState *s = state();
    if (s == nullptr || !s->lobby().IsValid()) return false;
    // Through the flat API — see steamabi.h for the MinGW crash the C++ calls cause.
    return lumi_steam::lobbyOwner(s->lobby()) == lumi_steam::localUser();
}

int SteamLobby::MemberCount() {
    LobbyState *s = state();
    if (s == nullptr || !s->lobby().IsValid()) return 0;
    return SteamMatchmaking()->GetNumLobbyMembers(s->lobby());
}

void SteamLobby::SetData(const std::string &key, const std::string &value) {
    if (!IsOwner()) return;
    SteamMatchmaking()->SetLobbyData(state()->lobby(), key.c_str(), value.c_str());
}

std::string SteamLobby::GetData(const std::string &key) {
    LobbyState *s = state();
    if (s == nullptr || !s->lobby().IsValid()) return {};
    const char *value = SteamMatchmaking()->GetLobbyData(s->lobby(), key.c_str());
    return value != nullptr ? std::string(value) : std::string();
}

void SteamLobby::SetRichPresence(const std::string &key, const std::string &value) {
    if (!Steam::IsReady() || SteamFriends() == nullptr) return;
    // Steam removes a key given an empty value, which is the documented way to clear one.
    SteamFriends()->SetRichPresence(key.c_str(), value.c_str());
}

void SteamLobby::ClearRichPresence() {
    if (!Steam::IsReady() || SteamFriends() == nullptr) return;
    SteamFriends()->ClearRichPresence();
}

bool SteamLobby::Poll(Event &out) {
    LobbyState *s = state();
    return s != nullptr && s->poll(out);
}

void SteamLobby::Shutdown() {
    if (!g_state) return;
    g_state->leave();
    g_state.reset();
}

#else

void SteamLobby::Create(Visibility, int) {}
void SteamLobby::Join(uint64_t) {}
void SteamLobby::Leave() {}
bool SteamLobby::InLobby() { return false; }
uint64_t SteamLobby::Id() { return 0; }
bool SteamLobby::IsOwner() { return false; }
int SteamLobby::MemberCount() { return 0; }
void SteamLobby::SetData(const std::string &, const std::string &) {}
std::string SteamLobby::GetData(const std::string &) { return {}; }
void SteamLobby::SetRichPresence(const std::string &, const std::string &) {}
void SteamLobby::ClearRichPresence() {}
bool SteamLobby::Poll(Event &) { return false; }
void SteamLobby::Shutdown() {}

#endif
