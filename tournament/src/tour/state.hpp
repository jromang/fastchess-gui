#pragma once

// The live state of a tournament: settings, schedule and results, rebuilt from its folder when it is opened and
// kept on disk as games end. The folder holds:
//
//   tournament.json      the settings and participants, written once at creation
//   journal.jsonl        everything that happened since (see journal.hpp)
//   games.pgn            move comments in the cutechess / fastchess style
//   games-chessbase.pgn  [%eval] [%clk] [%emt] comments
//
// Both are written in the order games end, and the journal finds every game by its place in them. The PGN file of
// the user's (spec.pgn_out) gets them in schedule order instead: see writeOut().
//
// A finished game is written to both PGN files first, then to the journal, each synced to disk. When a tournament
// is opened, PGN text past the last journaled game is cut, and games that were running are simply played again,
// with the opening their encounter started with. Resuming is therefore exact at any concurrency.
//
// One TournamentState of one program at a time has a folder open for writing (tournament.lock, see files.hpp);
// others can open it read-only.

#include <atomic>
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

#include "edits.hpp"
#include "files.hpp"
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
    // What fastchess' PV checks found (an illegal PV move, a mate PV that does not mate...): (ply, warning).
    std::vector<std::pair<uint32_t, std::string>> pv_warnings;

    // No longer counted (replay requested, participant replaced or withdrawn); kept to be shown as such.
    bool voided = false;
    std::string void_reason;

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
    // False when the game was not recorded: already played, dropped, or its encounter changed while it was played.
    bool kept = true;
};

// Where a game of the schedule stands.
enum class GameStatus { Pending, Running, Done, Dropped };

struct ScheduleRow {
    Encounter encounter;
    GameStatus status[2] = {GameStatus::Pending, GameStatus::Pending};
};

class TournamentState {
   public:
    // Creates the folder (which must not hold a tournament yet) and its tournament.json. Throws on error.
    static std::unique_ptr<TournamentState> create(const std::filesystem::path& dir, TournamentSpec spec);

    // Opens an existing tournament. Throws, with a readable message, when the folder is not a tournament or its
    // files cannot be read, and FolderBusy when another program (or TournamentState) has it open for writing.
    // `read_only` writes nothing, takes no lock and makes no edit: to show a tournament another program plays.
    static std::unique_ptr<TournamentState> open(const std::filesystem::path& dir, bool read_only = false);

    [[nodiscard]] static bool isTournament(const std::filesystem::path& dir);

    // Replays journal events on settings and schedule only, touching no file (for a summary of a tournament that
    // may be running). `spec` receives the participants as edits left them.
    struct Replayed {
        uint64_t played = 0;  // results that count
        std::optional<uint64_t> total;
        bool finished = false;
        std::string finish_reason;
        std::vector<GameResult> results;  // the games that count
    };
    [[nodiscard]] static Replayed replay(const std::vector<nlohmann::json>& events, TournamentSpec& spec);

    TournamentState(const TournamentState&)            = delete;
    TournamentState& operator=(const TournamentState&) = delete;

    [[nodiscard]] const std::filesystem::path& dir() const noexcept { return dir_; }
    [[nodiscard]] bool readOnly() const noexcept { return read_only_; }
    [[nodiscard]] const TournamentSpec& spec() const noexcept { return spec_; }
    [[nodiscard]] std::filesystem::path pgnPath() const { return dir_ / "games.pgn"; }
    [[nodiscard]] std::filesystem::path chessbasePgnPath() const { return dir_ / "games-chessbase.pgn"; }

    // Number of games the tournament plays, nullopt for a match without a game limit.
    [[nodiscard]] std::optional<uint64_t> totalGames() const;

    // --- edits (round robin and gauntlet; voiding games works for a match too) ---

    // What `edit` would change, without making it; Impact::error says why it cannot be made.
    [[nodiscard]] Impact preview(const Edit& edit) const;

    // Makes `edit`: journaled, then applied; the runner sees the new schedule at its next claim. Throws with a
    // readable message when the edit cannot be made.
    void applyEdit(const Edit& edit);

    // The last edit can be undone until the next game starts: nothing has been played under it yet. Games that
    // were running when it was made and finished since are kept. Journaled, so it holds after a resume. Throws
    // when it is too late.
    [[nodiscard]] bool canUndo() const;
    void undoEdit();

    // The whole schedule with the status of each game (empty for a match).
    [[nodiscard]] std::vector<ScheduleRow> scheduleRows() const;

    // Swiss: byes received, by participant id (a bye counts as a drawn pair of games, 1 point).
    [[nodiscard]] std::map<int, int> byes() const;

    // Knockout: the places of each round in bracket order (a pair, or a single player with a bye).
    [[nodiscard]] std::map<uint32_t, std::vector<std::vector<int>>> brackets() const;

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

    // Is the claimed game still to be played as it was claimed: not dropped, nor its encounter changed by an edit?
    [[nodiscard]] bool wanted(const GameTask& task) const;

    // Is there anything left to claim?
    [[nodiscard]] bool hasWork() const;

    // No game of the encounter is left to start: both played, or no longer to be played.
    [[nodiscard]] bool settled(uint32_t encounter) const;

    // The engine names of the participants still playing (not withdrawn, not replaced).
    [[nodiscard]] std::set<std::string> activeEngines() const;

    // Every game played, or the SPRT decided.
    [[nodiscard]] bool finished() const;
    [[nodiscard]] std::string finishReason() const;

    // --- any thread ---

    // Scored games, in the order they finished, voided ones included (flagged).
    [[nodiscard]] std::vector<GameResult> results() const;

    // Changes each time a game is claimed, released or recorded and at each edit, once the change is made: what the
    // views compute from the schedule and the results is computed again only when it moved.
    [[nodiscard]] uint64_t version() const noexcept { return version_.load(); }

    // Why the last write to spec().pgn_out failed, empty when it worked; the games wait for the next try.
    [[nodiscard]] std::string pgnOutError() const;

    // The opening each encounter started with (book position and moves), by encounter index.
    [[nodiscard]] std::map<uint32_t, fastchess::book::Opening> encounterOpenings() const;

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
    bool count(std::size_t index);                 // adds results_[index] to the statistics
    void applyEvent(const nlohmann::json& event);  // an edit event, journal replay or new; mutex_ held

    // What an edit event does to participants, schedule and results, on the given state (members or copies).
    static void effects(const nlohmann::json& event, TournamentSpec& spec, Schedule& schedule,
                        std::vector<GameResult>& results, std::set<uint64_t>& done, std::set<uint64_t>& dropped);
    void rebuildStats();
    void pairIfReady();                                 // Swiss, knockout: the next round when one is due
    void applyRoundEvent(const nlohmann::json& event);  // round_paired, tiebreak_added; mutex_ held
    [[nodiscard]] double pairPoints(int a, int b, uint32_t round) const;  // a's points against b in a round
    static void roundEventTo(const nlohmann::json& event, const TournamentSpec& spec, Schedule& schedule);
    void rebuildOpen();
    [[nodiscard]] bool hasWorkLocked() const;
    [[nodiscard]] bool gamesLeft() const;  // games to claim, finished or not; mutex_ held
    [[nodiscard]] uint64_t totalLocked() const;
    void checkSprt();
    // Every game played, none running: the end is journaled. mutex_ held.
    void finishIfComplete();
    [[nodiscard]] GameTask makeTask(uint32_t encounter, int game);
    [[nodiscard]] bool done(uint32_t encounter, int game) const;
    [[nodiscard]] bool frc() const noexcept;
    const OpeningSource& openings();  // loads the book on first use; mutex_ held
    // The opening a new encounter starts with: the next of the book, or its round's (spec_.opening_per_round).
    fastchess::book::Opening openingFor(const Encounter& encounter);
    [[nodiscard]] bool openingPerRound() const noexcept;
    // Appends to spec_.pgn_out the finished games every game before which, in the schedule, is written there or
    // will not be played: the two games of an encounter side by side, rounds in order. A replayed game is written
    // when it ends; at the end of the tournament, everything left. Journaled ("pgn_out"), so a resume carries on.
    // A failure is kept for pgnOutError() and tried again next time. mutex_ held.
    void writeOut();

    // What the last edit changed, to undo it (see canUndo()).
    struct UndoPoint {
        TournamentSpec spec;
        Schedule schedule;
        std::vector<std::pair<bool, std::string>> voided;  // voided and void_reason of each result then
        std::set<uint64_t> done;
        std::set<uint64_t> dropped;
        uint64_t claims = 0;
    };

    std::filesystem::path dir_;
    bool read_only_ = false;
    std::unique_ptr<FolderLock> lock_;
    TournamentSpec spec_;
    Schedule schedule_;
    std::unique_ptr<OpeningSource> openings_;  // loaded when a new encounter first needs it
    std::vector<std::string> warnings_;

    mutable std::mutex mutex_;

    std::unique_ptr<Journal> journal_;
    std::unique_ptr<PgnFile> pgn_;
    std::unique_ptr<PgnFile> pgn_chessbase_;

    std::vector<GameResult> results_;
    std::set<uint64_t> done_;                               // game numbers
    std::map<uint32_t, fastchess::book::Opening> started_;  // encounter -> opening it started with
    // spec_.opening_per_round: (cycle, round) -> the opening of its encounters.
    std::map<std::pair<uint32_t, uint32_t>, fastchess::book::Opening> round_openings_;
    std::map<uint32_t, std::size_t> half_;                   // encounter -> index in results_ of its first game
    std::map<std::pair<int, int>, fastchess::Stats> pairs_;  // (lower id, higher id), lower id's point of view

    std::map<int, int> byes_;
    std::map<uint32_t, std::vector<std::vector<int>>> brackets_;
    uint32_t round_ = 0;           // Swiss, knockout: last round paired
    std::set<uint64_t> dropped_;   // games that will not be played (withdrawn participant, voided without replay)
    std::set<uint64_t> running_;   // claimed by the runner, not finished
    uint32_t next_encounter_ = 0;  // first encounter never started
    std::deque<std::pair<uint32_t, int>> open_;  // games of started encounters still to play

    // spec_.pgn_out
    std::vector<std::size_t> out_held_;  // indexes in results_ of the games not written there yet, voided included
    std::set<uint64_t> out_written_;     // numbers of the games written there
    uint64_t out_floor_ = 0;             // every game number below is written there, done or dropped
    std::string out_error_;

    std::optional<UndoPoint> undo_;
    uint64_t claims_ = 0;  // games claimed since the tournament was opened

    // version(): bumped when a change is over, the mutex released (declared before the lock_guard).
    std::atomic<uint64_t> version_{0};
    struct Bump {
        std::atomic<uint64_t>& version;
        ~Bump() { ++version; }
    };

    fastchess::SPRT sprt_;
    bool finished_ = false;
    std::string finish_reason_;
};

}  // namespace fcg::tour
