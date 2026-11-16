// The engines that cannot start: the tries counted, the games set aside and brought back.

#include <doctest/doctest.hpp>

#include <filesystem>
#include <set>

#include <tour/state.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

TournamentSpec roundRobin(int players) {
    TournamentSpec spec;
    spec.name   = "start failures";
    spec.format = Format::RoundRobin;
    for (int i = 0; i < players; ++i) {
        Participant p;
        p.id          = i;
        p.engine.name = "E" + std::to_string(i);
        spec.participants.push_back(p);
    }
    return spec;
}

fs::path freshDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("fcg-tour-" + name);
    fs::remove_all(dir);
    return dir;
}

bool involves(const GameTask& task, int id) { return task.encounter.first == id || task.encounter.second == id; }

// A drawn game as fastchess would report it, without moves.
fastchess::MatchData drawn(const GameTask& task) {
    fastchess::MatchData data(chess::constants::STARTPOS);
    data.players     = {fastchess::MatchData::PlayerInfo(task.white, chess::GameResult::DRAW),
                        fastchess::MatchData::PlayerInfo(task.black, chess::GameResult::DRAW)};
    data.termination = fastchess::MatchTermination::NORMAL;
    data.reason      = "test";
    data.duration    = "00:00:02";
    return data;
}

}  // namespace

TEST_CASE("start failures: fastchess' lines about an engine that does not start are read, others are not") {
    auto line = parseStartFailure(
        "Warning; Stock fish 17 engine startup failure: \"Couldn't start \"engine\": no such file\", trying again in "
        "10 s\n");
    REQUIRE(line);
    CHECK(line->engine == "Stock fish 17");
    CHECK(line->error == "Couldn't start \"engine\": no such file");

    line = parseStartFailure("Fatal; A engine startup failure: \"Engine didn't respond to uciok after startup\"\n");
    REQUIRE(line);
    CHECK(line->engine == "A");
    CHECK(line->error == "Engine didn't respond to uciok after startup");

    CHECK_FALSE(parseStartFailure("Warning; Failed to set option Hash with value 64 for engine A: unknown"));
    CHECK_FALSE(parseStartFailure("Something; A engine startup failure: \"x\""));
    CHECK_FALSE(parseStartFailure("Warning; A engine startup failure: no quotes"));
}

TEST_CASE("start failures: tries a pause apart, then the engine is set aside and tried again now and then") {
    StartFailures f;
    const auto t0 = StartFailures::Clock::now();
    CHECK(f.empty());
    CHECK(f.setAside(t0).empty());

    // The first failure is a try; the games of the engine claimed meanwhile fail with it, without counting.
    CHECK(f.failed(2, 10, "E2", "no such file", t0) == StartFailures::Outcome::Retry);
    CHECK(f.failed(2, 11, "E2", "no such file", t0 + 1s) == StartFailures::Outcome::Same);
    CHECK(f.setAside(t0 + 9s) == std::set<int>{2});
    CHECK(f.deciding());

    // Due: the next game claimed is its try, and only it; the engine is set aside while it is tried.
    auto now = t0 + StartFailures::RETRY_PAUSE;
    CHECK(f.setAside(now).empty());
    f.claimed(2, 12);
    CHECK(f.setAside(now) == std::set<int>{2});
    CHECK(f.failed(2, 11, "E2", "late", now) == StartFailures::Outcome::Same);
    for (int i = 2; i < StartFailures::TRIES; ++i) {
        CHECK(f.failed(2, 12, "E2", "no such file", now) == StartFailures::Outcome::Retry);
        now += StartFailures::RETRY_PAUSE;
        f.claimed(2, 12);
    }
    CHECK(f.failed(2, 12, "E2", "still missing", now) == StartFailures::Outcome::Failed);
    CHECK_FALSE(f.deciding());

    auto engines = f.engines();
    REQUIRE(engines.size() == 1u);
    CHECK(engines[0].participant == 2);
    CHECK(engines[0].failed);
    CHECK(engines[0].tries == StartFailures::TRIES);
    CHECK(engines[0].error == "still missing");

    // Set aside until RETRY_EVERY has passed, or until asked.
    CHECK(f.setAside(now + StartFailures::RETRY_EVERY - 1s) == std::set<int>{2});
    CHECK(f.setAside(now + StartFailures::RETRY_EVERY).empty());
    CHECK(f.tryNow(2, now + 1s));
    CHECK(f.setAside(now + 1s).empty());
    f.claimed(2, 13);
    CHECK_FALSE(f.tryNow(2, now + 1s));  // being tried
    CHECK(f.failed(2, 13, "E2", "still missing", now + 2s) == StartFailures::Outcome::StillFailing);
    CHECK(f.setAside(now + 2s + StartFailures::RETRY_EVERY - 1s) == std::set<int>{2});

    // A pause does not count.
    f.postpone(1h);
    CHECK(f.setAside(now + 2s + StartFailures::RETRY_EVERY + 30min) == std::set<int>{2});

    // A try that did not happen (the game given up) leaves the engine due.
    CHECK(f.tryNow(2, now + 3s));
    f.claimed(2, 14);
    f.released(14);
    CHECK(f.setAside(now + 3s).empty());

    // It started: its games come back.
    const auto version = f.version();
    CHECK(f.started(2));
    CHECK(f.version() > version);
    CHECK_FALSE(f.started(2));
    CHECK(f.empty());
}

TEST_CASE("start failures: one last try of each failed engine when nothing else is left; forgotten when withdrawn") {
    StartFailures f;
    f.setWaits(1s, 1min);
    const auto t0 = StartFailures::Clock::now();
    auto now      = t0;
    for (int i = 0; i < StartFailures::TRIES; ++i) {
        f.failed(1, 5, "E1", "gone", now);
        now += 1s;
        f.claimed(1, 5);
    }
    f.released(5);
    CHECK(f.setAside(now) == std::set<int>{1});

    CHECK(f.lastChance(now));
    CHECK(f.setAside(now).empty());
    f.claimed(1, 6);
    CHECK(f.deciding());
    CHECK(f.failed(1, 6, "E1", "gone", now) == StartFailures::Outcome::StillFailing);
    CHECK_FALSE(f.lastChance(now));  // once

    f.forget(1);
    CHECK(f.empty());
}

TEST_CASE("start failures: the games of an engine set aside are passed over, and come first once it can play") {
    const auto dir = freshDir("start-failures-claims");
    std::set<uint64_t> passed;
    {
        auto state = TournamentState::create(dir, roundRobin(4));
        auto& f    = state->startFailures();

        // Participant 3 fails at its first game: its games are left out of the claims.
        auto first = state->claimNext();
        REQUIRE(first);
        while (!involves(*first, 3)) {
            state->complete(*first, drawn(*first));
            first = state->claimNext();
            REQUIRE(first);
        }
        f.failed(3, first->number(), "E3", "no such file", StartFailures::Clock::now());
        state->release(*first);
        passed.insert(first->number());

        // Every other game is played meanwhile (12 games, 6 of them with participant 3).
        int others = 0;
        while (auto task = state->claimNext()) {
            CHECK_FALSE(involves(*task, 3));
            state->complete(*task, drawn(*task));
            ++others;
        }
        CHECK(state->hasWork());
        CHECK_FALSE(state->finished());
        CHECK(static_cast<int>(state->results().size()) == 12 - 6);
        (void)others;

        // Asked to try it again: one game of it, its try; the others wait for the outcome.
        CHECK(f.tryNow(3, StartFailures::Clock::now()));
        const auto again = state->claimNext();
        REQUIRE(again);
        CHECK(involves(*again, 3));
        CHECK_FALSE(state->claimNext());
        state->release(*again);
    }

    // The encounters passed over count as started: a resume plays them, nothing else is left.
    auto state = TournamentState::open(dir);
    CHECK(state->startFailures().empty());
    int left = 0;
    while (auto task = state->claimNext()) {
        CHECK(involves(*task, 3));
        state->complete(*task, drawn(*task));
        ++left;
    }
    CHECK(left == 6);
    CHECK(state->finished());
}

TEST_CASE("start failures: a participant withdrawn or replaced is forgotten") {
    auto state = TournamentState::create(freshDir("start-failures-withdrawn"), roundRobin(3));
    auto& f    = state->startFailures();
    f.failed(2, 0, "E2", "no such file", StartFailures::Clock::now());
    f.failed(1, 1, "E1", "no such file", StartFailures::Clock::now());

    Edit withdraw;
    withdraw.kind        = Edit::Kind::Withdraw;
    withdraw.participant = 2;
    state->applyEdit(withdraw);
    auto engines = f.engines();
    REQUIRE(engines.size() == 1u);
    CHECK(engines[0].participant == 1);

    Edit replace;
    replace.kind        = Edit::Kind::ReplaceParticipant;
    replace.participant = 1;
    replace.engine.name = "E1 fixed";
    state->applyEdit(replace);
    CHECK(f.empty());
}

TEST_CASE("start failures: an engine whose machine keeps breaking off set aside at once, tried again later") {
    StartFailures failures;
    const auto now = StartFailures::Clock::now();
    failures.setAside(4, "Lc0", "the connection to gpu-box broke more than 3 times in an hour", now);
    const auto e = failures.engine(4);
    REQUIRE(e);
    CHECK(e->failed);
    CHECK(e->error.find("gpu-box") != std::string::npos);
    CHECK(failures.setAside(now).count(4) == 1);
    CHECK(failures.tryNow(4, now));  // asked for: tried at once
    CHECK(failures.started(4));
    CHECK(failures.empty());
}
