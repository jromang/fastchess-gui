// The home page as a control room: the results of the last tournaments, when the queue ends, a drive too full.

#include <doctest/doctest.hpp>

#include <filesystem>

#include "home.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

tour::Summary summary(tour::Format format, std::vector<tour::Summary::Score> scores, const std::string& reason = "",
                      bool finished = true) {
    tour::Summary s;
    s.spec.format = format;
    s.dir         = "/runs/" + std::to_string(reinterpret_cast<std::uintptr_t>(&s));
    s.scores      = std::move(scores);
    for (const auto& score : s.scores) s.played += static_cast<uint64_t>(score.games);
    s.finished      = finished;
    s.finish_reason = finished && reason.empty() ? "complete" : reason;
    return s;
}

}  // namespace

TEST_CASE("home: half points as the standings write them") {
    CHECK(home::halfPoints(0) == "0");
    CHECK(home::halfPoints(0.5) == "½");
    CHECK(home::halfPoints(9.5) == "9½");
    CHECK(home::halfPoints(12) == "12");
}

TEST_CASE("home: the result of a tournament in a few words") {
    // A match: A's score first, then B's.
    auto match = summary(tour::Format::Match, {{"Alpha", 52.5, 100}, {"Beta", 47.5, 100}});
    CHECK(home::resultText(match) == "Alpha 52½ – 47½ Beta");
    // An SPRT: its decision, then the score.
    auto sprt = summary(tour::Format::Match, {{"Alpha", 10, 19}, {"Beta", 9, 19}}, "sprt_h1");
    CHECK(home::resultText(sprt) == "H1 accepted · Alpha 10 – 9 Beta");
    sprt.finish_reason = "sprt_h0";
    CHECK(home::resultText(sprt).rfind("H0 accepted", 0) == 0);
    // A round robin: the first of the standings, its points of its games.
    const auto rr = summary(tour::Format::RoundRobin, {{"Bravo", 9.5, 14}, {"Alpha", 8, 14}});
    CHECK(home::resultText(rr) == "First: Bravo 9½/14");
    // A knockout finished: its winner (summarize() puts the final's winner first).
    const auto ko = summary(tour::Format::Knockout, {{"P1", 6, 6}, {"P3", 13, 26}});
    CHECK(home::resultText(ko) == "Winner: P1");
    // A team match: the teams' score, each engine's points to its team.
    auto teams = summary(tour::Format::TeamMatch, {{"A1", 5, 8}, {"B1", 4.5, 8}, {"A2", 2.5, 8}, {"B2", 4, 8}});
    for (const auto* name : {"A1", "A2", "B1", "B2"}) {
        tour::Participant p;
        p.engine.name = name;
        p.team        = name[0] == 'B' ? 1 : 0;
        teams.spec.participants.push_back(p);
    }
    teams.spec.team_names[1] = "Others";
    CHECK(home::resultText(teams) == "Team A 7½ – 8½ Others");
    // No game.
    CHECK(home::resultText(summary(tour::Format::Gauntlet, {})) == "No game played");
}

TEST_CASE("home: the last tournaments finished, newest first, without the one running") {
    std::vector<tour::Summary> list;
    for (int i = 0; i < 8; ++i) {
        auto s = summary(tour::Format::Match, {{"A", 1, 2}, {"B", 1, 2}}, "", i != 1);
        s.dir  = "/runs/" + std::to_string(i);
        list.push_back(std::move(s));
    }
    auto last = home::lastFinished(list, 5);
    REQUIRE(last.size() == 5);
    CHECK(last[0]->dir == "/runs/0");
    CHECK(last[1]->dir == "/runs/2");  // the interrupted one is left out
    CHECK(last[4]->dir == "/runs/5");

    last = home::lastFinished(list, 3, "/runs/0");
    REQUIRE(last.size() == 3);
    CHECK(last[0]->dir == "/runs/2");
    CHECK(home::lastFinished({}, 5).empty());
}

TEST_CASE("home: when the queue ends, the tournaments of unknown length counted apart") {
    auto end = home::queueEnd({3600.0, std::nullopt, 1800.0});
    CHECK(end.count == 3);
    CHECK(end.unknown == 1);
    CHECK(end.seconds == doctest::Approx(5400));
    CHECK(end.known());

    end = home::queueEnd({std::nullopt, std::nullopt});
    CHECK_FALSE(end.known());
    CHECK_FALSE(home::queueEnd({}).known());
}

TEST_CASE("home: a drive too full, with Open the folder and Change the tournaments folder") {
    constexpr std::uintmax_t GB = std::uintmax_t{1} << 30;
    CHECK(home::diskLow(GB / 2, 2000 * GB));         // under 1 GB, whatever the drive
    CHECK_FALSE(home::diskLow(30 * GB, 2000 * GB));  // 1.5 % of a large drive is still room enough
    CHECK(home::diskLow(3 * GB, 200 * GB));          // 1.5 % of a small one
    CHECK_FALSE(home::diskLow(10 * GB, 200 * GB));

    const home::DiskSpace disk{"/runs", GB / 2, 200 * GB};
    const auto notice = home::diskNotice(disk);
    CHECK(notice.text.find("/runs") != std::string::npos);
    CHECK(notice.text.find("512 MB") != std::string::npos);
    REQUIRE(notice.fixes.size() == 2);
    CHECK(notice.fixes[0].kind == Fix::Kind::OpenFolder);
    CHECK(notice.fixes[0].path == "/runs");
    CHECK(notice.fixes[1].kind == Fix::Kind::ChangeTournamentsFolder);
}

TEST_CASE("home: the free space of a folder not made yet is its drive's") {
    const auto dir = fs::temp_directory_path() / "fcg-home-disk";
    fs::create_directories(dir);
    const auto here = home::diskSpace(dir);
    REQUIRE(here);
    CHECK(here->capacity > 0);
    CHECK(here->available <= here->capacity);
    const auto later = home::diskSpace(dir / "not" / "made" / "yet");
    REQUIRE(later);
    CHECK(later->dir == dir / "not" / "made" / "yet");
    CHECK(later->capacity == here->capacity);
    CHECK_FALSE(home::diskSpace({}));
}
