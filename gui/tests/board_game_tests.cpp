// A game between two engines of the library from a position of the Board page, played outside fastchess, against
// the mock engine: a pair with the colours reversed, a loss on time, a stop, the PGN file.

#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include <tour/pgn_index.hpp>

#include "../../tournament/tests/self_path.hpp"
#include "board_game.hpp"
#include "board_text.hpp"
#include "run.hpp"

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

std::string mockEngine() { return (fcg::test::selfPath().parent_path() / FCG_RANDOM_MOVER).string(); }

void setVar(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

EngineEntry engine(const std::string& name, int threads = 1) {
    EngineEntry e;
    e.name     = name;
    e.cmd      = mockEngine();
    e.protocol = "uci";
    if (threads > 1) e.options.emplace_back("Threads", std::to_string(threads));
    return e;
}

BoardMatch::State waitEnd(BoardMatch& match, std::chrono::seconds limit = 60s) {
    BoardMatch::State state;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < until) {
        match.state(state, version);
        if (!state.running) break;
        std::this_thread::sleep_for(10ms);
    }
    match.reap();
    return state;
}

fs::path freshDir(const char* name) {
    const auto dir =
        fs::temp_directory_path() /
        (std::string(name) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("board game: a pair of games with the colours reversed, every move legal, both in the PGN file") {
    if (!fs::is_regular_file(mockEngine())) return;
    const auto dir = freshDir("fcg-board-game");
    const auto pgn = dir / "games.pgn";

    BoardMatch match;
    BoardMatchSetup setup;
    setup.first        = "Alpha";
    setup.second       = "Bravo";
    setup.first_clock  = {10, 0.1};
    setup.second_clock = {10, 0.1};
    setup.pair         = true;
    setup.fen          = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
    std::string error;
    const int before = boardThreads();
    REQUIRE(match.start(engine("Alpha", 3), engine("Bravo"), setup, pgn, error));
    CHECK(boardThreads() == before + 3);  // the larger of the two: they take turns
    const auto state = waitEnd(match);
    CHECK(boardThreads() == before);
    CHECK(state.error.empty());
    REQUIRE(state.finished.size() == 2);
    CHECK(state.finished[0].header("White") == "Alpha");
    CHECK(state.finished[0].header("Black") == "Bravo");
    CHECK(state.finished[1].header("White") == "Bravo");
    CHECK(state.finished[1].header("Round") == "2");
    CHECK(state.finished[0].header("TimeControl") == "10+0.1");
    for (const auto& g : state.finished) {
        CAPTURE(g.final_comment);
        CHECK((g.result == "1-0" || g.result == "0-1" || g.result == "1/2-1/2"));
        CHECK(!g.final_comment.empty());
        CHECK(g.start_fen == setup.fen);
        REQUIRE(!g.moves.empty());
        CHECK(g.moves[0].mover == chess::Color::WHITE);
        CHECK(g.moves[0].time_ms >= 0);
    }

    // Both games in the file, read back move for move.
    std::ifstream in(pgn, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();  // Windows removes no file still open
    std::istringstream stream(text);
    const auto index = tour::indexPgn(stream);
    REQUIRE(index.size() == 2);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& span = index.games[i].span;
        std::string read_error;
        const auto game = readPgnGame(text.substr(span.offset, span.length), read_error);
        REQUIRE(game);
        CHECK(game->moves.size() == state.finished[i].moves.size());
        CHECK(game->result == state.finished[i].result);
        CHECK(game->header("Termination") == state.finished[i].final_comment);
    }
    fs::remove_all(dir);
}

TEST_CASE("board game: an engine that thinks past its clock loses on time; a stop ends the game without a result") {
    if (!fs::is_regular_file(mockEngine())) return;
    const auto dir = freshDir("fcg-board-game-time");
    BoardMatchSetup setup;
    setup.first        = "Slow";
    setup.second       = "Bravo";
    setup.first_clock  = {0.1, 0};
    setup.second_clock = {60, 0};
    setup.fen          = std::string(chess::constants::STARTPOS);
    std::string error;
    {
        setVar("FCG_MOCK_MOVE_MS", "400");
        BoardMatch match;
        REQUIRE(match.start(engine("Slow"), engine("Bravo"), setup, dir / "time.pgn", error));
        const auto state = waitEnd(match);
        setVar("FCG_MOCK_MOVE_MS", nullptr);
        REQUIRE(state.finished.size() == 1);
        CHECK(state.finished[0].result == "0-1");
        CHECK(state.finished[0].final_comment.find("Slow") != std::string::npos);
        CHECK(state.finished[0].moves.empty());
    }
    {
        setVar("FCG_MOCK_MOVE_MS", "200");
        setup.first_clock = {60, 0};
        BoardMatch match;
        REQUIRE(match.start(engine("Slow"), engine("Bravo"), setup, dir / "stop.pgn", error));
        std::this_thread::sleep_for(700ms);
        const auto asked = std::chrono::steady_clock::now();
        match.stop();
        setVar("FCG_MOCK_MOVE_MS", nullptr);
        CHECK(std::chrono::steady_clock::now() - asked < 5s);
        CHECK_FALSE(match.running());
        BoardMatch::State state;
        uint64_t version = 0;
        match.state(state, version);
        REQUIRE(state.finished.size() == 1);
        CHECK(state.finished[0].result == "*");
    }
    // An engine without its program.
    {
        BoardMatch match;
        auto missing = engine("Gone");
        missing.cmd  = (dir / "no-such-engine").string();
        CHECK_FALSE(match.start(missing, engine("Bravo"), setup, dir / "x.pgn", error));
        CHECK(error.find("no-such-engine") != std::string::npos);
    }
    CHECK(clockText({60, 0.6}) == "1+0.6");
    CHECK(clockText({2400, 0}) == "40+0");
    CHECK(clockText({30, 0.2}) == "0:30+0.2");
    CHECK(clockText({90, 1}) == "1:30+1");
    fs::remove_all(dir);
}
