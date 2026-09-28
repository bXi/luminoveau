#include "steam.h"
#include "steamabi.h"
#include "steamleaderboards.h"
#include "steamlobby.h"

//*/
void Steam::_init(int newAppId, bool relaunchThroughSteam) {

#ifdef LUMINOVEAU_WITH_STEAM
#ifdef NDEBUG
    // Returning true means Steam is relaunching us through the client; this process must go away.
    if (relaunchThroughSteam && SteamAPI_RestartAppIfNecessary(newAppId))
        LOG_CRITICAL("relaunching through Steam");
#else
    LUMI_UNUSED(relaunchThroughSteam);
#endif

    SteamErrMsg errMsg;

    if (SteamAPI_InitEx(&errMsg) != k_ESteamAPIInitResult_OK) {
        LOG_WARNING("failed to init Steam: {}", errMsg);
        return;
    }

    _appId  = newAppId;
    _isInit = true;

    // Acquired up front so the first P2P connect does not stall waiting for relay tickets.
    SteamNetworkingUtils()->InitRelayNetworkAccess();
#else
    LUMI_UNUSED(newAppId, relaunchThroughSteam);
#endif
}

void Steam::_close() {
#ifdef LUMINOVEAU_WITH_STEAM
    // Its callbacks unregister in their destructors, which must happen while the API still exists.
    SteamLobby::Shutdown();
    SteamLeaderboards::Shutdown();

    if (_isInit)
        SteamAPI_Shutdown();
#endif
    _isInit = false;
}

void Steam::_tick() {
#ifdef LUMINOVEAU_WITH_STEAM
    if (_isInit)
        SteamAPI_RunCallbacks();
#endif
}

bool Steam::_isReady() const {
    return _isInit;
}

float Steam::_getStat(const std::string &pchName) {
    LUMI_UNUSED(pchName);

    return 0;
}

void Steam::_setStat(const std::string &pchName, float fData) {
    LUMI_UNUSED(pchName, fData);
}

bool Steam::_hasAchievement(const std::string &pchName) {
    if (!_isReady())
        return false;

#ifdef LUMINOVEAU_WITH_STEAM
    bool hasAchievement = false;
    SteamUserStats()->GetAchievement(pchName.c_str(), &hasAchievement);

    return hasAchievement;
#else
    LUMI_UNUSED(pchName);
    return false;
#endif
}

void Steam::_setAchievement(const std::string &pchName) {
    if (!_isReady())
        return;

#ifdef LUMINOVEAU_WITH_STEAM
    SteamUserStats()->SetAchievement(pchName.c_str());
    SteamUserStats()->StoreStats();
#else
    LUMI_UNUSED(pchName);
#endif
}

void Steam::_clearAchievement(const std::string &pchName) {
    if (!_isReady())
        return;

#ifdef LUMINOVEAU_WITH_STEAM
    SteamUserStats()->ClearAchievement(pchName.c_str());
    SteamUserStats()->StoreStats();
#else
    LUMI_UNUSED(pchName);
#endif
}

int Steam::_getUserSteamId() {
    if (!_isReady())
        return -1;

#ifdef LUMINOVEAU_WITH_STEAM
    // Flat API: the C++ GetSteamID crashes MinGW builds — see steamabi.h.
    return lumi_steam::localUser().GetAccountID();
#else
    return -1;
#endif
}

uint64_t Steam::_getUserSteamId64() {
    if (!_isReady())
        return 0;

#ifdef LUMINOVEAU_WITH_STEAM
    return lumi_steam::localUser().ConvertToUint64();
#else
    return 0;
#endif
}

std::string Steam::_getPersonaName() {
    if (!_isReady())
        return {};

#ifdef LUMINOVEAU_WITH_STEAM
    const char *name = SteamFriends()->GetPersonaName();
    return name != nullptr ? std::string(name) : std::string();
#else
    return {};
#endif
}
