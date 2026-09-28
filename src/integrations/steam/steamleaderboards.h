#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// @brief Steam leaderboards, by name.
///
/// A board is made in Steamworks and found here by its name the first time anything asks for it;
/// every call made before Steam has answered is held and carried out once it has. What a board is
/// *for* — lap times, a count, a flag — is the game's business: this only posts scores and reads
/// them back.
///
/// Everything is a no-op returning "nothing" without the SDK, on the web, or while
/// `Steam::IsReady()` is false.
class SteamLeaderboards {
public:
    /// How a new score treats the one already on the board.
    enum class Keep {
        Best,   ///< Only replaces it when better, by the board's own sort order.
        Latest, ///< Always replaces it.
    };

    struct Entry {
        uint64_t user  = 0; ///< SteamID.
        int32_t  score = 0;
        int      rank  = 0; ///< 1-based, across the whole board.
    };

    /// @brief Posts this player's score. A board not yet found is found first, and the post waits
    ///        for it; a second post while one is still on its way replaces the waiting one.
    static void Upload(const std::string &board, int32_t score, Keep keep = Keep::Best);

    /// @brief Starts fetching the board's entries for this player and their friends. Ignored while
    ///        a fetch for the same board is still running. The board's own read settings apply —
    ///        a board limited to friends answers this and nothing wider.
    static void RequestFriends(const std::string &board);

    /// @brief The most recent friends' entries fetched for `board`. False until the first fetch has
    ///        come back.
    static bool FriendsEntries(const std::string &board, std::vector<Entry> &out);

    /// @brief Drops every board and its pending calls. `Steam::Close` calls this before shutting the
    ///        API down, so no call result outlives it.
    static void Shutdown();
};
