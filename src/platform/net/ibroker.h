#pragma once

// Internal brokerage seam: who introduces peers to each other. A broker answers "who is
// out there and how do I reach them" and never carries game data. One is active at a
// time, chosen at Net::Init. User code never sees this.

#include <cstdint>
#include <string>
#include <vector>

#include "platform/net/net.h"
#include "platform/net/playerid.h"

/// @cond INTERNAL

// Narrows a session query. An empty filter asks for everything the broker will list.
struct LobbyFilter {
    std::vector<std::pair<std::string, std::string>> match;
    int                                             maxResults = 50;
};

struct BrokerEvent {
    enum Type { LobbyCreated,
        LobbyJoined,
        LobbyList,
        LobbyMemberChanged,
        SignalReceived,
        InviteAccepted,
        Error } type;

    PlayerId                      from;
    Net::Endpoint                 endpoint; // InviteAccepted
    std::vector<Net::SessionInfo> sessions; // LobbyList
    Net::NetError                 error = Net::NetError::None;
    std::vector<uint8_t>          data;
};

class IBroker {
public:
    virtual ~IBroker() = default;

    virtual const char *Name() const = 0;

    virtual bool     Init()                          = 0;
    virtual void     Shutdown()                      = 0;
    virtual void     Tick()                          = 0; // SteamAPI_RunCallbacks / EOS_Platform_Tick / no-op.
    virtual bool     Has(Net::Feature feature) const = 0;
    virtual PlayerId LocalPlayer() const             = 0;

    // Which build this is, so a broker can keep incompatible sessions out of its listings.
    virtual void SetBuildId(uint64_t id) = 0;

    // Advertising and finding sessions. CreateLobby runs for any broker whenever a
    // publicly-listed session starts, because that is also how a broker with no lobbies at
    // all knows to start answering for one. The key/value half needs Has(Feature::Lobby).
    virtual bool CreateLobby(const Net::HostConfig &cfg)          = 0;
    virtual bool JoinLobby(const Net::Endpoint &ep)               = 0;
    virtual void LeaveLobby()                                     = 0;
    virtual void SetPlayerCount(int players)                      = 0;
    virtual void SetLobbyData(const char *key, const char *value) = 0;
    virtual bool QueryLobbies(const LobbyFilter &filter)          = 0;

    // Signalling. Only called when the transport asks for custom signalling; CarriesSignals
    // is what decides whether such a transport can be used at all.
    virtual bool CarriesSignals() const { return false; }
    virtual bool SendSignal(const PlayerId &to, const void *data, uint32_t size) = 0;

    virtual void Poll(std::vector<BrokerEvent> &out) = 0;
};

// Implemented in brokers/. Falls back to the local broker when the preference is unavailable.
IBroker *createBroker(Net::Broker preference, const std::string &signalingUrl);

// Hands control back to the event loop while Net waits on something. A plain sleep would
// deadlock the browser, where nothing else runs until the frame yields.
void brokerYield();
/// @endcond
