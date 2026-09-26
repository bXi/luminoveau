#pragma once

// Steamworks calls that return a CSteamID, routed through the flat C API.
//
// **A CSteamID returned by value from an interface method crashes a MinGW build.** The interfaces
// are C++ vtables implemented inside steam_api64.dll, which is built with MSVC, and the two
// compilers disagree about how a member function returns a class that has constructors: MSVC
// passes a hidden pointer for the result, GCC expects it back in a register. So the DLL writes the
// id through a pointer the caller never passed — an access violation on the first call, with
// nothing in the log. `SteamUser()->GetSteamID()` and `SteamMatchmaking()->GetLobbyOwner()` are the
// usual first victims.
//
// The flat API is `extern "C"` and returns a plain uint64, which every compiler agrees on. Every
// call returning a CSteamID goes through here; calls returning scalars, pointers or bool are fine
// as they are. Internal: only for code already built with LUMINOVEAU_WITH_STEAM.

#ifdef LUMINOVEAU_WITH_STEAM

#include "steam_api.h"

// **Declared here rather than by including steam_api_flat.h**, which includes its siblings as
// `steam/steam_api.h`. The engine puts `sdk/public/steam` on the include path, not `sdk/public`,
// deliberately — `steam/` is exactly the prefix GameNetworkingSockets' headers collide on (see
// SteamDetection.cmake). These four are copied from that header, S_API and all, and are exported
// by steam_api64.dll under exactly these names.
S_API uint64 SteamAPI_ISteamUser_GetSteamID(ISteamUser *self);
S_API uint64 SteamAPI_ISteamMatchmaking_GetLobbyByIndex(ISteamMatchmaking *self, int iLobby);
S_API uint64 SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(ISteamMatchmaking *self, uint64 steamIDLobby,
                                                              int iMember);
S_API uint64 SteamAPI_ISteamMatchmaking_GetLobbyOwner(ISteamMatchmaking *self, uint64 steamIDLobby);

namespace lumi_steam {

inline CSteamID localUser() { return CSteamID(SteamAPI_ISteamUser_GetSteamID(SteamUser())); }

inline CSteamID lobbyOwner(CSteamID lobby) {
    return CSteamID(SteamAPI_ISteamMatchmaking_GetLobbyOwner(SteamMatchmaking(), lobby.ConvertToUint64()));
}

inline CSteamID lobbyByIndex(int index) {
    return CSteamID(SteamAPI_ISteamMatchmaking_GetLobbyByIndex(SteamMatchmaking(), index));
}

inline CSteamID lobbyMemberByIndex(CSteamID lobby, int index) {
    return CSteamID(
        SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(SteamMatchmaking(), lobby.ConvertToUint64(), index));
}

} // namespace lumi_steam

#endif
