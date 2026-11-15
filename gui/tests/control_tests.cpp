// The control channel of a tournament process: its messages come back as they went.

#include <doctest/doctest.hpp>

#include <filesystem>

#include "control.hpp"

using namespace fcg;
using namespace fcg::control;
namespace fs = std::filesystem;

TEST_CASE("control: hello, status and end come back as they went") {
    Hello h;
    h.version       = "9.9.9";
    h.dir           = "/runs/a";
    h.pid           = 4242;
    h.journal       = 12345;
    h.playing_ms    = 3'600'000;
    const auto back = parseHello(nlohmann::json::parse(helloJson(h).dump()));
    REQUIRE(back);
    CHECK(back->protocol == PROTOCOL);
    CHECK(back->version == "9.9.9");
    CHECK(back->dir == "/runs/a");
    CHECK(back->pid == 4242);
    CHECK(back->journal == 12345);
    CHECK(back->playing_ms == 3'600'000);
    CHECK_FALSE(parseHello(nlohmann::json{{"seq", 1}, {"e", "state"}}));

    Status s;
    s.phase         = Status::Phase::Running;
    s.paused        = true;
    s.stop_idle     = true;
    s.running_games = 3;
    s.open_pairs    = 1;
    s.running       = {4, 9, 12};
    s.can_undo      = true;
    s.pgn_out_error = "disk full";
    tour::StartFailures::Engine e;
    e.participant = 2;
    e.name        = "Broken";
    e.error       = "cannot execute";
    e.tries       = 4;
    e.failed      = true;
    e.next_try    = tour::StartFailures::Clock::now() + std::chrono::seconds(100);
    s.start_failures.push_back(e);
    s.stalled           = true;
    s.in_flight         = {6, 900, 1};
    s.allowance.threads = 8;
    s.allowance.gpus    = 1;
    s.waiting           = tour::Wait::Memory;
    const auto status   = statusFromJson(nlohmann::json::parse(statusJson(s).dump()));
    CHECK(status == s);
    REQUIRE(status.start_failures.size() == 1);
    // The next try, in seconds from now: about the same moment in the other process.
    const auto next = status.start_failures[0].next_try - tour::StartFailures::Clock::now();
    CHECK(next > std::chrono::seconds(95));
    CHECK(next <= std::chrono::seconds(100));
    Status other = s;
    other.running_games++;
    CHECK_FALSE(other == s);

    End end;
    end.error           = "boom";
    end.abnormal        = true;
    end.failed_engine   = "X";
    end.failed_reason   = "no uciok";
    end.exit_code       = 1;
    const auto end_back = endFromJson(nlohmann::json::parse(endJson(end).dump()));
    CHECK(end_back.error == "boom");
    CHECK(end_back.abnormal);
    CHECK_FALSE(end_back.finished);
    CHECK(end_back.failed_engine == "X");
    CHECK(end_back.failed_reason == "no uciok");
    CHECK(end_back.exit_code == 1);
}

TEST_CASE("control: the games in progress travel move by move, and a slot as a whole") {
    fastchess::observer::GameStart start;
    start.slot       = 2;
    start.game_id    = 17;
    start.round_id   = 9;
    start.white      = "A";
    start.black      = "B";
    start.start_fen  = chess::constants::STARTPOS;
    start.book_moves = {"e2e4", "c7c5"};
    start.white_time = 10000;
    const auto g     = gameStartFromJson(gameStartJson(start));
    CHECK(g.slot == 2);
    CHECK(g.game_id == 17);
    CHECK(g.round_id == 9);
    CHECK(g.white == "A");
    CHECK(g.book_moves == start.book_moves);
    CHECK(g.white_time == 10000);
    CHECK(g.black_time == 0);

    fastchess::MoveData m("g1f3");
    m.score           = fastchess::Score{fastchess::ScoreType::MATE, -3};
    m.wdl             = std::array<int64_t, 3>{10, 20, 970};
    m.depth           = 21;
    m.seldepth        = 30;
    m.elapsed_millis  = 1234;
    m.nodes           = 99999;
    m.nps             = 80000;
    m.tbhits          = 5;
    m.decision_millis = 800;
    m.pv              = "g1f3 b8c6";
    const auto md     = moveDataFromJson(nlohmann::json::parse(moveDataJson(m).dump()));
    CHECK(md.move == "g1f3");
    REQUIRE(md.score);
    CHECK(md.score->type == fastchess::ScoreType::MATE);
    CHECK(md.score->value == -3);
    CHECK(md.wdl == m.wdl);
    CHECK(md.depth == 21);
    CHECK(md.seldepth == 30);
    CHECK(md.elapsed_millis == 1234);
    CHECK(md.nodes == 99999);
    CHECK(md.nps == 80000);
    CHECK(md.tbhits == 5);
    CHECK(md.decision_millis == 800);
    CHECK(md.pv == "g1f3 b8c6");

    // A slot with two moves, one of them a castling of Chess960 (king takes rook).
    SlotState slot;
    slot.active    = true;
    slot.has_game  = true;
    slot.game_id   = 5;
    slot.white     = "A";
    slot.black     = "B";
    slot.start_fen = "bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9";
    chess::Board board;
    board.set960(true);
    board.setFen(slot.start_fen);
    for (const char* uci : {"g1h1", "c5c4"}) {
        LiveMove lm;
        lm.move  = chess::uci::uciToMove(board, uci);
        lm.san   = chess::uci::moveToSan(board, lm.move);
        lm.mover = board.sideToMove();
        lm.depth = 12;
        lm.score = fastchess::Score{fastchess::ScoreType::CP, 35};
        board.makeMove<true>(lm.move);
        slot.moves.push_back(lm);
    }
    slot.moves[0].book = true;
    slot.stm           = chess::Color::WHITE;
    slot.info[1]       = "info depth 3 score cp 10";
    slot.white_ms      = 5000;
    slot.clocks        = true;
    const auto back    = slotFromJson(nlohmann::json::parse(slotJson(slot, true).dump()));
    CHECK(back.active);
    CHECK(back.game_id == 5);
    CHECK(back.start_fen == slot.start_fen);
    REQUIRE(back.moves.size() == 2);
    CHECK(back.moves[0].move == slot.moves[0].move);
    CHECK(back.moves[0].book);
    CHECK(back.moves[1].move == slot.moves[1].move);
    CHECK(back.moves[1].san == slot.moves[1].san);
    CHECK(back.moves[1].depth == 12);
    CHECK(back.moves[1].score->value == 35);
    CHECK(back.info[1] == slot.info[1]);
    CHECK(back.white_ms == 5000);
    CHECK(back.clocks);
}

TEST_CASE("control: totals, statistics and allowances come back the same; lines are split as they come") {
    EngineTotals t;
    t.moves            = 40;
    t.depth_sum        = 800;
    t.nps_sum          = 1.5e7;
    t.nps_samples      = 38;
    t.nodes_sum        = 123456789;
    t.nodes_samples    = 40;
    t.time_ms          = 30000;
    t.decision_ms      = 20000;
    t.decision_samples = 35;
    const auto tb      = totalsFromJson(totalsJson(t));
    CHECK(tb.moves == 40);
    CHECK(tb.depth_sum == 800);
    CHECK(tb.nps_sum == doctest::Approx(1.5e7));
    CHECK(tb.nodes_sum == 123456789);
    CHECK(tb.decision_samples == 35);

    fastchess::Stats s(10, 7, 20);
    s.penta_WW      = 2;
    s.penta_LL      = 1;
    s.penta_DD      = 9;
    const auto back = statsFromJson(statsJson(s));
    CHECK(back == s);

    tour::Allowance a;
    a.threads         = 12;
    a.memory_mb       = 4096;
    a.gpus            = 0;
    a.one_game_always = false;
    CHECK(allowanceFromJson(allowanceJson(a)) == a);
    CHECK(allowanceFromJson(nlohmann::json::object()) == tour::Allowance{});
    CHECK(costFromJson(costJson({4, 300, 1})) == tour::GameCost{4, 300, 1});
    for (const auto w : {tour::Wait::None, tour::Wait::Cores, tour::Wait::Memory, tour::Wait::Gpus})
        CHECK(waitFromName(waitName(w)) == w);

    const auto c = command("pause", 7);
    CHECK(c["c"] == "pause");
    CHECK(c["id"] == 7);

    LineSplitter split;
    std::vector<std::string> lines;
    split.feed("{\"a\":1}\n{\"b\"", lines);
    CHECK(lines.size() == 1);
    split.feed(":2}\r\n\n", lines);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "{\"a\":1}");
    CHECK(lines[1] == "{\"b\":2}");
    CHECK(lines[2].empty());
    // A line too long is dropped, and the next one read.
    lines.clear();
    split.feed(std::string(100, 'x'), lines, 50);
    split.feed(std::string(100, 'y') + "\nok\n", lines, 50);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "ok");
}

TEST_CASE("control: the machines a tournament's engines run on, in its status, read back and compared") {
    control::Status s;
    control::Status::Machine m;
    m.id          = "gpu1";
    m.name        = "gpu-box";
    m.connections = 0;
    m.engines     = 0;
    m.error       = "gpu-box stopped answering: the machine is off, or the network is down.";
    m.since       = 1794770000;
    s.machines.push_back(m);
    const auto back = control::statusFromJson(control::statusJson(s));
    REQUIRE(back.machines.size() == 1);
    CHECK(back.machines[0] == m);
    CHECK(back == s);
    auto other                   = s;
    other.machines[0].connections = 1;
    CHECK_FALSE(other == s);  // sent again when a machine comes back
    CHECK_FALSE(control::statusJson(control::Status{}).contains("machines"));
}
