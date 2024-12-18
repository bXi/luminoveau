#include "steamhandler.h"

//*/
void Steam::_init(int newAppId) {

    #ifdef NDEBUG
    if ( SteamAPI_RestartAppIfNecessary( newAppId ) )
    {
         throw std::runtime_error("SteamAPI_RestartAppIfNecessary failed.");
    }
    #endif

    SteamErrMsg errMsg;

    if (SteamAPI_InitEx(&errMsg) != k_ESteamAPIInitResult_OK)
        throw std::runtime_error(Helpers::TextFormat("%s failed to init Steam.  %s", CURRENT_METHOD(), errMsg));

    isInit = true;
}

void Steam::_close() {
    isInit = false;
}

bool Steam::_isReady() const {
    return isInit;
}

float Steam::_getStat(const std::string &pchName) {
    return 0;
}

void Steam::_setStat(const std::string &pchName, float fData) {
}

bool Steam::_hasAchievement(const std::string &pchName) {
    if (!_isReady()) return false;

    bool hasAchievement = false;
    SteamUserStats()->GetAchievement(pchName.c_str(), &hasAchievement);

    return hasAchievement;
}

void Steam::_setAchievement(const std::string &pchName) {
    if (!_isReady()) return;

    SteamUserStats()->SetAchievement(pchName.c_str());
    SteamUserStats()->StoreStats();
}

void Steam::_clearAchievement(const std::string &pchName) {
    if (!_isReady()) return;

    SteamUserStats()->ClearAchievement(pchName.c_str());
    SteamUserStats()->StoreStats();
}

int Steam::_getUserSteamId() {
    if (!_isReady()) return -1;

    auto userId = SteamUser()->GetSteamID();
    return userId.GetAccountID();
}
