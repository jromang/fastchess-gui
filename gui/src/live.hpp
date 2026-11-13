#pragma once

// GuiObserver receives the tournament callbacks on the worker threads and turns them into
// state the GUI thread can copy cheaply: one locked record per slot, plus queues of finished
// games and statistics that the GUI drains once per frame. The games themselves are recorded
// (PGN files, journal) by the tournament layer, which tells the observer about each one.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <chess.hpp>
#include <core/observer.hpp>
#include <game/pgn/pgn_builder.hpp>
#include <matchmaking/stats.hpp>
#include <types/score.hpp>

#include <tour/runner.hpp>

#include "pgn_store.hpp"

namespace fcg {

using Clock = std::chrono::steady_clock;

struct LiveMove {
    std::string san;
    chess::Move move;
    chess::Color mover = chess::Color::WHITE;
    bool book          = false;

    std::optional<fastchess::Score> score;      // mover's point of view
    std::optional<std::array<int64_t, 3>> wdl;  // win, draw, loss in permille, mover's point of view
    int64_t depth    = 0;
    int64_t seldepth = 0;
    int64_t time_ms  = 0;
    uint64_t nodes   = 0;
    uint64_t nps     = 0;
    uint64_t tbhits  = 0;
    std::optional<int64_t> decision_ms;  // when it settled on the move
    std::string pv;                      // UCI moves, from the move played
};

// Everything the live view shows about one concurrent game.
struct SlotState {
    uint64_t version = 0;  // bumped on every change, so the GUI copies only when needed

    bool active          = false;
    bool has_game        = false;
    std::size_t game_id  = 0;
    std::size_t round_id = 0;
    std::string white;
    std::string black;
    std::string start_fen;
    std::vector<LiveMove> moves;

    chess::Color stm = chess::Color::WHITE;
    bool thinking    = false;
    Clock::time_point think_start{};
    int64_t white_ms = 0;  // clock after the last move, or at the start
    int64_t black_ms = 0;
    bool clocks      = false;

    // Last "info ... score ..." line of each side during its current or last search.
    std::array<std::string, 2> info;

    std::string result;  // set when the game is over
    std::string reason;
};

// In a gauntlet, whether the seed of a game between `white` and `black` has Black (true: the board is turned to its
// side) or White (false). nullopt for a game without a seed or between two seeds, and in the other formats. The
// seeds are the first spec.seeds participants, and those who replaced them.
[[nodiscard]] std::optional<bool> seedHasBlack(const tour::TournamentSpec& spec, const std::string& white,
                                               const std::string& black);

// A finished, scored game, small enough to keep millions of them.
struct GameRecord {
    uint64_t game_id    = 0;
    uint32_t round_id   = 0;
    uint16_t white      = 0;  // participant indexes
    uint16_t black      = 0;
    int8_t white_result = 0;     // +1 White won, 0 draw, -1 Black won
    bool a_white        = true;  // a match: did A (participant 0) have White, and how did it do
    int8_t a_result     = 0;     // +1 A won, 0 draw, -1 A lost
    uint8_t termination = 0;
    uint16_t reason     = 0;  // index into GuiObserver::reasons()
    uint16_t opening    = 0;  // index into GuiObserver::openings(); 0: none (Chess960, or an older PGN)
    EvalFacts facts;          // what its evaluations tell, for the "interesting games" filters
    uint16_t plies       = 0;
    uint16_t pv_warnings = 0;   // what fastchess' PV checks found
    int16_t pv_ply       = -1;  // the first move they were about
    uint32_t duration_ms = 0;
    PgnSpan fastchess_span;
    PgnSpan chessbase_span;
};

// The ECO code and name of an opening, from the game's PGN tags.
struct OpeningName {
    std::string eco;
    std::string name;
};

// Per engine counters collected from finished games.
struct EngineTotals {
    uint64_t moves            = 0;
    uint64_t depth_sum        = 0;
    double nps_sum            = 0;
    uint64_t nps_samples      = 0;
    uint64_t nodes_sum        = 0;  // over the nodes_samples moves that told their nodes
    uint64_t nodes_samples    = 0;
    uint64_t time_ms          = 0;
    uint64_t decision_ms      = 0;  // when it settled on its move, summed over decision_samples moves
    uint64_t decision_samples = 0;
    int timeouts              = 0;
    int crashes               = 0;
    int illegal               = 0;

    // Processor time of its processes while it thinks and while its opponent does, with the time those lasted. Kept
    // with each game (processor.tsv in the tournament's folder), so that a resumed tournament has its games' too.
    uint64_t own_cpu_ms   = 0;
    uint64_t own_wall_ms  = 0;
    uint64_t wait_cpu_ms  = 0;
    uint64_t wait_wall_ms = 0;
};

// How an engine used the processor: the cores it kept busy while it thought, and the share of a core it used while
// its opponent thought. Not measured until it has thought, and waited, for a while.
struct CpuUse {
    bool measured   = false;
    double thinking = 0;
    double waiting  = 0;
};
[[nodiscard]] CpuUse cpuUse(const EngineTotals& totals);

// What looks unfair, empty when nothing does: more cores than its `threads` while it thinks, or thinking on its
// opponent's time (pondering it was not asked for, a background thread).
[[nodiscard]] std::string cpuProblem(const EngineTotals& totals, int threads);

// What the processes of the two engines of a game used, White's then Black's: the totals of one game.
struct GameCpu {
    std::array<uint64_t, 2> own_cpu_ms{}, own_wall_ms{}, wait_cpu_ms{}, wait_wall_ms{};
};

// A line of processor.tsv: the game, by its PGN offset and when it ended, then what its engines used. nullopt when
// the line is not one (cut short by a crash).
struct GameCpuLine {
    uint64_t offset     = 0;
    int64_t finished_at = 0;
    GameCpu cpu;
};
[[nodiscard]] std::string gameCpuLine(const GameCpuLine& line);
[[nodiscard]] std::optional<GameCpuLine> parseGameCpuLine(std::string_view line);

// The processor time the engines of each concurrent game use, read when "go" is sent and when the move comes, and
// appended to processor.tsv (in the tournament's folder) with each game recorded. The window's observer measures
// with it, and so does the headless mode.
class CpuMeter {
   public:
    CpuMeter(int slots, std::filesystem::path file);
    ~CpuMeter();
    CpuMeter(const CpuMeter&)            = delete;
    CpuMeter& operator=(const CpuMeter&) = delete;

    // What the engines used while one of them thought: the processor time of each, White's then Black's, and how
    // long it lasted.
    struct Used {
        std::array<uint64_t, 2> cpu_ms{};
        uint64_t wall_ms = 0;
    };

    // --- tournament threads, from the observer callbacks of the same names ---
    void gameStart(int slot);
    void engineProcesses(int slot, int64_t white_pid, int64_t black_pid);
    void thinking(int slot);
    // What the engines used since thinking(); nullopt when it could not be read.
    std::optional<Used> moved(int slot, chess::Color mover);
    void gameEnd(int slot, bool counted);
    // Appends the line of a game to the file, synced; on the thread that ended it, after gameEnd(), as the runner
    // calls them. Nothing for a game whose engines were not measured.
    void recorded(const tour::GameResult& result);

   private:
    struct Slot {
        std::mutex mutex;
        std::array<std::vector<int64_t>, 2> processes;  // the engines' processes
        std::array<int64_t, 2> cpu_at_go{-1, -1};       // the readings when "go" was sent
        Clock::time_point go_at{};
        GameCpu game;  // the game in progress
    };
    Slot* slot(int i) { return i >= 0 && i < static_cast<int>(slots_.size()) ? slots_[i].get() : nullptr; }

    std::vector<std::unique_ptr<Slot>> slots_;
    std::filesystem::path path_;
    std::mutex file_mutex_;
    std::FILE* file_ = nullptr;
    bool failed_     = false;  // could not be opened: not tried again
};

class GuiObserver final : public fastchess::observer::GameObserver, public tour::RecordListener {
   public:
    // `state` is the match about to be played; the games it already holds (a resumed match) are queued as if
    // they had just been played, read from the PGN on a thread of their own (a big tournament takes seconds), before
    // the games played meanwhile.
    GuiObserver(int slots, const tour::TournamentState& state);
    ~GuiObserver() override;
    GuiObserver(const GuiObserver&)            = delete;
    GuiObserver& operator=(const GuiObserver&) = delete;

    // --- callbacks, tournament threads ---
    void onGameStart(const fastchess::observer::GameStart& info) override;
    void onEngineProcesses(int slot, int64_t white_pid, int64_t black_pid) override;
    void onThinking(int slot, chess::Color stm, int64_t time_left_ms) override;
    void onEngineLine(int slot, chess::Color side, std::string_view line) override;
    void onMove(int slot, chess::Color mover, const fastchess::MoveData& move, const std::string& san,
                int64_t white_time_left, int64_t black_time_left) override;
    void onGameEnd(int slot, std::size_t game_id, std::size_t round_id, const fastchess::MatchData& data,
                   bool counted) override;
    void onRecorded(const tour::GameResult& result, const fastchess::MatchData& data,
                    const std::optional<fastchess::Stats>& pair) override;

    // --- GUI thread ---
    [[nodiscard]] int slotCount() const noexcept { return static_cast<int>(slots_.size()); }

    // Copies slot `i` into `out` if it changed since `out.version`. Returns true if it did.
    bool copySlot(int i, SlotState& out) const;

    // The last games that ended in slot `i`, oldest first, kept when the next ones start: the next game often starts
    // before a broadcast saw the end of the previous one, and fast games (a few nodes per move) end several at a
    // time between two snapshots.
    [[nodiscard]] std::vector<SlotState> endedGames(int i) const;

    // Is anything going on in slot `i` (cheap, for tab labels)?
    [[nodiscard]] bool slotActive(int i) const;

    // The games a resumed tournament already holds are still being read.
    [[nodiscard]] bool loading() const;

    // Moves the games and statistics that arrived since the last call into the given vectors.
    void drain(std::vector<GameRecord>& games, std::vector<fastchess::Stats>& stats);

    [[nodiscard]] std::vector<std::string> reasons() const;
    [[nodiscard]] std::vector<OpeningName> openings() const;  // [0] is "no opening"
    [[nodiscard]] std::vector<EngineTotals> totals() const;   // by participant index ([0] = A, [1] = B in a match)
    // The same with each participant's name, taken together: from any thread.
    [[nodiscard]] std::vector<std::pair<std::string, EngineTotals>> namedTotals() const;

    // New participants (added or replacing one) get a name and counters; existing indexes do not change.
    void updateParticipants(const tour::TournamentSpec& spec);

    // The records of the games that count, for the views to start over after an edit. The games whose PGN was not
    // read yet (a resumed tournament still loading) are left out and come through drain() once read, in their
    // order: the lists end up as a fresh open of the tournament would show them.
    [[nodiscard]] std::vector<GameRecord> records(const std::vector<tour::GameResult>& results);

    // Participant names, by index.
    [[nodiscard]] const std::vector<std::string>& names() const noexcept { return names_; }
    [[nodiscard]] const std::string& name(std::size_t index) const;

    [[nodiscard]] const std::filesystem::path& fastchessPgn() const noexcept { return pgn_fastchess_; }
    [[nodiscard]] const std::filesystem::path& chessbasePgn() const noexcept { return pgn_chessbase_; }

    [[nodiscard]] const std::string& engineA() const noexcept { return engine_a_; }
    [[nodiscard]] const std::string& engineB() const noexcept { return engine_b_; }

   private:
    struct Slot {
        mutable std::mutex mutex;
        SlotState state;
        std::deque<SlotState> ended;             // the previous games, once they have a result; at most 8
        chess::Board board;                      // current position of the game in this slot
        std::array<int, 2> participant{-1, -1};  // the participant indexes of White and Black, for their totals
    };

    Slot* slot(int i) { return i >= 0 && i < static_cast<int>(slots_.size()) ? slots_[i].get() : nullptr; }

    uint16_t internReason(const std::string& reason);
    [[nodiscard]] int participantIndex(const std::string& name) const;  // -1 when unknown
    // What a game's PGN text tells (read from the file when `pgn` is empty), once per game; data_mutex_ held.
    struct GameExtras {
        uint16_t opening = 0;
        EvalFacts facts;
    };
    const GameExtras& extrasOf(const tour::GameResult& result, std::string_view pgn = {});
    // One move of an engine in its search statistics; data_mutex_ held.
    static void addMove(EngineTotals& totals, int64_t depth, int64_t time_ms, int64_t nps, uint64_t nodes,
                        std::optional<int64_t> decision_ms);
    GameRecord makeRecord(const tour::GameResult& result, std::string_view pgn = {});  // data_mutex_ held
    void countLoss(const tour::GameResult& result);                                    // data_mutex_ held
    // Reads the games of to_load_, on loader_.
    void loadPlayed();
    // What the loader read of a game's PGN.
    struct LoadedGame {
        OpeningName opening;
        EvalFacts facts;
        std::array<EngineTotals, 2> side;  // White's moves, Black's
    };
    // The record of a game the loader read, counted in the totals the first time; data_mutex_ held.
    GameRecord addLoaded(const tour::GameResult& result, LoadedGame& game);

    std::string engine_a_;
    std::string engine_b_;
    int id_a_   = 0;  // participant ids
    int id_b_   = 1;
    bool match_ = true;
    std::vector<std::string> names_;
    std::unordered_map<int, uint16_t> index_;  // participant id -> index

    std::vector<std::unique_ptr<Slot>> slots_;

    std::filesystem::path pgn_fastchess_;
    std::filesystem::path pgn_chessbase_;

    // processor.tsv: its size when the tournament was opened (the lines of the games played before, which the
    // loader reads), and what measures the games played now and appends them to it.
    std::filesystem::path cpu_path_;
    uint64_t cpu_known_ = 0;
    CpuMeter cpu_;
    // The lines of the games played before, by PGN offset, those of the games not counted yet once the first loader
    // is done; loader thread only (one loader at a time).
    bool cpu_read_ = false;
    std::vector<GameCpuLine> cpu_lines_;

    mutable std::mutex data_mutex_;
    std::vector<GameRecord> pending_games_;
    // game_id of every game handed out since records(): a game recorded while records() read the results would
    // otherwise come again through drain().
    std::unordered_set<uint64_t> shown_;
    std::vector<fastchess::Stats> pending_stats_;
    std::vector<std::string> reasons_;
    std::unordered_map<std::string, uint16_t> reason_index_;
    std::vector<OpeningName> openings_{1};
    std::unordered_map<std::string, uint16_t> opening_index_;  // "eco|name" -> index
    // By the game's PGN offset. A game there is counted in totals_, once: by the loader or when it was recorded.
    std::unordered_map<uint64_t, GameExtras> extras_;
    std::vector<EngineTotals> totals_;

    // While the games already played are read, the games recorded meanwhile wait here to come after them.
    bool loading_ = false;
    std::vector<GameRecord> arrived_;
    // The games for the loader to read, and their generation: records() hands it new ones, and it drops what it
    // read of the older ones (games an edit voided may be among them).
    std::vector<tour::GameResult> to_load_;
    uint64_t generation_ = 0;
    std::atomic<bool> stop_loading_{false};
    std::thread loader_;  // last: started once everything else is there
};

}  // namespace fcg
