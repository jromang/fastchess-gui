#pragma once

#include <string>
#include <string_view>

#include <chess.hpp>

#include <matchmaking/match/match.hpp>

#include <types/tournament.hpp>

#include <game/pgn/openings_data.hpp>
#include <game/pgn/pgn_gen.hpp>

#include <fmt/include/fmt/core.h>

namespace fastchess::pgn {

// How engine data is written in the move comments.
//  Fastchess: {+0.35/18 1.234s, ...}  (score from the mover's point of view, the cutechess layout)
//  ChessBase: {[%eval 0.35,18] [%clk 0:00:59.200] [%emt 0:00:01.234]}  (eval from White's point of view)
enum class CommentStyle { Fastchess, ChessBase };

class PgnBuilder {
   public:
    PgnBuilder(const config::Pgn& pgn_config, const MatchData& match, std::size_t round_id,
               CommentStyle style = CommentStyle::Fastchess);

    // Get the newly created pgn
    [[nodiscard]] std::string get() const noexcept { return fmt::format("{}\n\n", pgn_generator_.generate()); }

    static constexpr int LINE_LENGTH = 80;

    [[nodiscard]] static std::string convertMatchTermination(const MatchTermination& res) noexcept;

    [[nodiscard]] static std::string getResultFromWhiteMatch(const MatchData::PlayerInfo& white) noexcept;

   private:
    // Converts a UCI move to either SAN, LAN or keeps it as UCI
    [[nodiscard]] std::string moveNotation(chess::Board& board, const std::string& move) const noexcept;

    std::string createComment(const MoveData& move, const MoveData& next_move, bool illegal, bool last,
                              chess::Color mover) noexcept;

    // The [%eval ...] [%clk ...] [%emt ...] part of a ChessBase style comment.
    [[nodiscard]] std::string chessBaseCommands(const MoveData& move, chess::Color mover) const;

    [[nodiscard]] static std::string formatClock(int64_t millis);

    std::optional<Opening> getOpeningClassification(bool is_frc_variant) const;

    // Adds a comment to the pgn. The comment is formatted as {first, args}
    template <typename First, typename... Args>
    [[nodiscard]] static std::string addComment(First&& first, Args&&... args) {
        std::string result = fmt::format("{}", std::forward<First>(first));
        ((result += std::string(args).empty() ? "" : fmt::format(", {}", std::forward<Args>(args))), ...);
        return result;
    }

    // Formats a time in milliseconds to seconds with 3 decimals
    [[nodiscard]] static std::string formatTime(int64_t millis) { return fmt::format("{:.3f}s", millis / 1000.0); }

    const config::Pgn& pgn_config_;
    const MatchData& match_;
    CommentStyle style_;

    PGNGenerator pgn_generator_;
};

}  // namespace fastchess::pgn
