#include "steamhandler.h"

//*/
void Steam::_init(int appId) {

    if ( SteamAPI_RestartAppIfNecessary( appId ) )
	{
		// if Steam is not running or the game wasn't started through Steam, SteamAPI_RestartAppIfNecessary starts the
		// local Steam client and also launches this game again.

		// Once you get a public Steam AppID assigned for this game, you need to replace k_uAppIdInvalid with it and
		// removed steam_appid.txt from the game depot.

//        throw std::runtime_error("Can't initialize steam.");
	}
}

void Steam::_close() {
}

float Steam::_getStat(std::string pchName) {
    return 0;
}

void Steam::_setStat(std::string pchName, float fData) {
}

bool Steam::_hasAchievement(std::string pchName) {


    bool hasAchievement = false;
    SteamUserStats()->GetAchievement(pchName.c_str(), &hasAchievement);

    return hasAchievement;
}

void Steam::_setAchievement(std::string pchName) {
    SteamUserStats()->SetAchievement(pchName.c_str());


}

void Steam::_clearAchievement(std::string pchName) {
    SteamUserStats()->ClearAchievement(pchName.c_str());
}
