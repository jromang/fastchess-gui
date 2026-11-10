#pragma once

// GuiObserver receives the tournament callbacks on the worker threads and turns them into
// state the GUI thread can copy cheaply: one locked record per slot, plus queues of finished
// games and statistics that the GUI drains once per frame. The games themselves are recorded
// (PGN files, journal) by the tournament layer, which tells the observer about each one.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
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

    std::optional<fastchess::Score> score;  // mover's point of view
    int64_t depth    = 0;
    int64_t seldepth = 0;
    int64_t time_ms  = 0;
    uint64_t nodes   = 0;
    uint64_t nps     = 0;
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
    uint64_t moves       = 0;
    uint64_t depth_sum   = 0;
    double nps_sum       = 0;
    uint64_t nps_samples = 0;
    uint64_t time_ms     = 0;
    int timeouts         = 0;
    int crashes          = 0;
    int illegal          = 0;
};

class GuiObserver final : public fastchess::observer::GameObserver, public tour::RecordListener {
   public:
    // `state` is the match about to be played; the games it already holds (a resumed match) are queued as if
    // they had just been played, except for the search statistics, which are not kept.
    GuiObserver(int slots, const tour::TournamentState& state);

    // --- callbacks, tournament threads ---
    void onGameStart(const fastchess::observer::GameStart& info) override;
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

    // Moves the games and statistics that arrived since the last call into the given vectors.
    void drain(std::vector<GameRecord>& games, std::vector<fastchess::Stats>& stats);

    [[nodiscard]] std::vector<std::string> reasons() const;
    [[nodiscard]] std::vector<OpeningName> openings() const;  // [0] is "no opening"
    [[nodiscard]] std::vector<EngineTotals> totals() const;   // by participant index ([0] = A, [1] = B in a match)

    // New participants (added or replacing one) get a name and counters; existing indexes do not change.
    void updateParticipants(const tour::TournamentSpec& spec);

    // The records of the games that count, for the views to start over after an edit.
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
        std::deque<SlotState> ended;  // the previous games, once they have a result; at most 8
        chess::Board board;           // current position of the game in this slot
    };

    Slot* slot(int i) { return i >= 0 && i < static_cast<int>(slots_.size()) ? slots_[i].get() : nullptr; }

    uint16_t internReason(const std::string& reason);
    // What a game's PGN text tells (read from the file when `pgn` is empty), once per game; data_mutex_ held.
    struct GameExtras {
        uint16_t opening = 0;
        EvalFacts facts;
    };
    const GameExtras& extrasOf(const tour::GameResult& result, std::string_view pgn = {});
    // One move of an engine in its search statistics; data_mutex_ held.
    static void addMove(EngineTotals& totals, int64_t depth, int64_t time_ms, int64_t nps);
    GameRecord makeRecord(const tour::GameResult& result, std::string_view pgn = {});  // data_mutex_ held
    void countLoss(const tour::GameResult& result);                                    // data_mutex_ held

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

    mutable std::mutex data_mutex_;
    std::vector<GameRecord> pending_games_;
    std::vector<fastchess::Stats> pending_stats_;
    std::vector<std::string> reasons_;
    std::unordered_map<std::string, uint16_t> reason_index_;
    std::vector<OpeningName> openings_{1};
    std::unordered_map<std::string, uint16_t> opening_index_;  // "eco|name" -> index
    std::unordered_map<uint64_t, GameExtras> extras_;          // by the game's PGN offset
    std::vector<EngineTotals> totals_;
};

}  // namespace fcg
