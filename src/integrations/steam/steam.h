#pragma once

#include <string>
#include <stdexcept>

#ifdef LUMINOVEAU_WITH_STEAM
#include "steam_api.h"
#endif

#include "util/helpers.h"
#include "core/log/log.h"
/// @brief Steamworks integration: init, stats and achievements. No-op unless built with Steam support.
class Steam {
public:
    /// @brief Initializes the Steam API for the given app.
    /// @param appId Your Steam application ID.
    static void Init(int appId) { get()._init(appId); }
    /// @brief Shuts down the Steam API.
    static void Close() { get()._close(); }

    /// @brief Returns true if the Steam API is initialized and ready.
    static bool IsReady() { return get()._isReady(); }

    /// @brief Reads a named float stat from Steam.
    /// @param pchName The stat's API name.
    static float GetStat(const std::string& pchName) { return get()._getStat(pchName); }
    /// @brief Writes a named float stat to Steam.
    /// @param pchName The stat's API name.
    /// @param fData The value to store.
    static void SetStat(const std::string& pchName, float fData) { get()._setStat(pchName, fData); }

    /// @brief Returns true if the named achievement is unlocked.
    /// @param pchName The achievement's API name.
    static bool HasAchievement(const std::string& pchName) { return get()._hasAchievement(pchName); }
    /// @brief Unlocks the named achievement.
    /// @param pchName The achievement's API name.
    static void SetAchievement(const std::string& pchName) { get()._setAchievement(pchName); }

    /// @brief Clears (re-locks) the named achievement.
    /// @param pchName The achievement's API name.
    static void ClearAchievement(const std::string& pchName) { get()._clearAchievement(pchName); }

    /// @brief Returns the current user's Steam ID.
    static int GetUserSteamId() { return get()._getUserSteamId(); }



private:
    bool isInit = false;
    int appId = 0;


    void _init(int appId);
    void _close();

    [[nodiscard]] bool _isReady() const;

    float _getStat(const std::string& pchName);
    void _setStat(const std::string& pchName, float fData);

    bool _hasAchievement(const std::string& pchName);
    void _setAchievement(const std::string& pchName);

    void _clearAchievement(const std::string& pchName);

    int _getUserSteamId();


    //TODO: figure out nice way to implement this. curse Steam for making it possible to make pUnlockTime 0
    //bool GetAchievementAndUnlockTime(std::string pchName, out bool pbAchieved, out uint punUnlockTime)

public:
    /// @cond INTERNAL
    Steam(const Steam &) = delete;

    static Steam &get() {
        static Steam instance;
        return instance;
    }
    /// @endcond

private:
    Steam() = default;
};
