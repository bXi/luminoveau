#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// @brief A Steam lobby and rich presence, usable whichever `Net` brokerage is active.
///
/// The Steam *broker* owns lobbies only while it is the brokerage, and then the lobby is the
/// session. A game racing on another brokerage — signalling and WebRTC, say, so that browser players
/// can join — can still want Steam's social side: a lobby friends can see and be invited into, and
/// rich presence for the friends list's Join Game. This is that, on its own. It never carries game
/// data and never decides who may connect; what a lobby's data means is the game's business.
///
/// Asynchronous results and Steam's join requests arrive as events from `Poll`, which the game calls
/// once a frame after `Steam::Tick`. Everything is a no-op returning "nothing" without the SDK, on
/// the web, or while `Steam::IsReady()` is false.
///
/// **Do not use this while the active `Net` brokerage is Steam.** Both register for the same join
/// callbacks, and each would act on every invite.
class SteamLobby {
public:
    enum class Visibility {
        FriendsOnly, ///< Joinable by friends and by invite; absent from lobby searches.
        Public,      ///< Also returned by lobby searches.
    };

    struct Event {
        enum class Type {
            None,
            Created,       ///< `Create` succeeded; `lobby` is ours and we own it.
            CreateFailed,
            Entered,       ///< `Join` succeeded; `lobby`'s data is readable.
            EnterFailed,
            JoinRequested, ///< The player accepted an invite or chose Join Game; `lobby` is where.
        } type = Type::None;

        uint64_t lobby = 0;
    };

    /// @brief Starts creating a lobby this player owns. Leaves any current one first.
    static void Create(Visibility visibility, int maxMembers);

    /// @brief Starts entering an existing lobby. Leaves any current one first.
    static void Join(uint64_t lobby);

    /// @brief Leaves the current lobby, if any. Rich presence is left alone.
    static void Leave();

    static bool     InLobby();
    static uint64_t Id();
    static bool     IsOwner();

    /// @brief Members Steam counts in the current lobby, which is only ever Steam accounts.
    static int MemberCount();

    /// @brief Sets a key on the current lobby. Owner only; ignored otherwise. Steam rate-limits
    ///        these writes, so set on change rather than every frame.
    static void SetData(const std::string &key, const std::string &value);

    /// @brief Reads a key of the current lobby, or empty.
    static std::string GetData(const std::string &key);

    /// @brief Sets one rich presence key for this player. An empty value removes the key.
    static void SetRichPresence(const std::string &key, const std::string &value);

    /// @brief Removes every rich presence key this player has set.
    static void ClearRichPresence();

    /// @brief Opens the Steam overlay's invite dialog for the current lobby: the friends list,
    ///        each with an invite button. False when there is no lobby to invite into. Steam draws
    ///        the dialog; an invite accepted on the other end arrives there as a JoinRequested event.
    static bool OpenInviteDialog();

    /// @brief One of this player's Steam friends, as an in-game invite list wants them.
    struct Friend {
        uint64_t    id = 0;
        std::string name;
        bool        away       = false; ///< Away, snoozing or busy rather than simply online.
        bool        inThisGame = false; ///< Running this same app right now.
    };

    /// @brief This player's friends who are online, for a game drawing its own invite list rather
    ///        than opening the overlay. Offline friends are left out — an invite to them waits
    ///        until they next start Steam, which is not what "invite" means on a lobby screen.
    ///        Friends in this game come first, then the rest; within each, online before away,
    ///        then by name.
    static std::vector<Friend> Friends();

    /// @brief Invites one friend into the current lobby. False when there is no lobby, or Steam
    ///        refused. An accepted invite arrives on their end as a JoinRequested event.
    static bool Invite(uint64_t friendId);

    /// @brief The lobby a Steam `+connect_lobby <id>` argument names, or 0. Steam starts a game with
    ///        that on its command line when Join Game is chosen while the game is not running.
    static uint64_t LobbyFromCommandLine(int argc, char *argv[]);

    /// @brief Takes the next pending event. False when there is none.
    static bool Poll(Event &out);

    /// @brief Leaves any lobby and drops the callbacks. `Steam::Close` calls this before shutting
    ///        the API down, so a callback is never unregistered from a Steam that is already gone.
    static void Shutdown();
};
