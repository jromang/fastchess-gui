#pragma once

// The state of the Board page (view_board.cpp) and of the engine console of the Engines page
// (view_engine_console.cpp): what they show, apart from the App that draws them.

#include <array>
#include <atomic>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <chess.hpp>
#include <json.hpp>
#include <types/score.hpp>

#include <tour/pgn_index.hpp>

#include "analysis.hpp"
#include "board_game.hpp"
#include "engine_client.hpp"
#include "game_analysis.hpp"
#include "game_graph.hpp"
#include "pgn_store.hpp"
#include "review_runner.hpp"

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
    // The analysis of a whole game, refined pass after pass up to this time per position; 0: until stopped.
    int64_t review_ceiling_ms = 5120;
    // A game played from the board: the two engines, their clocks, and a second game with the colours reversed.
    std::string game_first, game_second;
    GameClock first_clock, second_clock;
    bool same_clock = true;
    bool game_pair  = false;
    GraphView graph;  // the game graph's tab, hidden curves, folded
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BoardSettings, analysis, arrows, review_ceiling_ms, game_first,
                                                game_second, first_clock, second_clock, same_clock, game_pair, graph)

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

// The moves of a line from the start: the game's (-1), or a variation's after those of its parents up to where it
// starts. Its tags are the game's.
[[nodiscard]] ReplayGame lineOf(const GameTree& tree, int line);
// The line that holds the move after position `ply` of `line`: the line itself, or a parent when `ply` is before the
// line starts.
[[nodiscard]] int ownerOf(const GameTree& tree, int line, int ply);
// The position of the game that a line leaves it at (its own start, or its first parent's); the line's ply for the
// game itself.
[[nodiscard]] int branchPly(const GameTree& tree, int line, int ply);
// Plays `move` after position `ply` of `line`: the line's next move when it is that one, a variation from there that
// starts with it, else a new variation; at the end of a line, the line grows. The line and ply shown after.
std::pair<int, int> playInTree(GameTree& tree, bool chess960, int line, int ply, chess::Move move);
// Removes a variation and those branching from it. Returns its parent, as numbered after the removal.
int removeVariation(GameTree& tree, int line);
// The variation becomes the line it branches from, whose moves after it become the variation: those branching from
// either follow their moves. Returns that line.
int promoteVariation(GameTree& tree, int line);

// The legal moves of the piece on `from`, the target squares as the board shows them (castling on the king's square).
[[nodiscard]] std::vector<chess::Move> movesFrom(const chess::Board& board, chess::Square from);

// The square a move goes to on the board: castling, stored as the king taking its rook, goes to the king's square.
[[nodiscard]] chess::Square shownTarget(const chess::Move& move);

// A PGN of several games, opened or pasted: its games listed, filtered by their tags, one shown at a time.
struct PgnSource {
    std::string name;                         // the file's, or "clipboard"
    std::filesystem::path file;               // empty for a pasted text
    std::shared_ptr<const std::string> text;  // the pasted text
    std::shared_ptr<const tour::PgnIndex> index;
    std::string filter;
    std::vector<uint32_t> shown;  // the games the filter keeps
    bool filter_dirty = true;
    int selected      = -1;  // the game on the board
};

// The text of one game of a source.
[[nodiscard]] std::optional<std::string> pgnSourceGame(const PgnSource& source, std::size_t game);

// A PGN file being read in the background.
struct PgnReading {
    std::filesystem::path file;
    uint64_t size = 0;
    std::future<tour::PgnIndex> result;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::shared_ptr<std::atomic<uint64_t>> done;
};

// The position editor: a placement of pieces, any while it is edited, and the rest of a FEN.
struct PositionEditor {
    std::array<chess::Piece, 64> squares{};  // a1 = 0 ... h8 = 63
    chess::Color stm   = chess::Color::WHITE;
    bool castle[4]     = {true, true, true, true};  // K Q k q
    int ep_file        = -1;                        // the en passant square's file, -1 none
    int fullmove       = 1;
    bool chess960      = false;
    chess::Piece brush = chess::Piece(chess::PieceType::QUEEN, chess::Color::WHITE);
    bool erase         = false;
    std::string fen_text;  // as typed in its field
};

// The editor's position as a FEN: the castling rights its kings and rooks allow, the en passant square only when a
// pawn just moved two squares there.
[[nodiscard]] std::string editorFen(const PositionEditor& editor);
// The editor set to a FEN's position; false (unchanged) when the board cannot read it.
bool editorLoad(PositionEditor& editor, const std::string& fen);
// Whether the castling right (0 K, 1 Q, 2 k, 3 q) is possible: the king, and a rook on that side, on their first
// rank (where they start in Chess960; on e1 and h1 or a1 otherwise).
[[nodiscard]] bool editorCanCastle(const PositionEditor& editor, int right);

// The game and the position of the Board page, and its analysis.
// A line laid on the board without touching the game (Show, a line hovered): the position `ply` of the game, then
// the line's moves.
struct BoardPreview {
    int ply = 0;
    int at  = 0;                     // the board's ply when it was shown: moving through the game ends it
    std::vector<std::string> moves;  // UCI
    std::string label;               // what it is, over the board
    bool hover = false;              // shown while the mouse is on it: gone when it leaves
};

struct BoardPage {
    ReplayGame game;
    bool chess960 = false;
    std::string title;  // what the game is ("Alpha – Charlie · Round robin · round 12"), empty for the user's
    // The game with the variations played on the board; `game` is the line shown (the game itself for line -1).
    GameTree tree;
    int line          = -1;
    int menu_line     = -1;                   // the variation a menu is open for
    int ply      = 0;                    // moves played on the board
    int followed      = -1;                   // the ply the move list was last scrolled to
    int followed_line = -1;                   // and its line
    bool flip    = false;
    std::optional<chess::Square> selected;  // a piece picked up
    std::optional<chess::Move> promotion;   // a pawn's move waiting for the piece it becomes
    std::optional<BoardPreview> preview;    // a line shown on the board instead of the game
    bool preview_hovered = false;           // a hovered line asked for it this frame
    std::string message;                    // what the last paste or file gave, or why it gave nothing
    bool message_error = false;

    BoardSettings settings;
    bool settings_loaded = false;
    Analyser analyser;
    bool analysing = false;  // asked for: the analysis follows the board while the page is shown
    Analyser threat;         // the opponent's best move were it to move again (X), on an engine of its own
    bool threat_on = false;
    bool new_game  = true;   // another game came: the engine gets ucinewgame

    std::optional<PgnSource> pgn;          // the games of a file or a paste
    std::optional<PgnReading> reading;     // a file being read
    std::optional<PositionEditor> editor;  // the position being set up
    int game_tab = 0;                      // the game card: 0 its moves, 1 the games of the file
    int open_tab = -1;                     // a tab to select on the next frame

    // The game analysed in passes (the continuous analysis), by one engine or several, each on a thread of its own.
    struct ReviewTask {
        std::string engine;
        std::unique_ptr<ReviewRunner> runner;
        ReviewRunner::State state;  // copied when it changes
        uint64_t version = 0;
        int saved        = 0;  // its passes kept in the profile
    };
    std::vector<ReviewTask> review_tasks;
    ReplayGame review_game;  // the game they analyse
    bool review_chess960 = false;
    std::string review_game_key;  // reviewKey(review_game)
    int64_t review_offered = 0;   // the time of the board's search last offered to them
    [[nodiscard]] bool reviewing() const { return !review_tasks.empty(); }
    std::vector<GameReview> reviews;  // the analyses of the game shown, the judging one first
    std::string review_key;           // the game they are of (reviewKey)
    std::vector<MoveReview> judged;   // its moves judged by the first, for the game shown

    // Games played from the board's position between two engines.
    bool play_open   = false;  // their settings shown
    bool play_anyway = false;  // asked to play on a machine the tournament and the analysis fill
    std::unique_ptr<BoardMatch> match;
    BoardMatch::State match_state;
    uint64_t match_version = 0;
    bool watching          = false;  // the board shows the game being played
};

}  // namespace fcg
