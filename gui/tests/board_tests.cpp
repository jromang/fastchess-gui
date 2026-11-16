// The engine client of the console and the Board page, against the mock engine; the analysis; the Board page's
// texts (FEN, PGN, move lists) and the analysis of a game.

#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

#include <engine/uci_engine.hpp>

#include "../../tournament/tests/self_path.hpp"
#include "analysis.hpp"
#include "board_page.hpp"
#include "board_text.hpp"
#include "engine_client.hpp"
#include "run.hpp"

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

std::string randomMover() { return (fcg::test::selfPath().parent_path() / FCG_RANDOM_MOVER).string(); }

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

// Sets a variable of the environment for as long as it lives.
struct ScopedEnv {
    const char* name;
    ScopedEnv(const char* n, const char* value) : name(n) { setEnv(n, value); }
    ~ScopedEnv() { setEnv(name, nullptr); }
};

EngineEntry mover() {
    EngineEntry e;
    e.name     = "mover";
    e.cmd      = randomMover();
    e.protocol = "uci";
    return e;
}

// Polls the analyser until `done` or 10 seconds.
template <typename F>
bool pollUntil(Analyser& a, F done) {
    const auto until = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < until) {
        a.poll();
        if (done()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

}  // namespace

TEST_CASE("engine client: a process of its own, off fastchess' list, that answers and ends") {
    if (!fs::is_regular_file(randomMover())) return;
    const int before = engineProcesses();

    EngineProcess p;
    std::string error;
    REQUIRE(p.start(engineLaunch(mover()), error));
    CHECK(p.running());
    // A tournament's end ends every process on fastchess' list: this one is not there.
    CHECK(engineProcesses() == before);

    CHECK(p.send("uci"));
    std::vector<EngineProcess::Line> lines;
    const auto until = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < until) {
        p.wait(100ms);
        for (auto& l : p.take()) lines.push_back(std::move(l));
        if (!lines.empty() && lines.back().text == "uciok") break;
    }
    REQUIRE(!lines.empty());
    INFO(consoleTranscript(lines));  // all of it, should a check fail
    // The engine's banner ("Using random seed", written unasked) may come before "uci" was sent, on a slow machine.
    const auto sent = std::find_if(lines.begin(), lines.end(), [](const auto& l) {
        return l.kind == EngineProcess::Line::Kind::Sent && l.text == "uci";
    });
    REQUIRE(sent != lines.end());
    CHECK(std::all_of(lines.begin(), sent, [](const auto& l) { return l.kind != EngineProcess::Line::Kind::Sent; }));
    CHECK(lines.back().text == "uciok");
    CHECK(std::any_of(lines.begin(), lines.end(), [](const auto& l) { return l.text == "id name random_move"; }));

    // The transcript marks what was sent.
    const auto text = consoleTranscript(lines);
    CHECK(text.find(" > uci\n") != std::string::npos);
    CHECK(text.find("   uciok\n") != std::string::npos);

    p.stop();
    CHECK_FALSE(p.running());
    CHECK_FALSE(p.send("isready"));

    // A program that is not there.
    auto missing = mover();
    missing.cmd  = (fs::temp_directory_path() / "no-such-engine-fcg").string();
    CHECK_FALSE(p.start(engineLaunch(missing), error));
    CHECK(error.find("no-such-engine-fcg") != std::string::npos);
}

TEST_CASE("engine client: a WinBoard engine goes through the adapter unless spoken to in its own protocol") {
    const std::string adapter         = fastchess::engine::xboard_adapter;
    fastchess::engine::xboard_adapter = "/opt/fcg/fastchess-gui";
    EngineEntry e;
    e.name = "wb", e.cmd = "/engines/crafty", e.args = "xboard", e.protocol = "xboard", e.dir = "/engines";
    const auto through = engineLaunch(e);
    CHECK(through.program == "/opt/fcg/fastchess-gui");
    CHECK(through.args == "--wb2uci \"/engines/crafty\" xboard");
    CHECK(through.dir == "/engines");
    const auto raw = engineLaunch(e, true);
    CHECK(raw.program == "/engines/crafty");
    CHECK(raw.args == "xboard");
    fastchess::engine::xboard_adapter = adapter;
}

TEST_CASE("engine client: the console's history and the core claims") {
    std::vector<std::string> history;
    rememberCommand(history, "uci");
    rememberCommand(history, "uci");
    rememberCommand(history, "");
    rememberCommand(history, "isready");
    CHECK(history == std::vector<std::string>{"uci", "isready"});
    for (int i = 0; i < 300; ++i) rememberCommand(history, "go depth " + std::to_string(i));
    CHECK(history.size() == 200);
    CHECK(history.back() == "go depth 299");

    const int before = boardThreads();
    {
        CoreClaim a(4);
        CoreClaim b;
        b.set(2);
        CHECK(boardThreads() == before + 6);
        a.set(1);
        CHECK(boardThreads() == before + 3);
    }
    CHECK(boardThreads() == before);
}

TEST_CASE("analysis: info lines into MultiPV lines") {
    std::vector<AnalysisLine> lines;
    CHECK(takeInfoLine(lines,
                       "info depth 12 seldepth 18 multipv 1 score cp 35 wdl 300 600 100 nodes 12345 nps 900000 "
                       "time 14 pv e2e4 e7e5 g1f3",
                       3));
    CHECK(takeInfoLine(lines, "info depth 12 multipv 3 score mate -4 nodes 12400 pv a2a3", 3));
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].depth == 12);
    CHECK(lines[0].seldepth == 18);
    CHECK(lines[0].score->value == 35);
    CHECK(lines[0].wdl == std::array<int64_t, 3>{300, 600, 100});
    CHECK(lines[0].pv == std::vector<std::string>{"e2e4", "e7e5", "g1f3"});
    CHECK(lines[1].pv.empty());  // not told yet
    CHECK(lines[2].score->type == fastchess::ScoreType::MATE);
    CHECK(lines[2].score->value == -4);

    // A bound without its PV keeps the last PV; a rank beyond those asked, a currmove line, a string: nothing.
    CHECK(takeInfoLine(lines, "info depth 13 multipv 1 score cp 50 lowerbound nodes 20000", 3));
    CHECK(lines[0].lowerbound);
    CHECK(lines[0].score->value == 50);
    CHECK(lines[0].pv.size() == 3);
    CHECK_FALSE(takeInfoLine(lines, "info depth 13 multipv 4 score cp 1 pv h2h3", 3));
    CHECK_FALSE(takeInfoLine(lines, "info depth 13 currmove e2e4 currmovenumber 1", 3));
    CHECK_FALSE(takeInfoLine(lines, "info string score cp 10 is a joke", 3));
    CHECK_FALSE(takeInfoLine(lines, "bestmove e2e4", 3));
    CHECK(lines.size() == 3);

    CHECK(goCommand({}) == "go infinite");
    CHECK(goCommand({1500, 0}) == "go movetime 1500");
    CHECK(goCommand({0, 18}) == "go depth 18");
    CHECK(positionCommand({"8/8/8/8/8/8/8/K1k5 w - - 0 1", {}, false}) == "position fen 8/8/8/8/8/8/8/K1k5 w - - 0 1");
    CHECK(positionCommand({std::string(chess::constants::STARTPOS), {"e2e4", "e7e5"}, false}) ==
          "position fen " + std::string(chess::constants::STARTPOS) + " moves e2e4 e7e5");
}

TEST_CASE("analysis: the mock engine analyses positions one after the other, with several lines") {
    if (!fs::is_regular_file(randomMover())) return;
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    const int before = boardThreads();

    Analyser a;
    AnalysisSettings settings;
    settings.engine  = "mover";
    settings.threads = 3;
    settings.multipv = 3;
    a.start(mover(), settings);
    const AnalysisPosition start{std::string(chess::constants::STARTPOS), {}, false};
    a.analyse(start);
    REQUIRE(pollUntil(a, [&] { return a.lines().size() == 3 && a.lines()[0].depth == 8; }));
    CHECK(a.state() == Analyser::State::Searching);
    CHECK(a.hasOption("MultiPV"));
    CHECK(a.lines()[0].wdl.has_value());  // UCI_ShowWDL was set
    CHECK(a.lines()[0].pv.front() == "a2a3");
    CHECK(a.threadsInUse() == 3);
    CHECK(boardThreads() == before + 3);
    CHECK_FALSE(a.finished());

    // Another position: the search is stopped, its move awaited, and the new one searched.
    const AnalysisPosition after{std::string(chess::constants::STARTPOS), {"e2e4"}, false};
    a.analyse(after);
    REQUIRE(pollUntil(a, [&] {
        return a.position() == after && a.lines().size() == 3 && a.lines()[0].depth == 8 &&
               a.state() == Analyser::State::Searching;
    }));
    CHECK(a.lines()[0].pv.front() == "a7a6");  // Black's moves now

    // Fewer lines, sent between two searches: the search starts again with them.
    settings.multipv = 1;
    a.update(settings);
    REQUIRE(pollUntil(a, [&] { return a.state() == Analyser::State::Searching && a.lines().size() == 1; }));

    // A limited search ends with the engine's move.
    a.analyse(start, {200, 0});
    REQUIRE(pollUntil(a, [&] { return a.finished(); }));
    CHECK(a.bestmove() == "a2a3");
    CHECK(a.threadsInUse() == 0);

    a.halt();
    a.stop();
    CHECK(a.state() == Analyser::State::Off);
    CHECK(boardThreads() == before);
}

TEST_CASE("analysis: the positions of a game searched one after the other, as its analysis move by move does") {
    if (!fs::is_regular_file(randomMover())) return;
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    ReplayGame game;
    game.start_fen = std::string(chess::constants::STARTPOS);
    chess::Board b;
    int ply = 0;
    for (const char* uci : {"e2e4", "e7e5", "g1f3", "d7d6", "d2d4", "c8g4", "d4e5", "g4f3", "d1f3", "d6e5", "f1c4",
                            "g8f6", "f3b3", "d8e7", "b1c3", "c7c6"}) {
        const auto move = chess::uci::uciToMove(b, uci);
        ply             = playOnGame(game, false, ply, move);
        b.makeMove(move);
    }
    Analyser a;
    AnalysisSettings settings;
    settings.multipv = 1;
    a.start(mover(), settings);
    for (int i = 0; i <= ply; ++i) {
        CAPTURE(i);
        const auto position = analysisPosition(game, false, i);
        a.analyse(position, {100, 0}, i == 0);
        REQUIRE(pollUntil(a, [&] { return a.finished() && a.position() == position; }));
        CHECK(!a.lines().empty());
        CHECK(!a.bestmove().empty());
    }
}

TEST_CASE("analysis: an engine that cannot start, or stops, says so") {
    Analyser a;
    auto e = mover();
    e.cmd  = (fs::temp_directory_path() / "no-such-engine-fcg").string();
    a.start(e, {});
    CHECK(a.state() == Analyser::State::Failed);
    CHECK(a.error().find("mover") != std::string::npos);
}

TEST_CASE("board: moves played over a game, the position for the analysis, scores from White's side") {
    ReplayGame game;
    game.start_fen = std::string(chess::constants::STARTPOS);
    chess::Board b;
    int ply = 0;
    for (const char* uci : {"e2e4", "e7e5", "g1f3"}) {
        const auto move = chess::uci::uciToMove(b, uci);
        ply             = playOnGame(game, false, ply, move);
        b.makeMove(move);
    }
    CHECK(ply == 3);
    REQUIRE(game.moves.size() == 3);
    CHECK(game.moves[2].san == "Nf3");
    CHECK(game.moves[1].mover == chess::Color::BLACK);

    // The game's own next move only goes on; another one takes the place of the rest.
    game.result = "1-0";
    auto board  = boardAt(game, false, 1);
    CHECK(playOnGame(game, false, 1, chess::uci::uciToMove(board, "e7e5")) == 2);
    CHECK(game.moves.size() == 3);
    CHECK(game.result == "1-0");
    CHECK(playOnGame(game, false, 1, chess::uci::uciToMove(board, "c7c5")) == 2);
    CHECK(game.moves.size() == 2);
    CHECK(game.moves[1].san == "c5");
    CHECK(game.result.empty());

    const auto position = analysisPosition(game, false, 2);
    CHECK(position.fen == std::string(chess::constants::STARTPOS));
    CHECK(position.moves == std::vector<std::string>{"e2e4", "c7c5"});
    CHECK(analysisPosition(game, false, 0).moves.empty());
    CHECK(boardAt(game, false, 99).getFen() == boardAt(game, false, 2).getFen());

    CHECK(whiteScore({fastchess::ScoreType::CP, 35}, chess::Color::BLACK).value == -35);
    CHECK(whiteScore({fastchess::ScoreType::MATE, 3}, chess::Color::WHITE).value == 3);

    // The moves of a piece, castling shown on the king's square.
    chess::Board castle("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    std::vector<std::string> targets;
    for (const auto& m : movesFrom(castle, chess::Square::SQ_E1))
        targets.push_back(static_cast<std::string>(shownTarget(m)));
    CHECK(std::find(targets.begin(), targets.end(), "g1") != targets.end());
    CHECK(std::find(targets.begin(), targets.end(), "c1") != targets.end());
    CHECK(movesFrom(castle, chess::Square::SQ_E8).empty());  // not its move
}

TEST_CASE("board text: a FEN, an EPD line, a PGN, moves in SAN or UCI, or nothing") {
    // A FEN, its move counters kept; an EPD line's four fields, with counters added.
    auto t = readBoardText("  r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3\n");
    REQUIRE(t.kind == BoardText::Kind::Position);
    CHECK(t.fen == "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
    CHECK_FALSE(t.chess960);
    t = readBoardText("4k3/8/8/8/8/8/4P3/4K3 b - - bm Kd7; id \"test\";");
    REQUIRE(t.kind == BoardText::Kind::Position);
    CHECK(t.fen == "4k3/8/8/8/8/8/4P3/4K3 b - - 0 1");
    // Castling rights whose rooks are not in the corners: Chess960.
    t = readBoardText("bqnbrkrn/pppppppp/8/8/8/8/PPPPPPPP/BQNBRKRN w KQkq - 0 1");
    REQUIRE(t.kind == BoardText::Kind::Position);
    CHECK(t.chess960);
    CHECK(needsChess960("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w HAha - 0 1"));
    CHECK_FALSE(needsChess960(std::string(chess::constants::STARTPOS)));

    // Positions no game can be played from.
    t = readBoardText("8/8/8/8/8/8/8/K7 w - - 0 1");
    CHECK(t.kind == BoardText::Kind::Nothing);
    CHECK(t.error.find("Black") != std::string::npos);
    CHECK(readBoardText("4k3/8/8/8/8/8/8/P3K3 w - - 0 1").kind == BoardText::Kind::Nothing);  // a pawn on rank 1
    CHECK_FALSE(positionProblem("4k3/8/8/8/8/8/8/4K2r b - - 0 1", false).empty());  // White, not to move, in check
    CHECK(positionProblem("4k3/8/8/8/8/8/8/4K2r w - - 0 1", false).empty());
    CHECK_FALSE(positionProblem("4k3/9/8/8/8/8/8/4K3 w - - 0 1", false).empty());

    // A PGN as it is; moves in SAN, numbered or not; in UCI, made SAN.
    t = readBoardText("[Event \"x\"]\n[White \"A\"]\n\n1. e4 e5 *\n");
    REQUIRE(t.kind == BoardText::Kind::Games);
    CHECK(t.pgn.find("[White \"A\"]") != std::string::npos);
    t = readBoardText("1. e4 e5 2. Nf3 Nc6 3.Bb5");
    REQUIRE(t.kind == BoardText::Kind::Games);
    std::string error;
    auto game = readPgnGame(t.pgn, error);
    REQUIRE(game);
    CHECK(game->moves.size() == 5);
    CHECK(game->moves[4].san == "Bb5");
    t = readBoardText("e2e4 e7e5 g1f3 b8c6 e1e8");  // the last one is not legal: the moves before it
    REQUIRE(t.kind == BoardText::Kind::Games);
    game = readPgnGame(t.pgn, error);
    REQUIRE(game);
    REQUIRE(game->moves.size() == 4);
    CHECK(game->moves[3].san == "Nc6");
    CHECK(readBoardText("e2e5").kind == BoardText::Kind::Nothing);
    CHECK(readBoardText("").kind == BoardText::Kind::Nothing);
    CHECK(readBoardText("hello world").kind == BoardText::Kind::Nothing);
}

TEST_CASE("board text: lichess evaluations, a FEN that cannot be played, a game written back") {
    // [%eval] is White's, in pawns or moves to mate; the move's score is its mover's.
    std::string error;
    auto game = readPgnGame(
        "[Event \"x\"]\n\n1. e4 {[%eval 0.3] [%clk 0:03:00]} e5 {[%eval 0.42]} 2. Qh5 {[%eval #-3]} *", error);
    REQUIRE(game);
    REQUIRE(game->moves.size() == 3);
    CHECK(game->moves[0].score->value == 30);
    CHECK(game->moves[1].score->value == -42);
    CHECK(game->moves[2].score->type == fastchess::ScoreType::MATE);
    CHECK(game->moves[2].score->value == -3);  // White moved, and Black mates in 3
    // ChessBase's, with the depth after a comma.
    game = readPgnGame("[Event \"x\"]\n\n1. d4 {[%eval -0.12,21] [%emt 0:00:02]} *", error);
    REQUIRE(game);
    CHECK(game->moves[0].score->value == -12);
    CHECK(game->moves[0].depth == 21);
    // A cutechess or fastchess comment still wins.
    // A comment that starts a line, after a line cut.
    game = readPgnGame(
        "[Event \"x\"]\n[SetUp \"1\"]\n[FEN \"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 "
        "3\"]\n\n3. Bb5 {0.00/1 0.001s} b6 {0.00/1 0.000s} 4. g4 {0.00/1 0.000s} Be7\n{0.00/1 0.000s} 5. a3 "
        "{0.00/1 0.000s} Bf8 {0.00/1 0.000s} 6. d3 {0.00/1 0.000s}\nQe7 {0.00/1 0.000s} *",
        error);
    REQUIRE(game);
    CHECK(game->moves.size() == 8);
    game = readPgnGame("[Event \"x\"]\n\n1. e4 {+0.25/12 0.5s} *", error);
    REQUIRE(game);
    CHECK(game->moves[0].score->value == 25);
    CHECK(game->moves[0].depth == 12);

    // A FEN tag the board cannot read, or an illegal position: no game.
    CHECK_FALSE(readPgnGame("[FEN \"8/8/8/8/8/8/8/K7 w - - 0 1\"]\n\n1. Kb1 *", error));
    CHECK_FALSE(readPgnGame("[FEN \"4k3/8/8/8/8/8/8/4K2r b - - 0 1\"]\n\n*", error));

    // Written back and read again: the same moves, tags and scores.
    game = readPgnGame(
        "[Event \"Test\"]\n[White \"A\"]\n[Black \"B\"]\n[Result \"1-0\"]\n[FEN "
        "\"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1\"]\n\n1. e4 {+1.50/20 1.250s} Kd7 {-M4/18 0.5s} 1-0",
        error);
    REQUIRE(game);
    const auto pgn = gamePgn(*game, false);
    CHECK(pgn.find("[SetUp \"1\"]") != std::string::npos);
    CHECK(pgn.find("[FEN \"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1\"]") != std::string::npos);
    CHECK(pgn.find("[PlyCount \"2\"]") != std::string::npos);
    CHECK(pgn.find("1. e4 {+1.50/20 1.250s} Kd7 {-M4/18 0.500s} 1-0") != std::string::npos);
    const auto again = readPgnGame(pgn, error);
    REQUIRE(again);
    CHECK(again->header("White") == "A");
    CHECK(again->result == "1-0");
    REQUIRE(again->moves.size() == 2);
    CHECK(again->moves[1].score->type == fastchess::ScoreType::MATE);
    CHECK(again->moves[1].score->value == game->moves[1].score->value);
    CHECK(again->moves[0].time_ms == 1250);
}

TEST_CASE("board: the position editor's FEN, castling and en passant") {
    PositionEditor e;
    REQUIRE(editorLoad(e, std::string(chess::constants::STARTPOS)));
    CHECK(editorFen(e) == std::string(chess::constants::STARTPOS));
    // The king moved: no castling for White; a rook gone: none on its side.
    e.squares[4]  = chess::Piece::NONE;                                         // e1
    e.squares[5]  = chess::Piece(chess::PieceType::KING, chess::Color::WHITE);  // f1
    e.squares[63] = chess::Piece::NONE;                                         // h8
    CHECK_FALSE(editorCanCastle(e, 0));
    CHECK_FALSE(editorCanCastle(e, 2));
    CHECK(editorCanCastle(e, 3));
    CHECK(editorFen(e) == "rnbqkbn1/pppppppp/8/8/8/8/PPPPPPPP/RNBQ1KNR w q - 0 1");
    // Chess960: the king between its rooks.
    e.chess960 = true;
    CHECK(editorCanCastle(e, 0));
    CHECK(editorCanCastle(e, 1));

    // En passant only behind a pawn that just moved two squares.
    REQUIRE(editorLoad(e, "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 12"));
    CHECK(e.ep_file == 3);
    CHECK(e.fullmove == 12);
    CHECK(editorFen(e) == "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 12");
    e.ep_file = 2;
    CHECK(editorFen(e) == "4k3/8/8/3pP3/8/8/8/4K3 w - - 0 12");

    // A placement without kings is kept while it is edited; a broken one is refused.
    REQUIRE(editorLoad(e, "8/8/8/8/8/8/8/8 w - - 0 1"));
    CHECK(editorFen(e) == "8/8/8/8/8/8/8/8 w - - 0 1");
    CHECK_FALSE(editorLoad(e, "8/8/8/8/8/8/8 w - - 0 1"));
    CHECK_FALSE(editorLoad(e, "9/8/8/8/8/8/8/8 w - - 0 1"));
    CHECK_FALSE(editorLoad(e, "8/8/8/8/8/8/8/7x w - - 0 1"));
    // Shredder-FEN rights.
    REQUIRE(editorLoad(e, "rk5r/8/8/8/8/8/8/RK5R w HAha - 0 1"));
    CHECK(e.castle[0]);
    CHECK(e.castle[1]);
    CHECK(e.castle[2]);
    CHECK(e.castle[3]);
}
