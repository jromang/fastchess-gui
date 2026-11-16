#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <chess.hpp>
#include <types/score.hpp>

#include <tour/pgn_file.hpp>

namespace fcg {

// The PGN files are written by the tournament layer.
using tour::PgnSpan;

// Copies the given games, in order, into a new file. Returns an error message, empty on success. `target` may not
// be `source` nor one of `own` (the tournament's other PGN file): writing it would destroy the games. `counted`, when
// given, makes the text of game i what is written (a result set by hand, see tour::countedPgn()).
std::string exportSpans(const std::filesystem::path& source, const std::vector<PgnSpan>& spans,
                        const std::filesystem::path& target, const std::vector<std::filesystem::path>& own = {},
                        const std::function<std::string(std::size_t i, std::string text)>& counted = {});

// The optional fields of a move comment, as the PGN options of the setup turn them on: a set of these bits.
enum PgnField : uint8_t {
    PGN_NODES    = 1 << 0,  // n=
    PGN_SELDEPTH = 1 << 1,  // sd=
    PGN_NPS      = 1 << 2,  // nps=
    PGN_TBHITS   = 1 << 3,  // tbhits=
    PGN_PV       = 1 << 4,  // pv="..."
    PGN_DECISION = 1 << 5,  // dt=
};

// A game read back from a PGN written in the fastchess comment style.
struct ReplayMove {
    std::string san;
    chess::Move move;
    chess::Color mover = chess::Color::WHITE;
    bool book          = false;

    std::optional<fastchess::Score> score;  // from the mover's point of view
    int depth       = 0;
    int64_t time_ms = -1;
    int64_t nps     = 0;                        // only when the PGN was written with nps=
    std::optional<std::array<int64_t, 3>> wdl;  // wdl=W/D/L: permille, from the mover's point of view
    // Only when the PGN was written with them: n=, sd=, tbhits=, pv="...", dt=.
    uint64_t nodes   = 0;
    int64_t seldepth = 0;
    uint64_t tbhits  = 0;
    std::string pv;  // UCI moves, from the move played
    std::optional<int64_t> decision_ms;
    uint8_t fields = 0;  // the PgnField found in its comment, a zero told apart from a field not written
};

struct ReplayGame {
    std::vector<std::pair<std::string, std::string>> headers;
    std::string start_fen;
    std::vector<ReplayMove> moves;
    std::string result;
    std::string final_comment;
    uint8_t fields = 0;  // the PgnField of any of its moves: those the tournament wrote

    [[nodiscard]] std::string header(const std::string& key) const;
};

// Moves played where the game, or another variation, went another way: the moves from that position on.
struct Variation {
    int parent = -1;  // the line it branches from: -1 the game, else a variation
    int from   = 0;   // the position of that line it starts from: its first move instead of that line's move from+1
    std::vector<ReplayMove> moves;
};

// A game and its variations.
struct GameTree {
    ReplayGame main;
    std::vector<Variation> variations;
};

[[nodiscard]] std::optional<ReplayGame> parseReplay(const std::string& pgn);

// One game's PGN with its moves on one line (its tags as they are, its escape lines left out): chess.hpp's reader
// loses a move's comment, and every move after it, when an end of line comes between the move and the comment, as
// in the PGN files of programs that cut their lines anywhere.
[[nodiscard]] std::string flattenMovetext(const std::string& pgn);

// What the evaluations of a game (fastchess comment style) tell, in pawns from the engines' own scores, a mate
// counting as 10: how far the two engines disagreed for 3 moves each in a row, the largest change of one engine's
// evaluation between two of its moves, and the best evaluation the loser had, from its own point of view. Each
// with the ply it happens at (0-based, -1 when it does not), to show it.
struct EvalFacts {
    float disagreement   = 0;
    int16_t disagree_ply = -1;
    float swing          = 0;
    int16_t swing_ply    = -1;
    float loser_best     = 0;
    int16_t loser_ply    = -1;
};
// `white_result`: +1 White won, 0 a draw, -1 Black won.
[[nodiscard]] EvalFacts evalFacts(std::string_view pgn, int white_result);

// The value of a tag of the game's header, empty when it has none. Reads only the header.
[[nodiscard]] std::string pgnTag(std::string_view pgn, std::string_view tag);

// Whether an opening matches what was typed in the Games filter: an ECO code or its beginning ("B9"), a range
// ("B20-B99"), or words of the name in any order, ignoring case. An empty query matches everything.
[[nodiscard]] bool openingMatches(const std::string& eco, const std::string& name, const std::string& query);

// "{+0.35/18 1.234s, ...}" (the body without braces) into score, depth, time and nps.
void parseFastchessComment(std::string_view comment, ReplayMove& move);

// The "[%eval 0.17]" or "[%eval #-3]" of a comment (lichess, ChessBase: White's point of view, in pawns or a mate
// in moves) into the move's score, from its mover's point of view; nothing when the comment has none. The mover must
// be set.
void parseEvalTag(std::string_view comment, ReplayMove& move);

// Calls f(white_moved, move) for every move of a game in the fastchess comment style, from the comments alone:
// the moves are not replayed, so it is fast enough for every game of a big tournament.
void forEachMoveComment(std::string_view pgn, const std::function<void(bool, const ReplayMove&)>& f);

}  // namespace fcg
