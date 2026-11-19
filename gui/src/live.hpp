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
#include <functional>
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

// The game of a slot as far as it went, as the Board page and the game graph read games: its players, start
// position, moves with what the engines said, and result.
[[nodiscard]] ReplayGame replayOfSlot(const SlotState& st);

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
    bool result_set     = false;  // the result was set by hand (tour::GameResult::set)
    int8_t unbalanced   = 0;      // its opening was unbalanced: +1 for White, -1 for Black (tour::GameResult)
    uint16_t reason     = 0;      // index into GuiObserver::reasons()
    uint16_t opening    = 0;      // index into GuiObserver::openings(); 0: none (Chess960, or an older PGN)
    EvalFacts facts;              // what its evaluations tell, for the "interesting games" filters
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

// The opening of a game as far as it went: the deepest of its positions that fastchess' table names (the ECO code and
// name its PGN gets once over), book moves and engine moves alike, so that a transposition after the book is named
// too; and where its book ended. No name in Chess960, nor when no position of the game is in the table.
struct LiveOpening {
    std::optional<OpeningName> name;
    int last_book = -1;  // index of the last book move, -1 without one
};
[[nodiscard]] LiveOpening liveOpening(const SlotState& st, bool chess960);

// The position of a game after its first `ply` moves.
[[nodiscard]] chess::Board slotBoard(const SlotState& st, int ply);

// "13.Kb1" or "13…Nf6": move `index` of a game with its number.
[[nodiscard]] std::string numberedSan(const SlotState& st, std::size_t index);

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

// An engine's search on average over the moves it searched (book moves left out), each figure nullopt until a move
// told it: the nps only from engines that give it, written in the PGN with nps=.
struct SearchAverage {
    std::optional<double> depth, nps, time_ms, decision_ms;
};
[[nodiscard]] SearchAverage searchAverage(const EngineTotals& totals);

// A PV in UCI notation as SAN, with move numbers, played from `board`: at most `max_moves`, then "…". Stops at the
// first move that is not legal.
[[nodiscard]] std::string pvToSan(chess::Board board, const std::vector<std::string>& pv, std::size_t max_moves);

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

// What the views read of a tournament's games, on the GUI thread, wherever the tournament plays: GuiObserver, told by
// the runner in this process, or by the tournament's process through the control channel (control.hpp). The Live,
// Games and Overview tabs, the charts and the broadcast read only this.
class LiveSource {
   public:
    virtual ~LiveSource() = default;

    [[nodiscard]] virtual int slotCount() const noexcept = 0;
    // Copies slot `i` into `out` if it changed since `out.version`. Returns true if it did.
    virtual bool copySlot(int i, SlotState& out) const = 0;
    // The last games that ended in slot `i`, oldest first, kept when the next ones start: the next game often starts
    // before a broadcast saw the end of the previous one, and fast games (a few nodes per move) end several at a
    // time between two snapshots.
    [[nodiscard]] virtual std::vector<SlotState> endedGames(int i) const = 0;
    // Is anything going on in slot `i` (cheap, for tab labels)?
    [[nodiscard]] virtual bool slotActive(int i) const = 0;
    // The participants of White and Black (indexes of names(), -1 for a name not known) of the game played in slot
    // `i`; nullopt when none is (cheap, for the cores in use).
    [[nodiscard]] virtual std::optional<std::array<int, 2>> slotPlayers(int i) const = 0;
    // The games a resumed tournament already holds are still being read.
    [[nodiscard]] virtual bool loading() const = 0;
    // Moves the games and statistics that arrived since the last call into the given vectors.
    virtual void drain(std::vector<GameRecord>& games, std::vector<fastchess::Stats>& stats) = 0;
    [[nodiscard]] virtual std::vector<std::string> reasons() const                           = 0;
    [[nodiscard]] virtual std::vector<OpeningName> openings() const = 0;  // [0] is "no opening"
    [[nodiscard]] virtual std::vector<EngineTotals> totals() const  = 0;  // by participant index
    // The same with each participant's name, taken together: from any thread.
    [[nodiscard]] virtual std::vector<std::pair<std::string, EngineTotals>> namedTotals() const = 0;
    // New participants (added or replacing one) get a name and counters; existing indexes do not change.
    virtual void updateParticipants(const tour::TournamentSpec& spec) = 0;
    // The records of the games that count, for the views to start over after an edit (see GuiObserver::records()).
    [[nodiscard]] virtual std::vector<GameRecord> records(const std::vector<tour::GameResult>& results) = 0;
    // Participant names, by index.
    [[nodiscard]] virtual const std::vector<std::string>& names() const noexcept     = 0;
    [[nodiscard]] virtual const std::string& name(std::size_t index) const           = 0;
    [[nodiscard]] virtual const std::filesystem::path& fastchessPgn() const noexcept = 0;
    [[nodiscard]] virtual const std::filesystem::path& chessbasePgn() const noexcept = 0;
    [[nodiscard]] virtual const std::string& engineA() const noexcept                = 0;
    [[nodiscard]] virtual const std::string& engineB() const noexcept                = 0;
};

// The search statistics of the moves White's engine and Black's engine played in a game (EngineTotals without the
// processor use and the losses): what is added to their totals when the game is recorded.
[[nodiscard]] std::array<EngineTotals, 2> sideTotals(const fastchess::MatchData& data);

class GuiObserver final : public fastchess::observer::GameObserver, public tour::RecordListener, public LiveSource {
   public:
    // `state` is the match about to be played; the games it already holds (a resumed match) are queued as if
    // they had just been played, read from the PGN on a thread of their own (a big tournament takes seconds), before
    // the games played meanwhile. Without `measure`, the processor use of the engines is not measured here (nor kept
    // in processor.tsv): the tournament's process measures it and tells it (addCpu()).
    GuiObserver(int slots, const tour::TournamentState& state, bool measure = true);
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

    // --- what the tournament's process tells (control.hpp), any thread ---
    // What the engines of slot `i` used while `mover` thought (CpuMeter::moved()), added to their totals.
    void addCpu(int i, chess::Color mover, const CpuMeter::Used& used);
    // The game of slot `i` ended: its result ("*" when it was not counted) and the reason.
    void gameEnded(int i, bool counted, const std::string& result, const std::string& reason);
    // A game recorded (in the tournament's results already), with the search statistics of each side's moves.
    void recorded(const tour::GameResult& result, const std::array<EngineTotals, 2>& side,
                  const std::optional<fastchess::Stats>& pair);
    // Slot `i` as it stands, for a window that connects to a tournament already playing.
    void restoreSlot(int i, const SlotState& state);
    // A move arrived that the board of its slot cannot play (from a tournament in another process: one a snapshot of
    // the slot already holds, or a position missed on a reconnection): left out, and true here once, for the slots to
    // be asked again.
    [[nodiscard]] bool takeResync() { return resync_.exchange(false); }

    // --- GUI thread ---
    [[nodiscard]] int slotCount() const noexcept override { return static_cast<int>(slots_.size()); }
    bool copySlot(int i, SlotState& out) const override;
    [[nodiscard]] std::vector<SlotState> endedGames(int i) const override;
    [[nodiscard]] bool slotActive(int i) const override;
    [[nodiscard]] std::optional<std::array<int, 2>> slotPlayers(int i) const override;

    // Calls `stop` with the process of the engine on move in slot `i`, while it plays game `game_id`
    // (SlotState::game_id) and its engines are started; false otherwise. See RunController::endGame().
    bool stopEngineOnMove(int i, std::size_t game_id, const std::function<void(int64_t pid)>& stop);
    // The numbers (GameTask::number()) of the games played, or ended since `since`, by an engine process among
    // `pids`: those an engine on a machine may have lost when its connection broke. An engine process plays game after
    // game, so the games it ended before are left out by their time.
    [[nodiscard]] std::vector<uint64_t> gamesOfProcesses(const std::vector<int64_t>& pids,
                                                         std::chrono::system_clock::time_point since) const;

    [[nodiscard]] bool loading() const override;
    void drain(std::vector<GameRecord>& games, std::vector<fastchess::Stats>& stats) override;

    [[nodiscard]] std::vector<std::string> reasons() const override;
    [[nodiscard]] std::vector<OpeningName> openings() const override;
    [[nodiscard]] std::vector<EngineTotals> totals() const override;  // [0] = A, [1] = B in a match
    [[nodiscard]] std::vector<std::pair<std::string, EngineTotals>> namedTotals() const override;
    void updateParticipants(const tour::TournamentSpec& spec) override;

    // The games whose PGN was not read yet (a resumed tournament still loading) are left out and come through drain()
    // once read, in their order: the lists end up as a fresh open of the tournament would show them.
    [[nodiscard]] std::vector<GameRecord> records(const std::vector<tour::GameResult>& results) override;

    [[nodiscard]] const std::vector<std::string>& names() const noexcept override { return names_; }
    [[nodiscard]] const std::string& name(std::size_t index) const override;

    [[nodiscard]] const std::filesystem::path& fastchessPgn() const noexcept override { return pgn_fastchess_; }
    [[nodiscard]] const std::filesystem::path& chessbasePgn() const noexcept override { return pgn_chessbase_; }

    [[nodiscard]] const std::string& engineA() const noexcept override { return engine_a_; }
    [[nodiscard]] const std::string& engineB() const noexcept override { return engine_b_; }

   private:
    struct Slot {
        mutable std::mutex mutex;
        SlotState state;
        std::deque<SlotState> ended;             // the previous games, once they have a result; at most 8
        chess::Board board;                      // current position of the game in this slot
        std::array<int, 2> participant{-1, -1};  // the participant indexes of White and Black, for their totals
        std::array<int64_t, 2> pids{};           // the processes of White's and Black's engines, 0 until started
        // Those of the last games ended in it, with their game_id and when they ended: a game lost to a broken
        // connection is over by the time the connection's cut is told.
        struct Ended {
            std::size_t game_id = 0;
            std::array<int64_t, 2> pids{};
            std::chrono::system_clock::time_point at;
        };
        std::deque<Ended> ended_pids;
        std::chrono::system_clock::time_point ended_at;  // of the game in `state`, once it has a result
    };

    Slot* slot(int i) { return i >= 0 && i < static_cast<int>(slots_.size()) ? slots_[i].get() : nullptr; }

    uint16_t internReason(const std::string& reason);
    [[nodiscard]] int participantIndex(const std::string& name) const;  // -1 when unknown
    // What a game's PGN text tells (read from the file when `pgn` is empty), once per game; data_mutex_ held.
    struct GameExtras {
        uint16_t opening = 0;
        EvalFacts facts;
        // The loss counted in totals_ for it (1 on time, 2 by a crash or a stall, 3 by an illegal move, 0 none), and
        // the loser's index: an edit (a result set by hand, a game voided) changes it.
        uint8_t loss   = 0;
        uint16_t loser = 0;
    };
    const GameExtras& extrasOf(const tour::GameResult& result, std::string_view pgn = {});
    // One move of an engine in its search statistics; data_mutex_ held.
    static void addMove(EngineTotals& totals, int64_t depth, int64_t time_ms, int64_t nps, uint64_t nodes,
                        std::optional<int64_t> decision_ms);
    GameRecord makeRecord(const tour::GameResult& result, std::string_view pgn = {});  // data_mutex_ held
    // Counts in totals_ the loss the game is now (none when it is voided), instead of what extras.loss counted;
    // data_mutex_ held.
    void countLoss(const tour::GameResult& result, GameExtras& extras);
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
    bool frc_   = false;  // Chess960: the castling moves read otherwise
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
    std::atomic<bool> resync_{false};  // see takeResync()
    std::thread loader_;  // last: started once everything else is there
};

// What the live view knows of a slot, to choose the slots it shows.
struct SlotSeen {
    bool active   = false;  // a game is being played there
    bool has_game = false;  // it played one (the last stays shown)
    Clock::time_point last_active{};
};
// How long a slot stays shown after its game ended: the thread that played it starts its next game.
inline constexpr std::chrono::seconds LIVE_SLOT_GRACE{10};
// The slots the live view shows, in order: the slots are fastchess' threads, as many as the games played at once, and
// near the end of a tournament or with fewer games than threads most have nothing to play. Those playing, or that
// ended their game less than LIVE_SLOT_GRACE ago, and `followed` if it has a game; with none of them (paused,
// finished), the last that played, within LIVE_SLOT_GRACE of the last one; with no game at all, `followed`.
[[nodiscard]] std::vector<int> shownSlots(const std::vector<SlotSeen>& slots, int followed, Clock::time_point now);

}  // namespace fcg
