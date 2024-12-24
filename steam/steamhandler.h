#pragma once

#include <string>
#include <stdexcept>

#if __has_include("steam_api.h")
#include "steam_api.h"
#endif

#include "utils/helpers.h"
class Steam {
public:
    static void Init(int appId) { get()._init(appId); }
    static void Close() { get()._close(); }

    static bool IsReady() { return get()._isReady(); }

    static float GetStat(const std::string& pchName) { return get()._getStat(pchName); }
    static void SetStat(const std::string& pchName, float fData) { get()._setStat(pchName, fData); }

    static bool HasAchievement(const std::string& pchName) { return get()._hasAchievement(pchName); }
    static void SetAchievement(const std::string& pchName) { get()._setAchievement(pchName); }

    static void ClearAchievement(const std::string& pchName) { get()._clearAchievement(pchName); }

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
    Steam(const Steam &) = delete;

    static Steam &get() {
        static Steam instance;
        return instance;
    }

private:
    Steam() = default;
};