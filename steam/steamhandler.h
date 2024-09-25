#pragma once

#include <string>
#include <stdexcept>

#include "steam_api.h"

class Steam {
public:
    static void Init(int appId) { get()._init(appId); }
    static void Close() { get()._close(); }

    static float GetStat(std::string pchName) { return get()._getStat(pchName); }
    static void SetStat(std::string pchName, float fData) { get()._setStat(pchName, fData); }

    static bool HasAchievement(std::string pchName) { return get()._hasAchievement(pchName); }
    static void SetAchievement(std::string pchName) { get()._setAchievement(pchName); }

    static void ClearAchievement(std::string pchName) { get()._clearAchievement(pchName); }


private:
    bool isInit = false;
    int appId = 0;


    void _init(int appId);
    void _close();

    float _getStat(std::string pchName);
    void _setStat(std::string pchName, float fData);

    bool _hasAchievement(std::string pchName);
    void _setAchievement(std::string pchName);

    void _clearAchievement(std::string pchName);

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