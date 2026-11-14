#pragma once

// The state of the Board page (view_board.cpp) and of the engine console of the Engines page
// (view_engine_console.cpp): what they show, apart from the App that draws them.

#include <optional>
#include <string>
#include <vector>

#include <chess.hpp>
#include <json.hpp>
#include <types/score.hpp>

#include "analysis.hpp"
#include "engine_client.hpp"
#include "pgn_store.hpp"

namespace fcg {

// The console of an engine of the library: the lines exchanged with it, typed by the user.
struct EngineConsole {
    std::string engine;  // the library engine, by name
    bool raw = false;    // a WinBoard engine spoken to in its own protocol, without the adapter
    EngineProcess process;
    std::vector<EngineProcess::Line> log;
    std::string input;
    std::vector<std::string> history;
    int history_pos = -1;  // browsing the history with Up and Down; -1: the line being typed
    bool follow     = true;
    bool focus      = true;  // the input line takes the keyboard on the next frame
    bool shown      = true;  // in the engine's card, instead of its settings
    // UCI: the library's option values are sent once the engine answers uciok.
    std::vector<std::pair<std::string, std::string>> options;
    bool options_due = false;
};

// The quick commands of the console, as typed: UCI's, or WinBoard's when spoken to in its own protocol.
[[nodiscard]] const std::vector<const char*>& consoleCommands(bool winboard);

// What the Board page keeps between two launches (board.json in the profile): the analysis's own settings.
struct BoardSettings {
    AnalysisSettings analysis;
    bool arrows = true;  // the first move of each line drawn on the board
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BoardSettings, analysis, arrows)

// The position after the first `ply` moves of `game` (its start position for 0). The moves were checked when the game
// was read or played.
[[nodiscard]] chess::Board boardAt(const ReplayGame& game, bool chess960, int ply);

// The same, for the analysis engine: the start position and the moves to it.
[[nodiscard]] AnalysisPosition analysisPosition(const ReplayGame& game, bool chess960, int ply);

// A score from the side to move's point of view, from White's.
[[nodiscard]] fastchess::Score whiteScore(const fastchess::Score& score, chess::Color side_to_move);

// Plays `move` on the game after its first `ply` moves: the game's next move when it is that one, otherwise the moves
// after `ply` give way to it. Returns the new ply.
int playOnGame(ReplayGame& game, bool chess960, int ply, chess::Move move);

// The legal moves of the piece on `from`, the target squares as the board shows them (castling on the king's square).
[[nodiscard]] std::vector<chess::Move> movesFrom(const chess::Board& board, chess::Square from);

// The square a move goes to on the board: castling, stored as the king taking its rook, goes to the king's square.
[[nodiscard]] chess::Square shownTarget(const chess::Move& move);

// The game and the position of the Board page, and its analysis.
struct BoardPage {
    ReplayGame game;
    bool chess960 = false;
    std::string title;  // what the game is ("Alpha – Charlie · Round robin · round 12"), empty for the user's
    std::optional<ReplayGame> original;  // the game as it came, before moves were played over it
    int ply      = 0;                    // moves played on the board
    int followed = -1;                   // the ply the move list was last scrolled to
    bool flip    = false;
    std::optional<chess::Square> selected;  // a piece picked up
    std::optional<chess::Move> promotion;   // a pawn's move waiting for the piece it becomes
    std::string message;                    // what the last action says (no engine to analyse with)
    bool message_error = false;

    BoardSettings settings;
    bool settings_loaded = false;
    Analyser analyser;
    bool analysing = false;  // asked for: the analysis follows the board while the page is shown
    bool new_game  = true;   // another game came: the engine gets ucinewgame
};

}  // namespace fcg
