#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>

#include <tour/ccrl.hpp>
#include <tour/pgn_file.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;

namespace {

std::string game(const std::string& white, const std::string& black, const std::string& result, int plies,
                 const std::string& termination = "normal") {
    return fmt::format(
        "[Event \"fastchess-gui\"]\n[Site \"?\"]\n[Date \"2026.11.12\"]\n[Round \"7\"]\n[White \"{}\"]\n"
        "[Black \"{}\"]\n[Result \"{}\"]\n[PlyCount \"{}\"]\n[Termination \"{}\"]\n[TimeControl \"60+1\"]\n\n"
        "1. e4 {{+0.30/20 1.2s}} e5 {{-0.25/19 1.1s}} {}\n\n",
        white, black, result, plies, termination, result);
}

}  // namespace

TEST_CASE("ccrl: names as the lists write them, the file and zip names, the event") {
    CHECK(ccrlName("Stockfish 19", 4) == "Stockfish 19 64-bit 4CPU");
    CHECK(ccrlName("Stockfish 19", 1) == "Stockfish 19 64-bit");
    CHECK(ccrlName(" Stockfish 19 64-Bit ", 8) == "Stockfish 19 64-Bit 8CPU");
    CHECK(ccrlName("Stockfish 19 64-bit 4CPU", 2) == "Stockfish 19 64-bit 2CPU");
    CHECK(ccrlName("Stockfish 19 4CPU", 1) == "Stockfish 19 64-bit");
    CHECK(ccrlName("RubiChess CPU", 1) == "RubiChess CPU 64-bit");

    CcrlExport e;
    e.tester        = "Francesco Torsello";
    e.date          = "2026-09-28";
    e.event         = "Triumviratus 7.0 64-bit 8CPU - Sep 27";
    e.hash_mb       = 4096;
    e.book          = "avt-book-2026";
    e.egtb          = 5;
    const auto stem = ccrlFileStem(e);
    CHECK(stem ==
          "[Francesco Torsello 2026-09-28] Triumviratus 7.0 64-bit 8CPU - Sep 27 (hash 4096MB) (book avt-book-2026) "
          "(egtb 5-man)");
    // As TorsGUI names it, after the testers' scripts.
    CHECK(ccrlZipName(stem) ==
          "Francesco_Torsello_2026-09-28_Triumviratus_7.0_64-bit_8CPU_-_Sep_27_hash_4096MB_book_avt-book-2026_egtb_5-"
          "man.zip");

    // Noon, so that the day is the same in every time zone of the tests.
    CHECK(ccrlEvent("Caissa 2.0 64-bit 4CPU", int64_t{1758542400} * 1000) == "Caissa 2.0 64-bit 4CPU - Sep 22");

    fastchess::EngineConfiguration engine;
    engine.options = {{"Hash", "256"}, {"Threads", "4"}, {"Ponder", "x"}};
    CHECK(optionValue(engine, "Threads", 1) == 4);
    CHECK(optionValue(engine, "Hash", 0) == 256);
    CHECK(optionValue(engine, "Ponder", 7) == 7);
    CHECK(optionValue(engine, "SyzygyPath", 0) == 0);
}

TEST_CASE("ccrl: the games renamed and numbered again, the voided, duplicated and crashed ones left out") {
    const auto dir = fs::temp_directory_path() / "fcg-ccrl-export";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto file = dir / "games.pgn";

    TournamentSpec spec;
    for (int i = 0; i < 3; ++i) {
        Participant p;
        p.id          = 10 + i;
        p.engine.name = std::string("Engine ") + static_cast<char>('A' + i);
        spec.participants.push_back(p);
    }

    std::vector<GameResult> results;
    {
        PgnFile pgn(file);
        const auto add = [&](uint32_t encounter, int g, int white, int black, const std::string& result,
                             const std::string& text, int64_t at,
                             fastchess::MatchTermination term = fastchess::MatchTermination::NORMAL) {
            GameResult r;
            r.encounter   = encounter;
            r.game        = g;
            r.white       = white;
            r.black       = black;
            r.result      = result;
            r.termination = term;
            r.pgn         = pgn.append(text);
            r.finished_at = at;
            results.push_back(r);
        };
        add(0, 0, 10, 11, "1-0", game("Engine A", "Engine B", "1-0", 2), 100);
        add(0, 1, 11, 10, "1/2-1/2", game("Engine B", "Engine A", "1/2-1/2", 2), 200);
        results.back().voided = true;  // replayed below
        add(0, 1, 11, 10, "1/2-1/2", game("Engine B", "Engine A", "1/2-1/2", 2), 300);
        add(0, 1, 11, 10, "0-1", game("Engine B", "Engine A", "0-1", 2), 350);  // recorded twice: left out
        add(1, 0, 10, 12, "0-1", game("Engine A", "Engine C", "0-1", 2, "abandoned"), 400,
            fastchess::MatchTermination::DISCONNECT);
        add(1, 1, 12, 10, "0-1", game("Engine C", "Engine A", "0-1", 3, "illegal move"), 250,
            fastchess::MatchTermination::ILLEGAL_MOVE);
    }

    CcrlExport e;
    e.event        = "Engine A 64-bit 2CPU - Nov 12";
    e.site         = "Milan";
    e.names        = {{10, "Engine A 64-bit 2CPU"}, {11, "Engine B 64-bit"}, {12, "  "}};
    const auto out = ccrlGames(file, results, spec, e);
    CHECK(out.games == 3);
    CHECK(out.voided == 1);
    CHECK(out.duplicates == 1);
    CHECK(out.crashes == 1);
    CHECK(out.unreadable == 0);

    // In the order they ended, numbered from 1; a name left blank is the engine's own; the illegal move does not
    // count in PlyCount; every other tag as played.
    CHECK(out.pgn ==
          "[Event \"Engine A 64-bit 2CPU - Nov 12\"]\n[Site \"Milan\"]\n[Date \"2026.11.12\"]\n[Round \"1\"]\n"
          "[White \"Engine A 64-bit 2CPU\"]\n[Black \"Engine B 64-bit\"]\n[Result \"1-0\"]\n[PlyCount \"2\"]\n"
          "[Termination \"normal\"]\n[TimeControl \"60+1\"]\n\n1. e4 {+0.30/20 1.2s} e5 {-0.25/19 1.1s} 1-0\n\n"
          "[Event \"Engine A 64-bit 2CPU - Nov 12\"]\n[Site \"Milan\"]\n[Date \"2026.11.12\"]\n[Round \"2\"]\n"
          "[White \"Engine C\"]\n[Black \"Engine A 64-bit 2CPU\"]\n[Result \"0-1\"]\n[PlyCount \"2\"]\n"
          "[Termination \"illegal move\"]\n[TimeControl \"60+1\"]\n\n1. e4 {+0.30/20 1.2s} e5 {-0.25/19 1.1s} 0-1\n\n"
          "[Event \"Engine A 64-bit 2CPU - Nov 12\"]\n[Site \"Milan\"]\n[Date \"2026.11.12\"]\n[Round \"3\"]\n"
          "[White \"Engine B 64-bit\"]\n[Black \"Engine A 64-bit 2CPU\"]\n[Result \"1/2-1/2\"]\n[PlyCount \"2\"]\n"
          "[Termination \"normal\"]\n[TimeControl \"60+1\"]\n\n1. e4 {+0.30/20 1.2s} e5 {-0.25/19 1.1s} 1/2-1/2\n\n");

    // Kept when asked, the site as played when none is given.
    e.leave_out_crashes = false;
    e.site.clear();
    const auto all = ccrlGames(file, results, spec, e);
    CHECK(all.games == 4);
    CHECK(all.crashes == 0);
    CHECK(all.pgn.find("[Site \"?\"]") != std::string::npos);
    CHECK(pgnTag(all.pgn, "Round") == "1");
    CHECK(pgnTag("[Event \"a \\\"b\\\"\"]\n", "Event") == "a \"b\"");
    CHECK(pgnTag("[Event \"a\"]\n", "Site").empty());

    fs::remove_all(dir);
}
