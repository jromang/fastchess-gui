// A participant's own Polyglot book: the lookup, the adapter's protocol, and games played through it.

#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include <core/config/config.hpp>
#include <core/globals/globals.hpp>
#include <core/observer.hpp>

#include <tour/book_adapter.hpp>
#include <tour/runner.hpp>

#include "polyglot_writer.hpp"

#ifdef _WIN32
#    include <windows.h>
#endif

using namespace fcg::tour;
using fcg::test::polyglotKey;
using fcg::test::writePolyglot;
namespace fs = std::filesystem;

namespace {

fs::path self() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    const auto n = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return fs::path(std::string(buffer, n));
#else
    return fs::read_symlink("/proc/self/exe");
#endif
}

// A book of a few lines: 1. e4 (3) or d4 (1); after 1. e4 e5, 2. Nf3; castling short after the Italian.
fs::path smallBook() {
    const auto path    = fs::temp_directory_path() / "fcg-own-book.bin";
    const auto italian = std::string("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5");
    writePolyglot(path, {{polyglotKey(""), "e2e4", 3},
                         {polyglotKey(""), "d2d4", 1},
                         {polyglotKey(""), "h2h3", 0},  // weighs nothing: never played
                         {polyglotKey(""), "e2e5", 9},  // not a legal move: skipped
                         {polyglotKey("e2e4 e7e5"), "g1f3", 1},
                         {polyglotKey(italian), "e1h1", 1}});
    return path;
}

// The adapter with its two sides recorded.
struct Adapter {
    std::vector<std::string> gui, engine;
    BookAdapter adapter;

    explicit Adapter(const fs::path& book, int depth = 0, uint64_t seed = 1, bool chess960 = false)
        : adapter([this](const std::string& l) { gui.push_back(l); },
                  [this](const std::string& l) { engine.push_back(l); }, std::make_unique<PolyglotBook>(book), depth,
                  seed, chess960) {}

    void fromGui(const std::string& line) { adapter.fromGui(line); }
    [[nodiscard]] bool engineGot(const std::string& line) const {
        return std::find(engine.begin(), engine.end(), line) != engine.end();
    }
    [[nodiscard]] std::size_t enginesGo() const {
        return std::count_if(engine.begin(), engine.end(), [](const auto& l) { return l.rfind("go", 0) == 0; });
    }
};

}  // namespace

TEST_CASE("own book: the moves of a position, weighted, legal ones only; castling as the king takes its rook") {
    PolyglotBook book(smallBook());

    chess::Board board;
    const auto moves = book.moves(board);
    REQUIRE(moves.size() == 2);
    CHECK(chess::uci::moveToUci(moves[0].move) == "e2e4");
    CHECK(moves[0].weight == 3);
    CHECK(chess::uci::moveToUci(moves[1].move) == "d2d4");

    // Out of the book.
    board.makeMove(chess::uci::uciToMove(board, "a2a3"));
    CHECK(book.moves(board).empty());
    std::mt19937_64 rng(1);
    CHECK(book.pick(board, rng) == chess::Move::NO_MOVE);

    // The book says e1h1: UCI writes e1g1, Chess960 e1h1.
    board.setFen("r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4");
    const auto castling = book.moves(board);
    REQUIRE(castling.size() == 1);
    CHECK(castling[0].move.typeOf() == chess::Move::CASTLING);
    CHECK(chess::uci::moveToUci(castling[0].move) == "e1g1");
    CHECK(chess::uci::moveToUci(castling[0].move, true) == "e1h1");

    // About three e4 for one d4.
    board  = chess::Board();
    int e4 = 0;
    for (int i = 0; i < 4000; ++i) e4 += chess::uci::moveToUci(book.pick(board, rng)) == "e2e4";
    CHECK(e4 > 2800);
    CHECK(e4 < 3200);
}

TEST_CASE("own book: a file that is no Polyglot book, or none, is refused with a readable message") {
    const auto path = fs::temp_directory_path() / "fcg-own-book-bad.bin";
    std::ofstream(path, std::ios::binary) << "not a book";
    CHECK_THROWS_WITH_AS(PolyglotBook{path}, doctest::Contains("no Polyglot book"), std::runtime_error);
    CHECK_THROWS_AS(PolyglotBook{fs::temp_directory_path() / "fcg-no-such-book.bin"}, std::runtime_error);
}

TEST_CASE("own book adapter: a book move answers go at once, and the engine never sees that go") {
    Adapter a(smallBook());

    // Everything else goes through, both ways.
    a.fromGui("uci");
    a.adapter.fromEngine("id name Engine");
    a.adapter.fromEngine("uciok");
    a.fromGui("isready");
    a.adapter.fromEngine("readyok");
    CHECK(a.engine == std::vector<std::string>{"uci", "isready"});
    CHECK(a.gui == std::vector<std::string>{"id name Engine", "uciok", "readyok"});

    a.gui.clear();
    a.fromGui("ucinewgame");
    a.fromGui("position startpos");
    a.fromGui("go wtime 10000 btime 10000");
    REQUIRE(a.gui.size() == 1);
    CHECK((a.gui[0] == "bestmove e2e4" || a.gui[0] == "bestmove d2d4"));
    CHECK(a.engineGot("position startpos"));
    CHECK(a.enginesGo() == 0);

    // A stop then (an interrupted game): the engine has nothing to stop, and is not told.
    a.fromGui("stop");
    CHECK_FALSE(a.engineGot("stop"));
    a.fromGui("isready");
    CHECK(a.engine.back() == "isready");

    // Out of the book: the engine searches, a stop reaches it, its move comes back.
    a.gui.clear();
    a.fromGui("position startpos moves e2e4 c7c5");
    a.fromGui("go wtime 10000 btime 10000");
    CHECK(a.engine.back() == "go wtime 10000 btime 10000");
    CHECK(a.gui.empty());
    a.fromGui("stop");
    CHECK(a.engine.back() == "stop");
    a.adapter.fromEngine("info depth 1 score cp 20 pv g1f3");
    a.adapter.fromEngine("bestmove g1f3");
    CHECK(a.gui.back() == "bestmove g1f3");

    // Back in the book after a transposition the book knows (1. e4 e5): its move again.
    a.gui.clear();
    a.fromGui("position startpos moves e2e4 e7e5");
    a.fromGui("go wtime 10000 btime 10000");
    CHECK(a.gui == std::vector<std::string>{"bestmove g1f3"});
    CHECK(a.enginesGo() == 1);

    // A ponder search is the engine's.
    a.fromGui("position startpos");
    a.fromGui("go ponder wtime 10000 btime 10000");
    CHECK(a.engine.back() == "go ponder wtime 10000 btime 10000");
    a.fromGui("ponderhit");
    CHECK(a.engine.back() == "ponderhit");
    a.adapter.fromEngine("bestmove e2e4");

    a.fromGui("quit");
    CHECK(a.engine.back() == "quit");
    CHECK(a.adapter.quitting());
}

TEST_CASE("own book adapter: the depth counts the plies from the game's start, the opening's moves included") {
    Adapter a(smallBook(), 2);
    a.fromGui("ucinewgame");
    a.fromGui("position startpos");
    a.fromGui("go movetime 100");
    CHECK(a.enginesGo() == 0);

    // Ply 2 is in the book (1. e4 e5 2. Nf3) but beyond its depth.
    a.fromGui("position startpos moves e2e4 e7e5");
    a.fromGui("go movetime 100");
    CHECK(a.enginesGo() == 1);

    // An opening given as a position: its plies count from there.
    a.adapter.fromEngine("bestmove g1f3");
    a.fromGui("position fen rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
    a.fromGui("go movetime 100");
    CHECK(a.enginesGo() == 1);
    CHECK(a.gui.back() == "bestmove g1f3");
}

TEST_CASE("own book adapter: castling as UCI wants it, or as Chess960 does") {
    const auto italian = std::string("position startpos moves e2e4 e7e5 g1f3 b8c6 f1c4 f8c5");
    {
        Adapter a(smallBook());
        a.fromGui(italian);
        a.fromGui("go movetime 100");
        CHECK(a.gui.back() == "bestmove e1g1");
    }
    {
        Adapter a(smallBook());
        a.fromGui("setoption name UCI_Chess960 value true");
        CHECK(a.engineGot("setoption name UCI_Chess960 value true"));
        a.fromGui("ucinewgame");
        a.fromGui(
            "position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 moves e2e4 e7e5 g1f3 b8c6 f1c4 "
            "f8c5");
        a.fromGui("go movetime 100");
        CHECK(a.gui.back() == "bestmove e1h1");
    }
}

TEST_CASE("own book adapter: the same seed gives the same choices, game after game; another seed others") {
    const auto choices = [](uint64_t seed) {
        Adapter a(smallBook(), 0, seed);
        std::string out;
        for (int game = 0; game < 40; ++game) {
            a.fromGui("ucinewgame");
            a.fromGui("position startpos");
            a.fromGui("go movetime 100");
            out += a.gui.back().substr(9, 1);
        }
        return out;
    };
    CHECK(choices(7) == choices(7));
    CHECK(choices(7) != choices(8));
    // Both moves come, game after game.
    const auto c = choices(7);
    CHECK(c.find('e') != std::string::npos);
    CHECK(c.find('d') != std::string::npos);
}

TEST_CASE("own book: the configuration through the adapter, a WinBoard engine behind the WinBoard adapter") {
    fastchess::EngineConfiguration engine;
    engine.name = "Alpha";
    engine.cmd  = "/engines/alpha 2";
    engine.args = "--uci -t 1";
    engine.options.emplace_back("Hash", "64");

    const auto previous = book_adapter;
    book_adapter.clear();
    CHECK_THROWS_AS((void)throughBookAdapter(engine, "/books/a.bin", 12, 5), std::runtime_error);

    book_adapter = "/opt/fastchess-gui";
    auto config  = throughBookAdapter(engine, "/books/my book.bin", 12, 5);
    CHECK(config.name == "Alpha");
    CHECK(config.cmd == "/opt/fastchess-gui");
    CHECK(config.args ==
          "--book-adapter --book \"/books/my book.bin\" --depth 12 --seed 5 -- \"/engines/alpha 2\" "
          "--uci -t 1");
    CHECK(config.options == engine.options);

    engine.protocol = "xboard";
    engine.variant  = fastchess::VariantType::FRC;
    config          = throughBookAdapter(engine, "/books/a.bin", 0, 5);
    CHECK(config.protocol == "uci");
    CHECK(config.args ==
          "--book-adapter --book \"/books/a.bin\" --depth 0 --seed 5 --chess960 -- "
          "\"/opt/fastchess-gui\" --wb2uci \"/engines/alpha 2\" --uci -t 1");
    book_adapter = previous;
}

TEST_CASE("own book: the moves the book played are marked as book moves, within the depth, for its side only") {
    fastchess::MatchData data(chess::constants::STARTPOS);
    for (const auto* m : {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "e1g1"}) data.moves.emplace_back(m);
    data.moves[0].book = true;  // the tournament's opening

    PolyglotBook book(smallBook());
    auto white = data;
    markOwnBookMoves(white, chess::Color::WHITE, book, 0, false);
    std::vector<bool> marked;
    for (const auto& m : white.moves) marked.push_back(m.book);
    // e4 was the opening's; Nf3 and O-O are the book's; the Italian's other moves are not in it.
    CHECK(marked == std::vector<bool>{true, false, true, false, false, false, true});

    markOwnBookMoves(data, chess::Color::WHITE, book, 4, false);
    CHECK(data.moves[2].book);
    CHECK_FALSE(data.moves[6].book);

    auto black = fastchess::MatchData(chess::constants::STARTPOS);
    for (const auto* m : {"e2e4", "e7e5"}) black.moves.emplace_back(m);
    markOwnBookMoves(black, chess::Color::BLACK, book, 0, false);
    CHECK_FALSE(black.moves[0].book);
    CHECK_FALSE(black.moves[1].book);
}

TEST_CASE("own book: tournament.json keeps a participant's book and depth, and nothing when it has none") {
    Participant p;
    p.id             = 3;
    p.engine.name    = "Alpha (A)";
    p.book           = "/books/a.bin";
    p.book_depth     = 12;
    nlohmann::json j = p;
    CHECK(j.at("book") == "/books/a.bin");
    CHECK(j.at("book_depth") == 12);
    const auto back = j.get<Participant>();
    CHECK(back.book == "/books/a.bin");
    CHECK(back.book_depth == 12);

    Participant plain;
    j = plain;
    CHECK_FALSE(j.contains("book"));
    CHECK_FALSE(j.contains("book_depth"));
    CHECK(j.get<Participant>().book.empty());
}

TEST_CASE("own book: in a tournament, its moves come from the book, its engine plays the rest") {
    const auto mover = (self().parent_path() / FCG_RANDOM_MOVER).string();
    if (!fs::is_regular_file(mover)) {
        MESSAGE("random mover not available, skipped");
        return;
    }
    const auto previous = book_adapter;
    book_adapter        = self().string();

    // White: 1. e4 or 1. d4. Black: an answer to each first move, the same for all. And 2. Nf3 after any of them
    // when the book plays White, beyond the depth of 2 plies.
    const auto path                                             = fs::temp_directory_path() / "fcg-own-book-tour.bin";
    std::vector<std::tuple<uint64_t, std::string, int>> entries = {{polyglotKey(""), "e2e4", 1},
                                                                   {polyglotKey(""), "d2d4", 1}};
    chess::Movelist firsts;
    chess::movegen::legalmoves(firsts, chess::Board());
    for (const auto& m : firsts) {
        const auto first = chess::uci::moveToUci(m);
        entries.emplace_back(polyglotKey(first), first == "g1h3" ? "b8c6" : "g8h6", 1);
    }
    writePolyglot(path, entries);

    const auto dir = fs::temp_directory_path() / "fcg-tour-own-book";
    fs::remove_all(dir);
    TournamentSpec s;
    s.name                   = "own book";
    s.format                 = Format::Match;
    s.max_games              = 8;
    s.conditions.concurrency = 2;
    s.conditions.seed        = 42;
    for (int i = 0; i < 2; ++i) {
        Participant p;
        p.id                 = i;
        p.engine.name        = i == 0 ? "random (book)" : "random";
        p.engine.cmd         = mover;
        p.engine.limit.nodes = 1;
        s.participants.push_back(p);
    }
    s.participants[0].book       = path.string();
    s.participants[0].book_depth = 2;
    auto state                   = TournamentState::create(dir, s);

    auto config         = state->spec().conditions;
    config.games        = 2;
    config.opening.file = "";
    config.pgn.file     = "";
    config.config_name  = "";
    config.output       = fastchess::OutputType::NONE;
    std::vector<fastchess::EngineConfiguration> engines;
    for (const auto& p : state->spec().participants) engines.push_back(p.engine);
    fastchess::atomic::stop                 = false;
    fastchess::atomic::abnormal_termination = false;
    fastchess::config::TournamentConfig     = std::make_unique<fastchess::config::Tournament>(config);
    fastchess::config::EngineConfigs        = std::make_unique<std::vector<fastchess::EngineConfiguration>>(engines);
    fastchess::observer::PauseGate::instance().reset();
    {
        Runner runner(*state, nullptr, nullptr);
        runner.start();
        CHECK(runner.error().empty());
    }
    fastchess::stopProcesses();
    book_adapter = previous;

    const auto results = state->results();
    REQUIRE(results.size() == 8);
    for (const auto& r : results) CHECK(r.reason.find("disconnect") == std::string::npos);

    // Every game in the PGN: the book side's first move is a book move; the engine's are not.
    std::ifstream in(dir / state->spec().pgnFile(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t games = 0, as_white = 0, as_black = 0;
    for (auto at = text.find("[Event "); at != std::string::npos; ++games) {
        const auto next  = text.find("[Event ", at + 1);
        const auto game  = text.substr(at, next == std::string::npos ? std::string::npos : next - at);
        const auto moves = game.substr(game.find("\n\n") + 2);
        INFO(game);
        if (game.find("[White \"random (book)\"]") != std::string::npos) {
            ++as_white;
            CHECK((moves.rfind("1. e4 {book}", 0) == 0 || moves.rfind("1. d4 {book}", 0) == 0));
            // Its second move is beyond the depth: the engine's, with its search.
            CHECK(moves.find("{book}", 12) > moves.find("2. "));
        } else {
            ++as_black;
            const auto first = moves.find('}');
            CHECK(moves.substr(0, first).find("book") == std::string::npos);
            CHECK(moves.find("{book}", first) != std::string::npos);
            CHECK((moves.find(" Nh6 {book}") != std::string::npos || moves.find(" Nc6 {book}") != std::string::npos));
        }
        at = next;
    }
    CHECK(games == 8);
    CHECK(as_white == 4);
    CHECK(as_black == 4);
}
