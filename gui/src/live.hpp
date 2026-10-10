#pragma once

// GuiObserver receives the tournament callbacks on the worker threads and turns them into
// state the GUI thread can copy cheaply: one locked record per slot, plus queues of finished
// games and statistics that the GUI drains once per frame.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
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
    uint64_t game_id     = 0;
    uint32_t round_id    = 0;
    bool a_white         = true;
    int8_t a_result      = 0;  // +1 A won, 0 draw, -1 A lost
    uint8_t termination  = 0;
    uint16_t reason      = 0;  // index into GuiObserver::reasons()
    uint16_t plies       = 0;
    uint32_t duration_ms = 0;
    PgnSpan fastchess_span;
    PgnSpan chessbase_span;
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

class GuiObserver final : public fastchess::observer::GameObserver {
   public:
    GuiObserver(int slots, std::string engine_a, std::string engine_b, const std::filesystem::path& run_dir);

    // --- callbacks, tournament threads ---
    void onGameStart(const fastchess::observer::GameStart& info) override;
    void onThinking(int slot, chess::Color stm, int64_t time_left_ms) override;
    void onEngineLine(int slot, chess::Color side, std::string_view line) override;
    void onMove(int slot, chess::Color mover, const fastchess::MoveData& move, const std::string& san,
                int64_t white_time_left, int64_t black_time_left) override;
    void onGameEnd(int slot, std::size_t game_id, std::size_t round_id, const fastchess::MatchData& data,
                   bool counted) override;
    void onResult(const fastchess::Stats& h2h, const std::string& first, const std::string& second) override;

    // --- GUI thread ---
    [[nodiscard]] int slotCount() const noexcept { return static_cast<int>(slots_.size()); }

    // Copies slot `i` into `out` if it changed since `out.version`. Returns true if it did.
    bool copySlot(int i, SlotState& out) const;

    // Is anything going on in slot `i` (cheap, for tab labels)?
    [[nodiscard]] bool slotActive(int i) const;

    // Moves the games and statistics that arrived since the last call into the given vectors.
    void drain(std::vector<GameRecord>& games, std::vector<fastchess::Stats>& stats);

    [[nodiscard]] std::vector<std::string> reasons() const;
    [[nodiscard]] std::array<EngineTotals, 2> totals() const;  // [0] = A, [1] = B

    [[nodiscard]] const PgnFile& fastchessPgn() const noexcept { return pgn_fastchess_; }
    [[nodiscard]] const PgnFile& chessbasePgn() const noexcept { return pgn_chessbase_; }

    [[nodiscard]] const std::string& engineA() const noexcept { return engine_a_; }
    [[nodiscard]] const std::string& engineB() const noexcept { return engine_b_; }

   private:
    struct Slot {
        mutable std::mutex mutex;
        SlotState state;
        chess::Board board;  // current position of the game in this slot
    };

    Slot* slot(int i) { return i >= 0 && i < static_cast<int>(slots_.size()) ? slots_[i].get() : nullptr; }

    uint16_t internReason(const std::string& reason);

    std::string engine_a_;
    std::string engine_b_;

    std::vector<std::unique_ptr<Slot>> slots_;

    PgnFile pgn_fastchess_;
    PgnFile pgn_chessbase_;

    mutable std::mutex data_mutex_;
    std::vector<GameRecord> pending_games_;
    std::vector<fastchess::Stats> pending_stats_;
    std::vector<std::string> reasons_;
    std::unordered_map<std::string, uint16_t> reason_index_;
    std::array<EngineTotals, 2> totals_{};
};

}  // namespace fcg
