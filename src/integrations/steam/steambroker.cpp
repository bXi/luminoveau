// Broker 4: Steam. The first brokerage worth shipping to players — it browses, it invites,
// it knows who your friends are, and connections that fail to punch fall back to SDR relay
// for free. In exchange it is Steam-only, so Feature::Crossplay stays false.
//
// Sessions are Steam lobbies. The lobby carries the engine's session metadata as lobby data
// and the host's SteamID is what the transport actually connects to; a lobby endpoint is
// resolved to that host before anything is dialled.
//
// Compiled unconditionally. Without the Steamworks SDK this is a factory that returns
// nothing, so a build with no SDK still links and simply never selects this broker.

#include "platform/net/brokers/brokers.h"

#ifdef LUMINOVEAU_WITH_STEAM

#include "core/log/log.h"
#include "integrations/steam/steam.h"

#include <string>

namespace {

// Keys the engine owns inside a lobby. Games get the blob and can add their own.
constexpr const char *KEY_NAME  = "lumi_name";
constexpr const char *KEY_BUILD = "lumi_build";
constexpr const char *KEY_BLOB  = "lumi_blob";

// Lobby data is text, and the blob is bytes, so it travels as hex. Values are capped at
// k_cubChatMetadataMax, which two hex digits per byte halves.
constexpr size_t MAX_BLOB = 1024;

std::string toHex(const std::vector<uint8_t> &bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string           out;
    out.reserve(bytes.size() * 2);
    for (uint8_t byte : bytes) {
        out += digits[byte >> 4];
        out += digits[byte & 0x0f];
    }
    return out;
}

std::vector<uint8_t> fromHex(const char *text) {
    auto value = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    };
    std::vector<uint8_t> out;
    for (const char *p = text; p[0] && p[1]; p += 2) {
        const int hi = value(p[0]), lo = value(p[1]);
        if (hi < 0 || lo < 0)
            break;
        out.push_back((uint8_t)((hi << 4) | lo));
    }
    return out;
}

PlayerId idOf(CSteamID steamId) {
    return PlayerId::From(PlayerId::Provider::Steam, steamId.ConvertToUint64());
}

CSteamID steamIdOf(const PlayerId &id) {
    if (id.provider != PlayerId::Provider::Steam || id.length != sizeof(uint64))
        return CSteamID();
    uint64 raw = 0;
    for (int i = 0; i < 8; ++i)
        raw |= (uint64)id.bytes[i] << (i * 8);
    return CSteamID(raw);
}

class SteamBroker : public IBroker {
public:
    const char *Name() const override { return "steam"; }

    bool Init() override {
        // Steam::Init is the game's call, not the engine's, so this only checks the result.
        return Steam::IsReady() && SteamMatchmaking() && SteamFriends();
    }

    void Shutdown() override { LeaveLobby(); }

    void Tick() override { Steam::Tick(); }

    void SetBuildId(uint64_t id) override { _buildId = id; }

    bool Has(Net::Feature feature) const override {
        switch (feature) {
        case Net::Feature::Browse:
        case Net::Feature::Invite:
        case Net::Feature::Friends:
        case Net::Feature::Relay:
        case Net::Feature::Lobby:
            return true;
        case Net::Feature::Crossplay:
            return false; // everyone in the lobby is on Steam, by construction
        }
        return false;
    }

    PlayerId LocalPlayer() const override {
        return PlayerId::From(PlayerId::Provider::Steam, Steam::GetUserSteamId64());
    }

    bool CreateLobby(const Net::HostConfig &cfg) override {
        LeaveLobby();
        _pendingConfig = cfg;
        if (_pendingConfig.blob.size() > MAX_BLOB)
            _pendingConfig.blob.resize(MAX_BLOB);

        const ELobbyType type = cfg.publicListing ? k_ELobbyTypePublic : k_ELobbyTypeFriendsOnly;
        SteamAPICall_t   call = SteamMatchmaking()->CreateLobby(type, cfg.slots);
        _createResult.Set(call, this, &SteamBroker::_onLobbyCreated);
        return true;
    }

    bool JoinLobby(const Net::Endpoint &ep) override {
        if (ep.kind != Net::Endpoint::Kind::Lobby || ep.lobby == 0)
            return false;
        LeaveLobby();
        SteamAPICall_t call = SteamMatchmaking()->JoinLobby(CSteamID((uint64)ep.lobby));
        _enterResult.Set(call, this, &SteamBroker::_onLobbyEntered);
        return true;
    }

    void LeaveLobby() override {
        if (_lobby.IsValid()) {
            SteamMatchmaking()->LeaveLobby(_lobby);
            _lobby = CSteamID();
        }
        SteamFriends()->SetRichPresence("connect", "");
    }

    void SetPlayerCount(int players) override {
        // Steam counts lobby members itself; this only keeps a browsing client's view honest
        // for games that report a count differing from the member list.
        if (_lobby.IsValid() && players != _players) {
            _players = players;
            SteamMatchmaking()->SetLobbyData(_lobby, "lumi_players", std::to_string(players).c_str());
        }
    }

    void SetLobbyData(const char *key, const char *value) override {
        if (_lobby.IsValid())
            SteamMatchmaking()->SetLobbyData(_lobby, key, value);
    }

    bool QueryLobbies(const LobbyFilter &filter) override {
        SteamMatchmaking()->AddRequestLobbyListStringFilter(
            KEY_BUILD, std::to_string(_buildId).c_str(), k_ELobbyComparisonEqual);
        for (const auto &[key, value] : filter.match)
            SteamMatchmaking()->AddRequestLobbyListStringFilter(key.c_str(), value.c_str(), k_ELobbyComparisonEqual);
        SteamMatchmaking()->AddRequestLobbyListResultCountFilter(filter.maxResults);

        SteamAPICall_t call = SteamMatchmaking()->RequestLobbyList();
        _listResult.Set(call, this, &SteamBroker::_onLobbyList);
        return true;
    }

    // Signalling is Phase 5 territory; Steam's own P2P needs none of it.
    bool SendSignal(const PlayerId &, const void *, uint32_t) override { return false; }

    void Poll(std::vector<BrokerEvent> &out) override {
        for (BrokerEvent &ev : _events)
            out.push_back(std::move(ev));
        _events.clear();
    }

private:
    void _onLobbyCreated(LobbyCreated_t *result, bool ioFailure) {
        if (ioFailure || result->m_eResult != k_EResultOK) {
            _queueError(Net::NetError::BrokerUnavailable);
            return;
        }
        _lobby = CSteamID(result->m_ulSteamIDLobby);
        SteamMatchmaking()->SetLobbyData(_lobby, KEY_NAME, _pendingConfig.name.c_str());
        SteamMatchmaking()->SetLobbyData(_lobby, KEY_BUILD, std::to_string(_buildId).c_str());
        SteamMatchmaking()->SetLobbyData(_lobby, KEY_BLOB, toHex(_pendingConfig.blob).c_str());

        // What a friend's client hands back when they pick "Join Game".
        SteamFriends()->SetRichPresence("connect",
            ("+connect_lobby " + std::to_string(_lobby.ConvertToUint64())).c_str());

        BrokerEvent ev;
        ev.type          = BrokerEvent::LobbyCreated;
        ev.from          = LocalPlayer();
        ev.endpoint.kind = Net::Endpoint::Kind::Lobby;
        ev.endpoint.lobby = _lobby.ConvertToUint64();
        _events.push_back(std::move(ev));
    }

    void _onLobbyEntered(LobbyEnter_t *result, bool ioFailure) {
        if (ioFailure || result->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
            _queueError(result && result->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseFull
                            ? Net::NetError::LobbyFull
                            : Net::NetError::Refused);
            return;
        }
        _lobby = CSteamID(result->m_ulSteamIDLobby);

        // The host is who the transport actually dials; the lobby was only the introduction.
        BrokerEvent ev;
        ev.type           = BrokerEvent::LobbyJoined;
        ev.from           = idOf(SteamMatchmaking()->GetLobbyOwner(_lobby));
        ev.endpoint.kind  = Net::Endpoint::Kind::Player;
        ev.endpoint.player = ev.from;
        _events.push_back(std::move(ev));
    }

    void _onLobbyList(LobbyMatchList_t *result, bool ioFailure) {
        BrokerEvent ev;
        ev.type = BrokerEvent::LobbyList;
        if (!ioFailure) {
            for (uint32 i = 0; i < result->m_nLobbiesMatching; ++i) {
                const CSteamID lobby = SteamMatchmaking()->GetLobbyByIndex((int)i);
                if (!lobby.IsValid())
                    continue;

                Net::SessionInfo info;
                info.endpoint.kind  = Net::Endpoint::Kind::Lobby;
                info.endpoint.lobby = lobby.ConvertToUint64();
                info.host           = idOf(SteamMatchmaking()->GetLobbyOwner(lobby));
                info.name           = SteamMatchmaking()->GetLobbyData(lobby, KEY_NAME);
                info.players        = SteamMatchmaking()->GetNumLobbyMembers(lobby);
                info.slots          = SteamMatchmaking()->GetLobbyMemberLimit(lobby);
                info.blob           = fromHex(SteamMatchmaking()->GetLobbyData(lobby, KEY_BLOB));
                ev.sessions.push_back(std::move(info));
            }
        }
        _events.push_back(std::move(ev));
    }

    void _queueError(Net::NetError error) {
        BrokerEvent ev;
        ev.type  = BrokerEvent::Error;
        ev.error = error;
        _events.push_back(std::move(ev));
    }

    // Accepting an invite, or picking Join Game in the friends list.
    STEAM_CALLBACK(SteamBroker, _onJoinRequested, GameLobbyJoinRequested_t);
    STEAM_CALLBACK(SteamBroker, _onRichPresenceJoin, GameRichPresenceJoinRequested_t);
    STEAM_CALLBACK(SteamBroker, _onLobbyChatUpdate, LobbyChatUpdate_t);

    CCallResult<SteamBroker, LobbyCreated_t>   _createResult;
    CCallResult<SteamBroker, LobbyEnter_t>     _enterResult;
    CCallResult<SteamBroker, LobbyMatchList_t> _listResult;

    CSteamID                 _lobby;
    Net::HostConfig          _pendingConfig;
    uint64_t                 _buildId = 0;
    int                      _players = 0;
    std::vector<BrokerEvent> _events;
};

void SteamBroker::_onJoinRequested(GameLobbyJoinRequested_t *info) {
    BrokerEvent ev;
    ev.type           = BrokerEvent::InviteAccepted;
    ev.from           = idOf(info->m_steamIDFriend);
    ev.endpoint.kind  = Net::Endpoint::Kind::Lobby;
    ev.endpoint.lobby = info->m_steamIDLobby.ConvertToUint64();
    _events.push_back(std::move(ev));
}

void SteamBroker::_onRichPresenceJoin(GameRichPresenceJoinRequested_t *info) {
    // The connect string is ours; the shape is Valve's convention so their UI understands it.
    const std::string connect = info->m_rgchConnect;
    const std::string prefix  = "+connect_lobby ";
    if (connect.rfind(prefix, 0) != 0)
        return;

    BrokerEvent ev;
    ev.type           = BrokerEvent::InviteAccepted;
    ev.from           = idOf(info->m_steamIDFriend);
    ev.endpoint.kind  = Net::Endpoint::Kind::Lobby;
    ev.endpoint.lobby = strtoull(connect.substr(prefix.size()).c_str(), nullptr, 10);
    _events.push_back(std::move(ev));
}

void SteamBroker::_onLobbyChatUpdate(LobbyChatUpdate_t *info) {
    BrokerEvent ev;
    ev.type = BrokerEvent::LobbyMemberChanged;
    ev.from = idOf(CSteamID(info->m_ulSteamIDUserChanged));
    _events.push_back(std::move(ev));
}

} // namespace

/// @cond INTERNAL
IBroker *createSteamBroker() {
    return new SteamBroker();
}
/// @endcond

#else

/// @cond INTERNAL
IBroker *createSteamBroker() {
    return nullptr;
}
/// @endcond

#endif
