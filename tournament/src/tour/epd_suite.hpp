#pragma once

// Test suites: EPD positions with the move to find (bm), or the moves to avoid (am), and for the Strategic Test
// Suite the points of several moves (c0 "Nf3=10, Be2=4"). An engine thinks on each position; it solves it when its
// final move is a solution, and its time to solution is when it settled on that move.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <chess.hpp>

namespace fcg::tour {

struct SuitePosition {
    std::string id;                  // the id operation, or "#N" (1-based line of the position among them)
    std::string fen;                 // with the move counters (hmvc, fmvn or 0 1)
    std::vector<chess::Move> best;   // bm
    std::vector<chess::Move> avoid;  // am
    std::vector<std::pair<chess::Move, int>> points;  // STS c0, the best move first
    std::string best_text;                            // bm and am as written, for display ("Nf3 Qd1", "≠ Bxh7")

    // Whether `move` solves the position: one of bm when there are, and none of am; with points only, the move with
    // the most.
    [[nodiscard]] bool solvedBy(const chess::Move& move) const;
    // STS points of `move`, 0 when it has none.
    [[nodiscard]] int pointsOf(const chess::Move& move) const;
};

struct Suite {
    std::vector<SuitePosition> positions;
    int skipped = 0;  // lines without a legal position or without bm, am or c0
    [[nodiscard]] bool hasPoints() const;
    [[nodiscard]] int maxPoints() const;  // the best move's points, summed
};

// Reads an EPD suite. Throws std::runtime_error when the file cannot be read.
[[nodiscard]] Suite readSuite(const std::filesystem::path& file);

// Parses one EPD line; false when it is not a usable position.
[[nodiscard]] bool parseSuiteLine(std::string_view line, SuitePosition& out);

// What an engine did on a position.
struct SuiteAnswer {
    bool solved = false;
    std::string move;      // its move, SAN
    int64_t time_ms = -1;  // time to solution when solved, its whole search otherwise
    int depth       = 0;   // its depth at the end
    int points      = 0;   // STS
};

// Follows the info lines of one search. `hold` > 0: the search can stop once a solution has stayed the first PV
// move for `hold` more plies of depth.
class SuiteSearch {
   public:
    SuiteSearch(const SuitePosition& position, int hold);

    // An info line; true when the engine may be stopped.
    bool info(std::string_view line, int64_t elapsed_ms);
    // The engine's final move (UCI) after `elapsed_ms`.
    [[nodiscard]] SuiteAnswer finish(std::string_view bestmove, int64_t elapsed_ms) const;

   private:
    const SuitePosition& position_;
    chess::Board board_;
    int hold_ = 0;

    std::string first_;      // the current first PV move
    int64_t found_ms_ = -1;  // when the current first move, a solution, became so; -1 when it is none
    int found_depth_  = 0;
    int depth_        = 0;
};

}  // namespace fcg::tour
