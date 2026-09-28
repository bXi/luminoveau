#include "steamleaderboards.h"

#include "steam.h"

#ifdef LUMINOVEAU_WITH_STEAM

#include <map>
#include <memory>

namespace {

/// One named board and everything waiting on it. Call results need an object to land on, so each
/// board is one, and a map of them is the whole state.
class Board {
public:
    explicit Board(std::string name) : _name(std::move(name)) {}

    void upload(int32_t score, SteamLeaderboards::Keep keep) {
        _pendingScore = score;
        _pendingKeep  = keep;
        _hasPending   = true;
        pump();
    }

    void requestFriends() {
        _wantFriends = true;
        pump();
    }

    bool friends(std::vector<SteamLeaderboards::Entry> &out) const {
        if (!_haveFriends) return false;
        out = _friends;
        return true;
    }

    /// True once Steam has said there is no board by this name.
    bool missing() const { return _missing; }

private:
    /// Does whatever is waiting and can go now: find the board first, then one post and one fetch
    /// at a time.
    void pump() {
        ISteamUserStats *stats = SteamUserStats();
        if (stats == nullptr) return;

        if (_handle == 0) {
            if (!_finding) {
                _finding = true;
                _findResult.Set(stats->FindLeaderboard(_name.c_str()), this, &Board::onFound);
            }
            return;
        }

        if (_hasPending && !_uploading) {
            _hasPending = false;
            _uploading  = true;
            const ELeaderboardUploadScoreMethod method = _pendingKeep == SteamLeaderboards::Keep::Best
                                                             ? k_ELeaderboardUploadScoreMethodKeepBest
                                                             : k_ELeaderboardUploadScoreMethodForceUpdate;
            _uploadResult.Set(stats->UploadLeaderboardScore(_handle, method, _pendingScore, nullptr, 0),
                              this, &Board::onUploaded);
        }

        if (_wantFriends && !_downloading) {
            _wantFriends = false;
            _downloading = true;
            _downloadResult.Set(
                stats->DownloadLeaderboardEntries(_handle, k_ELeaderboardDataRequestFriends, 0, 0), this,
                &Board::onDownloaded);
        }
    }

    void onFound(LeaderboardFindResult_t *result, bool ioFailure) {
        _finding = false;
        if (ioFailure || result == nullptr || !result->m_bLeaderboardFound) {
            // Left unfound for the rest of the run: asking again every call would only repeat it.
            LOG_WARNING("SteamLeaderboards: no leaderboard called '{}' - is it made and published?", _name);
            _hasPending  = false;
            _wantFriends = false;
            _missing     = true;
            return;
        }
        _handle = result->m_hSteamLeaderboard;
        pump();
    }

    void onUploaded(LeaderboardScoreUploaded_t *result, bool ioFailure) {
        _uploading = false;
        // The usual cause is the board's writes being Trusted, which only a server may post to.
        if (ioFailure || result == nullptr || !result->m_bSuccess) {
            LOG_WARNING("SteamLeaderboards: could not post to '{}' - are its writes set to Trusted?", _name);
        }
        pump();
    }

    void onDownloaded(LeaderboardScoresDownloaded_t *result, bool ioFailure) {
        _downloading = false;
        if (!ioFailure && result != nullptr && SteamUserStats() != nullptr) {
            _friends.clear();
            for (int i = 0; i < result->m_cEntryCount; ++i) {
                LeaderboardEntry_t entry{};
                if (!SteamUserStats()->GetDownloadedLeaderboardEntry(result->m_hSteamLeaderboardEntries, i,
                                                                      &entry, nullptr, 0)) {
                    continue;
                }
                // Read out of the struct rather than returned by value — see steamabi.h.
                _friends.push_back({entry.m_steamIDUser.ConvertToUint64(), entry.m_nScore,
                                    entry.m_nGlobalRank});
            }
            _haveFriends = true;
        }
        pump();
    }

    std::string        _name;
    SteamLeaderboard_t _handle  = 0;
    bool               _finding = false;
    bool               _missing = false;

    bool                    _hasPending   = false;
    int32_t                 _pendingScore = 0;
    SteamLeaderboards::Keep _pendingKeep  = SteamLeaderboards::Keep::Best;
    bool                    _uploading    = false;

    bool                                 _wantFriends = false;
    bool                                 _downloading = false;
    bool                                 _haveFriends = false;
    std::vector<SteamLeaderboards::Entry> _friends;

    CCallResult<Board, LeaderboardFindResult_t>       _findResult;
    CCallResult<Board, LeaderboardScoreUploaded_t>    _uploadResult;
    CCallResult<Board, LeaderboardScoresDownloaded_t> _downloadResult;
};

std::map<std::string, std::unique_ptr<Board>> g_boards;

/// Null while Steam is not up, or for a board already known not to exist.
Board *boardNamed(const std::string &name) {
    if (!Steam::IsReady() || SteamUserStats() == nullptr || name.empty()) return nullptr;
    auto it = g_boards.find(name);
    if (it == g_boards.end()) it = g_boards.emplace(name, std::make_unique<Board>(name)).first;
    return it->second->missing() ? nullptr : it->second.get();
}

} // namespace

void SteamLeaderboards::Upload(const std::string &board, int32_t score, Keep keep) {
    if (Board *b = boardNamed(board)) b->upload(score, keep);
}

void SteamLeaderboards::RequestFriends(const std::string &board) {
    if (Board *b = boardNamed(board)) b->requestFriends();
}

bool SteamLeaderboards::FriendsEntries(const std::string &board, std::vector<Entry> &out) {
    auto it = g_boards.find(board);
    return it != g_boards.end() && it->second->friends(out);
}

void SteamLeaderboards::Shutdown() { g_boards.clear(); }

#else

void SteamLeaderboards::Upload(const std::string &, int32_t, Keep) {}
void SteamLeaderboards::RequestFriends(const std::string &) {}
bool SteamLeaderboards::FriendsEntries(const std::string &, std::vector<Entry> &) { return false; }
void SteamLeaderboards::Shutdown() {}

#endif
