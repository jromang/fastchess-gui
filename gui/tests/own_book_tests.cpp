// A participant's own opening book: the names that tell the same engine apart, the setup and the tournament that
// keep the book, and tournaments played through the book adapter.

#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <engine/uci_engine.hpp>
#include <tour/book_adapter.hpp>
#include <tour/pgn_book.hpp>

#include "../../tournament/tests/polyglot_writer.hpp"
#include "../../tournament/tests/self_path.hpp"
#include "book_jobs.hpp"
#include "run.hpp"

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

fs::path self() { return fcg::test::selfPath(); }

OwnBook ownBook(const std::string& path, const std::string& name, int depth = 0) {
    OwnBook book;
    book.path  = path;
    book.name  = name;
    book.depth = depth;
    return book;
}

ParticipantSetup participant(const std::string& engine, const OwnBook& book = {}) {
    ParticipantSetup p;
    p.name = engine;
    p.book = book;
    return p;
}

// 1. e4 or 1. d4 for White; an answer to every first move for Black.
fs::path writeBook(const fs::path& path) {
    using fcg::test::polyglotKey;
    std::vector<std::tuple<uint64_t, std::string, int>> entries = {{polyglotKey(""), "e2e4", 1},
                                                                   {polyglotKey(""), "d2d4", 1}};
    chess::Movelist firsts;
    chess::movegen::legalmoves(firsts, chess::Board());
    for (const auto& m : firsts) entries.emplace_back(polyglotKey(chess::uci::moveToUci(m)), "g8f6", 1);
    fcg::test::writePolyglot(path, entries);
    return path;
}

}  // namespace

TEST_CASE("own book: a book that cannot play for an engine is told, and the tournament does not start") {
    const auto dir = fs::temp_directory_path() / "fcg-own-book-problems";
    fs::create_directories(dir);
    // Unsorted but small: sorted in memory, it plays.
    const auto small = dir / "small.bin";
    fcg::test::writePolyglot(
        small, {{fcg::test::polyglotKey("e2e4"), "e7e5", 1}, {fcg::test::polyglotKey(""), "e2e4", 1}}, false);
    CHECK(ownBookProblem(small.string()).empty());

    // Unsorted and too large to be sorted in memory; no Polyglot book at all.
    const auto large = dir / "large.bin";
    fcg::test::writeLargePolyglot(large, tour::PolyglotBook::SORT_MAX + 16, false);
    CHECK(ownBookProblem(large.string()).find("not sorted") != std::string::npos);
    const auto junk = dir / "junk.bin";
    std::ofstream(junk, std::ios::binary) << "not a book";
    CHECK_FALSE(ownBookProblem(junk.string()).empty());

    // The start refuses it, naming the participant.
    const auto mover = (self().parent_path() / FCG_RANDOM_MOVER).string();
    if (fs::is_regular_file(mover)) {
        std::vector<EngineEntry> engines(1);
        engines[0].name = "random", engines[0].cmd = mover;
        MatchSetup setup;
        setup.engine_a         = "random";
        setup.engine_b         = "random";
        setup.book_a           = ownBook(large.string(), "Large");
        setup.book_b           = ownBook(small.string(), "Small");
        setup.fixed_games      = 2;
        setup.limit            = LimitKind::Nodes;
        setup.nodes            = 1;
        setup.concurrency_auto = false;
        setup.concurrency      = 1;
        setup.notify           = false;
        RunController run;
        const auto error = run.start(setup, engines, topologyFromCores({{0, 0}}), dir / "runs");
        INFO(error);
        CHECK(error.find("random (Large)") != std::string::npos);
        CHECK(error.find("not sorted") != std::string::npos);
    }
    fs::remove_all(dir);
}

TEST_CASE("own book: the same engine with books of its own plays under names that tell it apart") {
    MatchSetup setup;
    setup.format       = TournamentFormat::RoundRobin;
    const auto a       = ownBook("/books/a.bin", "Book A", 12);
    const auto b       = ownBook("/books/b.bin", "");  // no library name: the file's
    setup.participants = {participant("Alpha", a), participant("Alpha", b), participant("Beta", a)};
    CHECK(participantNames(setup) == std::vector<std::string>{"Alpha (Book A)", "Alpha (b)", "Beta"});

    // The engine alone and with a book: the one without keeps the engine's name.
    setup.participants = {participant("Alpha"), participant("Alpha", a)};
    CHECK(participantNames(setup) == std::vector<std::string>{"Alpha", "Alpha (Book A)"});

    // Twice without a book, or twice with the same: the same name twice, which the start refuses.
    setup.participants = {participant("Alpha"), participant("Alpha")};
    CHECK(participantNames(setup) == std::vector<std::string>{"Alpha", "Alpha"});

    // A match of an engine against itself, each side with its book.
    MatchSetup match;
    match.engine_a = match.engine_b = "Alpha";
    match.book_a                    = a;
    match.book_b                    = b;
    CHECK(participantNames(match) == std::vector<std::string>{"Alpha (Book A)", "Alpha (b)"});
    CHECK(defaultTournamentName(match) == "Alpha (Book A) vs Alpha (b)");
    match.engine_b = "Beta";
    CHECK(participantNames(match) == std::vector<std::string>{"Alpha", "Beta"});

    CHECK(ownBookText("Book A", 12) == "own book Book A, 12 plies");
    CHECK(ownBookText("Book A", 0) == "own book Book A, all of it");
}

TEST_CASE("own book: the setup, its templates and the tournament keep each participant's book and depth") {
    const auto dir = fs::temp_directory_path() / "fcg-tests-own-book-setup";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto binary = dir / "engine.exe";  // fastchess appends .exe to extension-less commands on Windows
    std::ofstream(binary) << "x";
    const auto book = writeBook(dir / "a.bin").string();

    std::vector<EngineEntry> engines(2);
    engines[0].name = "Alpha", engines[0].cmd = binary.string();
    engines[1].name = "Beta", engines[1].cmd = binary.string();

    MatchSetup setup;
    setup.format       = TournamentFormat::RoundRobin;
    setup.limit        = LimitKind::Nodes;
    setup.participants = {participant("Alpha", ownBook(book, "A", 10)), participant("Alpha"), participant("Beta")};

    // Templates and the queue store the setup as JSON; a setup of an older version has no books.
    const auto back = nlohmann::json(setup).get<MatchSetup>();
    REQUIRE(back.participants.size() == 3);
    CHECK(back.participants[0].book.path == book);
    CHECK(back.participants[0].book.name == "A");
    CHECK(back.participants[0].book.depth == 10);
    CHECK(back.participants[1].book.empty());
    auto old = nlohmann::json(setup);
    for (auto& p : old["participants"]) p.erase("book");
    old.erase("book_a");
    CHECK(old.get<MatchSetup>().participants[0].book.empty());
    CHECK(old.get<MatchSetup>().book_a.empty());

    const auto topology = topologyFromCores({{0, 0}, {0, 1}});
    auto run            = buildRun(setup, engines, topology, dir / "run");
    REQUIRE(run.spec.participants.size() == 3);
    CHECK(run.spec.participants[0].engine.name == "Alpha (A)");
    CHECK(run.spec.participants[0].engine.cmd == binary.string());  // the adapter comes in when it plays
    CHECK(run.spec.participants[0].book == book);
    CHECK(run.spec.participants[0].book_depth == 10);
    CHECK(run.spec.participants[1].engine.name == "Alpha");
    CHECK(run.spec.participants[1].book.empty());

    // tournament.json: what resume and --run play.
    const auto stored = nlohmann::json(run.spec).get<tour::TournamentSpec>();
    CHECK(stored.participants[0].book == book);
    CHECK(stored.participants[0].book_depth == 10);
    CHECK(stored.participants[0].engine.name == "Alpha (A)");

    // The same engine twice, apart by nothing; a book that is not a Polyglot one, or not there.
    setup.participants[0].book = {};
    CHECK_THROWS_WITH((void)buildRun(setup, engines, topology, dir / "run"), doctest::Contains("twice"));
    setup.participants[0].book = ownBook((dir / "openings.epd").string(), "EPD");
    CHECK_THROWS_WITH((void)buildRun(setup, engines, topology, dir / "run"), doctest::Contains("Polyglot"));
    setup.participants[0].book = ownBook((dir / "missing.bin").string(), "Missing");
    CHECK_THROWS_WITH((void)buildRun(setup, engines, topology, dir / "run"), doctest::Contains("not found"));

    // A match of an engine against itself needs a book to tell them apart.
    MatchSetup match;
    match.engine_a = match.engine_b = "Alpha";
    match.limit                     = LimitKind::Nodes;
    CHECK_THROWS_AS((void)buildRun(match, engines, topology, dir / "run"), std::exception);
    match.book_b = ownBook(book, "A");
    run          = buildRun(match, engines, topology, dir / "run");
    CHECK(run.spec.participants[0].engine.name == "Alpha");
    CHECK(run.spec.participants[1].engine.name == "Alpha (A)");
    CHECK(run.spec.participants[1].book == book);

    fs::remove_all(dir);
}

TEST_CASE("own book: a round robin with the same engine twice and a WinBoard engine, each with a book of its own") {
    const auto wb  = (self().parent_path() / FCG_WB_MOVER).string();
    const auto uci = (self().parent_path() / FCG_RANDOM_MOVER).string();
    if (!fs::is_regular_file(wb) || !fs::is_regular_file(uci)) {
        MESSAGE("mock engines not available on this platform, skipped");
        return;
    }
    fastchess::engine::xboard_adapter = self().string();
    tour::book_adapter                = self().string();

    const auto root = fs::temp_directory_path() / "fcg-own-book-tests";
    fs::remove_all(root);
    fs::create_directories(root);
    const auto book = writeBook(root / "first moves.bin").string();

    std::vector<EngineEntry> engines(2);
    engines[0].name = "uci", engines[0].cmd = uci;
    engines[1].name = "wb", engines[1].cmd = wb, engines[1].args = "v2", engines[1].protocol = "xboard";

    MatchSetup setup;
    setup.format           = TournamentFormat::RoundRobin;
    setup.name             = "Own books";
    setup.participants     = {participant("uci", ownBook(book, "First", 2)), participant("uci"),
                              participant("wb", ownBook(book, "First"))};
    setup.limit            = LimitKind::Time;
    setup.tc_base_s        = 10;
    setup.tc_inc_s         = 0.1;
    setup.concurrency_auto = false;
    setup.concurrency      = 3;
    setup.affinity         = false;
    setup.notify           = false;

    RunController run;
    std::vector<GameRecord> games;
    std::vector<fastchess::Stats> stats;
    REQUIRE(run.start(setup, engines, topologyFromCores({{0, 0}, {0, 1}, {0, 2}, {0, 3}}), root).empty());
    const auto end = std::chrono::steady_clock::now() + 120s;
    while (run.state() != RunController::State::Finished && std::chrono::steady_clock::now() < end) {
        run.update();
        if (auto* obs = run.observer()) obs->drain(games, stats);
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(run.state() == RunController::State::Finished);
    CHECK(run.error().empty());
    REQUIRE(run.observer());
    CHECK(run.observer()->names() == std::vector<std::string>{"uci (First)", "uci", "wb"});

    const auto& results = run.tournament()->results();
    CHECK(results.size() == 6);
    for (const auto& r : results) {
        INFO(r.reason);
        CHECK(r.reason.find("illegal") == std::string::npos);
        CHECK(r.reason.find("disconnect") == std::string::npos);
        CHECK(r.reason.find("stalls") == std::string::npos);
    }

    // The players' first moves come from their books; the engine without one plays its own.
    std::ifstream in(run.tournament()->dir() / run.tournament()->spec().pgnFile(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int checked = 0;
    for (auto at = text.find("[Event "); at != std::string::npos;) {
        const auto next  = text.find("[Event ", at + 1);
        const auto game  = text.substr(at, next == std::string::npos ? std::string::npos : next - at);
        const auto moves = game.substr(game.find("\n\n") + 2);
        INFO(game);
        if (game.find("[White \"uci\"]") == std::string::npos) {
            CHECK((moves.rfind("1. e4 {book}", 0) == 0 || moves.rfind("1. d4 {book}", 0) == 0));
            ++checked;
        } else {
            CHECK(moves.substr(0, moves.find('}')).find("book") == std::string::npos);
            CHECK(moves.find(" Nf6 {book}") != std::string::npos);
            ++checked;
        }
        at = next;
    }
    CHECK(checked == 6);

    fastchess::engine::xboard_adapter.clear();
    tour::book_adapter.clear();
}

namespace {

// A game for each first move, each answered 1... Nf6, and one game that cannot be read.
fs::path writePgnBook(const fs::path& path) {
    std::ofstream out(path, std::ios::binary);
    chess::Board board;
    chess::Movelist firsts;
    chess::movegen::legalmoves(firsts, board);
    for (const auto& m : firsts) {
        out << "[Event \"answer\"]\n[Result \"*\"]\n\n1. " << chess::uci::moveToSan(board, m) << " Nf6 *\n\n";
    }
    out << "[Event \"broken\"]\n[Result \"*\"]\n\n1. e4 Ke7 *\n\n";
    return path;
}

// Polls the conversions, as the frames do, until `pgn` at `depth` is no longer being prepared.
PgnBookJobs::Status prepared(PgnBookJobs& jobs, const std::string& pgn, int depth) {
    using State = PgnBookJobs::Status::State;
    auto status = jobs.want(pgn, depth, false);
    for (int i = 0; i < 1000 && (status.state == State::Queued || status.state == State::Preparing); ++i) {
        (void)jobs.poll();
        std::this_thread::sleep_for(5ms);
        status = jobs.want(pgn, depth, false);
    }
    return status;
}

}  // namespace

TEST_CASE("own book: a PGN book is prepared in the background while it is asked for, and found again after") {
    using State         = PgnBookJobs::Status::State;
    const auto root     = fs::temp_directory_path() / "fcg-own-book-pgn-jobs";
    const auto previous = tour::book_cache_dir;
    fs::remove_all(root);
    fs::create_directories(root);
    tour::book_cache_dir = root / "cache";
    const auto pgn       = writePgnBook(root / "answers.pgn").string();

    {
        PgnBookJobs jobs;
        CHECK(jobs.want(pgn, 4, false).state == State::Queued);
        const auto status = prepared(jobs, pgn, 4);
        REQUIRE(status.state == State::Ready);
        CHECK(status.stats.games == 20);
        CHECK(status.stats.skipped == 1);

        // Another depth: prepared again, but only while somebody asks for it.
        CHECK(jobs.want(pgn, 6, false).state == State::Queued);
        (void)jobs.poll();
        CHECK(jobs.busy());
        // Not asked for at the next frame: cancelled, nothing kept.
        for (int i = 0; i < 1000 && jobs.busy(); ++i) {
            (void)jobs.poll();
            std::this_thread::sleep_for(1ms);
        }
        CHECK_FALSE(jobs.busy());

        // A file that is not there, or that gives no move: failed, with the reason.
        CHECK(jobs.want((root / "none.pgn").string(), 4, false).state == State::Failed);
        std::ofstream(root / "empty.pgn", std::ios::binary) << "[Event \"x\"]\n\n*\n";
        const auto empty = prepared(jobs, (root / "empty.pgn").string(), 4);
        CHECK(empty.state == State::Failed);
        CHECK(empty.error.find("no game") != std::string::npos);
    }
    // Another session: found in the cache at once.
    PgnBookJobs jobs;
    const auto again = jobs.want(pgn, 4, false);
    CHECK(again.state == State::Ready);
    CHECK(again.stats.skipped == 1);

    tour::book_cache_dir = previous;
    fs::remove_all(root);
}

TEST_CASE("own book: a match with a PGN book of its own, prepared by the start when the window did not") {
    const auto uci = (self().parent_path() / FCG_RANDOM_MOVER).string();
    if (!fs::is_regular_file(uci)) {
        MESSAGE("mock engine not available on this platform, skipped");
        return;
    }
    tour::book_adapter  = self().string();
    const auto previous = tour::book_cache_dir;
    const auto root     = fs::temp_directory_path() / "fcg-own-book-pgn-match";
    fs::remove_all(root);
    fs::create_directories(root);
    tour::book_cache_dir = root / "cache";
    const auto pgn       = writePgnBook(root / "answers.pgn").string();

    std::vector<EngineEntry> engines(1);
    engines[0].name = "uci", engines[0].cmd = uci;
    MatchSetup setup;
    setup.engine_a         = "uci";
    setup.engine_b         = "uci";
    setup.book_b           = ownBook(pgn, "Answers", 2);
    setup.mode             = MatchMode::Fixed;
    setup.fixed_games      = 4;
    setup.limit            = LimitKind::Nodes;
    setup.nodes            = 1;
    setup.concurrency_auto = false;
    setup.concurrency      = 2;
    setup.affinity         = false;
    setup.notify           = false;
    REQUIRE(pgnBookNeeds(setup).size() == 1);
    CHECK_FALSE(tour::cachedPgnBook(pgn, 2, false));

    RunController run;
    std::vector<GameRecord> games;
    std::vector<fastchess::Stats> stats;
    REQUIRE(run.start(setup, engines, topologyFromCores({{0, 0}, {0, 1}}), root / "runs").empty());
    CHECK(tour::cachedPgnBook(pgn, 2, false));
    const auto end = std::chrono::steady_clock::now() + 60s;
    while (run.state() != RunController::State::Finished && std::chrono::steady_clock::now() < end) {
        run.update();
        if (auto* obs = run.observer()) obs->drain(games, stats);
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(run.state() == RunController::State::Finished);
    CHECK(run.error().empty());
    REQUIRE(run.tournament());
    CHECK(run.tournament()->results().size() == 4);
    // The tournament keeps the PGN book as the participant's book.
    CHECK(run.tournament()->spec().participants[1].book == pgn);

    // The book's side answers 1... Nf6 from its book; it never plays White's first move from it.
    std::ifstream in(run.tournament()->dir() / run.tournament()->spec().pgnFile(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int checked = 0;
    for (auto at = text.find("[Event "); at != std::string::npos;) {
        const auto next  = text.find("[Event ", at + 1);
        const auto game  = text.substr(at, next == std::string::npos ? std::string::npos : next - at);
        const auto moves = game.substr(game.find("\n\n") + 2);
        INFO(game);
        if (game.find("[Black \"uci (Answers)\"]") != std::string::npos) {
            CHECK(moves.substr(0, moves.find('}')).find("book") == std::string::npos);
            CHECK(moves.find(" Nf6 {book}") != std::string::npos);
        } else {
            CHECK(moves.find("{book}") < moves.find("1..."));
        }
        ++checked;
        at = next;
    }
    CHECK(checked == 4);

    tour::book_adapter.clear();
    tour::book_cache_dir = previous;
    // The tournament is still open (its journal, on Windows, cannot be removed): what can be.
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST_CASE("own book: a conversion that failed is done again when asked, never by itself") {
    using State         = PgnBookJobs::Status::State;
    const auto root     = fs::temp_directory_path() / "fcg-own-book-pgn-retry";
    const auto previous = tour::book_cache_dir;
    fs::remove_all(root);
    fs::create_directories(root);
    const auto pgn = writePgnBook(root / "answers.pgn").string();
    // A file where the cache's folder would be: the book cannot be written, as on a full disk.
    tour::book_cache_dir = root / "cache";
    std::ofstream(tour::book_cache_dir, std::ios::binary) << "in the way";

    PgnBookJobs jobs;
    const auto failed = prepared(jobs, pgn, 4);
    REQUIRE(failed.state == State::Failed);
    CHECK_FALSE(failed.error.empty());

    // The disk has room again: still failed, until a start or "Try again" asks for it once more.
    fs::remove(tour::book_cache_dir);
    for (int i = 0; i < 5; ++i) {
        CHECK(jobs.want(pgn, 4, false).state == State::Failed);
        (void)jobs.poll();
        CHECK_FALSE(jobs.busy());
    }
    jobs.retry(pgn, 4, false);
    const auto again = prepared(jobs, pgn, 4);
    CHECK(again.state == State::Ready);
    CHECK(again.stats.games == 20);
    // A book that is ready is left as it is.
    jobs.retry(pgn, 4, false);
    CHECK(jobs.want(pgn, 4, false).state == State::Ready);

    tour::book_cache_dir = previous;
    fs::remove_all(root);
}

TEST_CASE("own book: a withdrawn participant's PGN book, deleted since, does not hold the resume") {
    const auto uci = (self().parent_path() / FCG_RANDOM_MOVER).string();
    if (!fs::is_regular_file(uci)) {
        MESSAGE("mock engine not available on this platform, skipped");
        return;
    }
    tour::book_adapter  = self().string();
    const auto previous = tour::book_cache_dir;
    const auto root     = fs::temp_directory_path() / "fcg-own-book-pgn-withdrawn";
    fs::remove_all(root);
    fs::create_directories(root);
    tour::book_cache_dir = root / "cache";
    const auto pgn       = writePgnBook(root / "answers.pgn").string();
    const auto dir       = root / "t";

    tour::TournamentSpec spec;
    spec.name                   = "withdrawn";
    spec.format                 = tour::Format::RoundRobin;
    spec.cycles                 = 1;
    spec.conditions.concurrency = 1;
    for (int i = 0; i < 4; ++i) {
        tour::Participant p;
        p.id                 = i;
        p.engine.name        = "random " + std::to_string(i);
        p.engine.cmd         = uci;
        p.engine.limit.nodes = 1;
        spec.participants.push_back(p);
    }
    spec.participants[3].book = pgn;
    REQUIRE(pgnBookNeeds(spec).size() == 1);
    {
        auto state = tour::TournamentState::create(dir, spec);
        tour::Edit withdraw;
        withdraw.kind        = tour::Edit::Kind::Withdraw;
        withdraw.participant = 3;
        state->applyEdit(withdraw);
    }
    fs::remove(pgn);

    // What the window's resume waits for, from the tournament as listed.
    const auto summary = tour::summarize(dir);
    REQUIRE(summary);
    CHECK(pgnBookNeeds(summary->spec).empty());

    RunController run;
    std::vector<GameRecord> games;
    std::vector<fastchess::Stats> stats;
    REQUIRE(run.resume(dir).empty());
    const auto end = std::chrono::steady_clock::now() + 60s;
    while (run.state() != RunController::State::Finished && std::chrono::steady_clock::now() < end) {
        run.update();
        if (auto* obs = run.observer()) obs->drain(games, stats);
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(run.state() == RunController::State::Finished);
    CHECK(run.error().empty());
    REQUIRE(run.tournament());
    CHECK(run.tournament()->results().size() == 6);  // the three others, once each way

    tour::book_adapter.clear();
    tour::book_cache_dir = previous;
    // The tournament is still open (its journal, on Windows, cannot be removed): what can be.
    std::error_code ec;
    fs::remove_all(root, ec);
}
