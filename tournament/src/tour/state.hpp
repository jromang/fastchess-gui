#pragma once

// The live state of a tournament: settings, schedule and results, rebuilt from its folder when it is opened and
// kept on disk as games end. The folder holds:
//
//   tournament.json      the settings and participants, written once at creation
//   journal.jsonl        everything that happened since (see journal.hpp)
//   games.pgn            move comments in the cutechess / fastchess style
//   games-chessbase.pgn  [%eval] [%clk] [%emt] comments
//
// A finished game is written to both PGN files first, then to the journal (synced to disk). When a tournament is
// opened, PGN text past the last journaled game is cut, and games that were running are simply played again,
// with the opening their encounter started with. Resuming is therefore exact at any concurrency.

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <game/book/opening.hpp>
#include <matchmaking/sprt/sprt.hpp>
#include <matchmaking/stats.hpp>
#include <types/engine_config.hpp>
#include <types/match_data.hpp>

#include "journal.hpp"
#include "model.hpp"
#include "openings.hpp"
#include "pgn_file.hpp"
#include "schedule.hpp"

namespace fcg::tour {

// One game handed to the runner.
struct GameTask {
    Encounter encounter;
    int game = 0;  // 0: encounter.first has White, 1: encounter.second has
    fastchess::book::Opening opening;
    fastchess::EngineConfiguration white;
    fastchess::EngineConfiguration black;

    // 0-based game number in the tournament, stable across resumes.
    [[nodiscard]] uint64_t number() const noexcept { return uint64_t{encounter.index} * 2 + game; }
};

// A finished, scored game, as kept in the journal.
struct GameResult {
    uint32_t encounter = 0;
    int game           = 0;
    int white          = 0;  // participant ids
    int black          = 0;
    std::string result;  // "1-0", "0-1" or "1/2-1/2"
    fastchess::MatchTermination termination = fastchess::MatchTermination::NORMAL;
    std::string reason;
    uint32_t plies       = 0;
    uint32_t duration_ms = 0;
    PgnSpan pgn;              // games.pgn
    PgnSpan pgn_chessbase;    // games-chessbase.pgn
    int64_t finished_at = 0;  // unix time, milliseconds

    [[nodiscard]] uint64_t number() const noexcept { return uint64_t{encounter} * 2 + game; }

    // Score of participant `id` in this game: 1, 0.5 or 0.
    [[nodiscard]] double scoreOf(int id) const noexcept;
};

// The statistics of one complete encounter (its two games), from participant `pov`'s point of view, pentanomial
// counts included.
[[nodiscard]] fastchess::Stats encounterStats(const GameResult& a, const GameResult& b, int pov);

// What complete() recorded.
struct Recorded {
    GameResult result;
    // Set when the game completed its encounter: statistics of the two participants after it, from the point of
    // view of the lower id.
    std::optional<fastchess::Stats> pair;
};

class TournamentState {
   public:
    // Creates the folder (which must not hold a tournament yet) and its tournament.json. Throws on error.
    static std::unique_ptr<TournamentState> create(const std::filesystem::path& dir, TournamentSpec spec);

    // Opens an existing tournament. Throws, with a readable message, when the folder is not a tournament or its
    // files cannot be read.
    static std::unique_ptr<TournamentState> open(const std::filesystem::path& dir);

    [[nodiscard]] static bool isTournament(const std::filesystem::path& dir);

    TournamentState(const TournamentState&)            = delete;
    TournamentState& operator=(const TournamentState&) = delete;

    [[nodiscard]] const std::filesystem::path& dir() const noexcept { return dir_; }
    [[nodiscard]] const TournamentSpec& spec() const noexcept { return spec_; }
    [[nodiscard]] std::filesystem::path pgnPath() const { return dir_ / "games.pgn"; }
    [[nodiscard]] std::filesystem::path chessbasePgnPath() const { return dir_ / "games-chessbase.pgn"; }

    // Number of games the tournament plays, nullopt for a match without a game limit.
    [[nodiscard]] std::optional<uint64_t> totalGames() const;

    // Things the user should know about, found when the tournament was opened (a changed opening book...).
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return warnings_; }

    // --- tournament threads ---

    // The next game to play: games of encounters already started first (the second game of a pair comes right
    // after the first), then the next encounter of the schedule. nullopt when there is nothing to play now.
    [[nodiscard]] std::optional<GameTask> claimNext();

    // A claimed game that will not be finished (interrupted): it is played again later.
    void release(const GameTask& task);

    // Records a finished game: PGN files, then the journal. Updates the statistics and the SPRT. Throws when the
    // files cannot be written.
    Recorded complete(const GameTask& task, const fastchess::MatchData& data);

    // Is there anything left to claim?
    [[nodiscard]] bool hasWork() const;

    // Every game played, or the SPRT decided.
    [[nodiscard]] bool finished() const;
    [[nodiscard]] std::string finishReason() const;

    // --- any thread ---

    // Scored games, in the order they finished.
    [[nodiscard]] std::vector<GameResult> results() const;

    // Statistics of `a` against `b` from a's point of view, counted on complete encounters only (both games),
    // like fastchess does with pentanomial reporting. Includes the pentanomial counts.
    [[nodiscard]] fastchess::Stats pairStats(int a, int b) const;

    // pairStats(a, b) after each encounter between them completed, in the order they completed.
    [[nodiscard]] std::vector<fastchess::Stats> pairHistory(int a, int b) const;

   private:
    TournamentState(std::filesystem::path dir, TournamentSpec spec);

    void load();  // replays the journal
    // Returns true when the result completes its encounter.
    bool apply(const GameResult& result);
    void checkSprt();
    [[nodiscard]] GameTask makeTask(uint32_t encounter, int game);
    [[nodiscard]] bool done(uint32_t encounter, int game) const;
    [[nodiscard]] bool frc() const noexcept;

    std::filesystem::path dir_;
    TournamentSpec spec_;
    Schedule schedule_;
    OpeningSource openings_;
    std::vector<std::string> warnings_;

    mutable std::mutex mutex_;

    std::unique_ptr<Journal> journal_;
    std::unique_ptr<PgnFile> pgn_;
    std::unique_ptr<PgnFile> pgn_chessbase_;

    std::vector<GameResult> results_;
    std::set<uint64_t> done_;                                // game numbers
    std::map<uint32_t, fastchess::book::Opening> started_;   // encounter -> opening it started with
    std::map<uint32_t, std::size_t> half_;                   // encounter -> index in results_ of its first game
    std::map<std::pair<int, int>, fastchess::Stats> pairs_;  // (lower id, higher id), lower id's point of view

    uint32_t next_encounter_ = 0;                // first encounter never started
    std::deque<std::pair<uint32_t, int>> open_;  // games of started encounters still to play

    fastchess::SPRT sprt_;
    bool finished_ = false;
    std::string finish_reason_;
};

}  // namespace fcg::tour
