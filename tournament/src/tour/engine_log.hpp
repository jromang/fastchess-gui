#pragma once

// The lines exchanged with the two engines of a game, kept in memory while it is played and written to the
// tournament folder when the game ends badly (a crash, a loss on time, an illegal move, a stall), or for every
// game when the tournament asks for it. It is what an engine author needs to reproduce a bug.

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <chess.hpp>

#include <types/match_data.hpp>

namespace fcg::tour {

class EngineLog {
   public:
    // The first lines (engine start, options, the first position) and the last ones are kept; the middle of a
    // very long game is dropped.
    static constexpr std::size_t HEAD_LINES = 300;
    static constexpr std::size_t TAIL_LINES = 5000;

    // Forgets the previous game.
    void start();

    // `sent`: a line written to the engine, otherwise one read from it. Thread safe.
    void add(chess::Color side, bool sent, std::string_view line);

    // The whole log: a header describing the game, then one line per exchange, as
    // "   12.345 W> go wtime 1000 ..." (sent to White's engine) or "   12.346 B< bestmove e7e5" (read from Black's).
    [[nodiscard]] std::string text(uint64_t game_number, const fastchess::MatchData& data) const;

   private:
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    std::vector<std::string> head_;
    std::deque<std::string> tail_;
    uint64_t dropped_ = 0;
};

// Whether a game ending this way keeps its engine log.
[[nodiscard]] bool endsBadly(fastchess::MatchTermination termination) noexcept;

// logs/game-N.log in the tournament folder, N being the game number shown to the user (1-based).
[[nodiscard]] std::filesystem::path engineLogPath(const std::filesystem::path& tournament_dir, uint64_t game_number);

}  // namespace fcg::tour
