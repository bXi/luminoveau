#include "steam.h"

//*/
void Steam::_init(int newAppId) {

#ifdef LUMINOVEAU_WITH_STEAM
#ifdef NDEBUG
    if ( SteamAPI_RestartAppIfNecessary( newAppId ) )
    {

         LOG_CRITICAL("SteamAPI_RestartAppIfNecessary failed.");
    }
#endif

    SteamErrMsg errMsg;

    if (SteamAPI_InitEx(&errMsg) != k_ESteamAPIInitResult_OK)
        LOG_CRITICAL("failed to init Steam: {}", errMsg);

    isInit = true;
#else
    LUMI_UNUSED(newAppId);
#endif
}

void Steam::_close() {
#ifdef LUMINOVEAU_WITH_STEAM
    if (isInit) SteamAPI_Shutdown();
#endif
    isInit = false;
}

bool Steam::_isReady() const {
    return isInit;
}

float Steam::_getStat(const std::string &pchName) {
    LUMI_UNUSED(pchName);

    return 0;
}

void Steam::_setStat(const std::string &pchName, float fData) {
    LUMI_UNUSED(pchName, fData);
}

bool Steam::_hasAchievement(const std::string &pchName) {
    if (!_isReady()) return false;

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
    if (!_isReady()) return;

#ifdef LUMINOVEAU_WITH_STEAM
    SteamUserStats()->SetAchievement(pchName.c_str());
    SteamUserStats()->StoreStats();
#else
    LUMI_UNUSED(pchName);
#endif
}

void Steam::_clearAchievement(const std::string &pchName) {
    if (!_isReady()) return;

#ifdef LUMINOVEAU_WITH_STEAM
    SteamUserStats()->ClearAchievement(pchName.c_str());
    SteamUserStats()->StoreStats();
#else
    LUMI_UNUSED(pchName);
#endif
}

int Steam::_getUserSteamId() {
    if (!_isReady()) return -1;

#ifdef LUMINOVEAU_WITH_STEAM
    auto userId = SteamUser()->GetSteamID();
    return userId.GetAccountID();
#else
    return -1;
#endif
}
