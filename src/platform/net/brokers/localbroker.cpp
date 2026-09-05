// Broker 1: direct addresses. Nothing introduces anybody — the address came from outside
// the game, so there is no service, no accounts and no capabilities to offer. It exists so
// the rest of the engine can always talk to a broker instead of null-checking one.

#include "platform/net/brokers/brokers.h"

namespace {

class LocalBroker : public IBroker {
public:
    const char *Name() const override { return "local"; }

    bool Init() override {
        _self = mintProcessIdentity();
        return true;
    }

    void Shutdown() override { _self = {}; }
    void Tick() override { }

    void     SetBuildId(uint64_t) override { }
    bool     Has(Net::Feature) const override { return false; }
    PlayerId LocalPlayer() const override { return _self; }

    bool CreateLobby(const Net::HostConfig &) override { return false; }
    bool JoinLobby(const Net::Endpoint &) override { return false; }
    void LeaveLobby() override { }
    void SetPlayerCount(int) override { }
    void SetLobbyData(const char *, const char *) override { }
    bool QueryLobbies(const LobbyFilter &) override { return false; }

    bool SendSignal(const PlayerId &, const void *, uint32_t) override { return false; }

    void Poll(std::vector<BrokerEvent> &) override { }

private:
    PlayerId _self;
};

} // namespace

/// @cond INTERNAL
IBroker *createLocalBroker() {
    return new LocalBroker();
}
/// @endcond
