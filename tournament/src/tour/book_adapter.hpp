#pragma once

// A participant's own opening book, without changing fastchess: the engine is started through an adapter,
// `fastchess-gui --book-adapter --book FILE --depth N --seed S -- ENGINE [ARGS...]`, that speaks UCI to both sides
// and passes everything on, except that it answers a go itself, at once, with a move of the book while the game is
// in it (within the depth): the engine never searches a book move. The tournament's opening is played first, as
// for every engine; the book takes over from there.
//
// The book move is chosen at random in proportion to the book's weights, as Cute Chess does. Each game draws its
// own choices: an adapter process mixes a random number of its own into the seed (the tournament's), so that the
// processes of an engine (one per concurrent game, a new one after each restart, crash or resume, or for every game
// when the engine is restarted for each) do not replay the same lines. The adapter cannot know a game's number in
// the tournament, so the choices are not reproducible, even with the tournament's seed set.

#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <chess.hpp>

#include <types/engine_config.hpp>
#include <types/match_data.hpp>

#include "polyglot.hpp"

namespace fcg::tour {

// The program that runs the adapter (and the WinBoard one): fastchess-gui itself, set at its start. Empty, a
// participant with a book of its own cannot play.
inline std::string book_adapter;

// The configuration that starts `engine` through the adapter with `book`: the same name, options and limits, the
// program and its arguments those of the adapter. A WinBoard engine goes on through the WinBoard adapter, behind
// the book adapter. Throws std::runtime_error when book_adapter is not set.
[[nodiscard]] fastchess::EngineConfiguration throughBookAdapter(fastchess::EngineConfiguration engine,
                                                                const std::string& book, int depth, uint64_t seed);

// Marks as book moves (PGN comment "book") the moves of `side` that its adapter played from `book`: those of the
// positions the book has a move for, within `depth`. The adapter answers exactly these, so no other trace is needed.
void markOwnBookMoves(fastchess::MatchData& data, chess::Color side, PolyglotBook& book, int depth, bool chess960);

// The translation, without processes: lines in, lines out, so that it can be tested.
class BookAdapter {
   public:
    using Sink = std::function<void(const std::string&)>;

    // `chess960`: castling moves are written as the king taking its rook (also set by UCI_Chess960). `seed` is used
    // as given, so that the tests get the same choices from the same seed; runBookAdapter() mixes its random number
    // into it first.
    BookAdapter(Sink to_gui, Sink to_engine, std::unique_ptr<PolyglotBook> book, int depth, uint64_t seed,
                bool chess960 = false);

    // A UCI command from the GUI.
    void fromGui(const std::string& line);

    // A line of the engine's output.
    void fromEngine(const std::string& line);

    [[nodiscard]] bool quitting() const noexcept { return quitting_; }

   private:
    void position(const std::vector<std::string>& tokens);
    [[nodiscard]] bool answerFromBook(const std::vector<std::string>& tokens);

    Sink to_gui_;
    Sink to_engine_;
    std::unique_ptr<PolyglotBook> book_;
    int depth_     = 0;
    uint64_t seed_ = 0;

    bool chess960_ = false;
    chess::Board board_;
    std::size_t ply_ = 0;      // moves played since the position the game started from
    bool known_      = false;  // the last position could be followed: a book move may answer it
    uint64_t games_  = 0;      // ucinewgame received
    bool reseed_     = true;   // at the next position: a new game, a new generator
    std::mt19937_64 rng_;
    bool searching_ = false;  // the engine has a go it has not answered yet
    bool quitting_  = false;
};

// The --book-adapter mode: `args` as above, after "--book-adapter". Returns the exit code.
int runBookAdapter(const std::vector<std::string>& args);

}  // namespace fcg::tour
