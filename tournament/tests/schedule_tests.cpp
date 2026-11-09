#include <doctest/doctest.hpp>

#include <map>
#include <set>

#include <tour/schedule.hpp>

using namespace fcg::tour;

namespace {

TournamentSpec specWith(Format format, int players, int cycles = 1, int seeds = 1) {
    TournamentSpec spec;
    spec.format = format;
    spec.cycles = cycles;
    spec.seeds  = seeds;
    for (int i = 0; i < players; ++i) {
        Participant p;
        p.id          = 10 + i;  // ids are not indexes
        p.engine.name = "engine" + std::to_string(i);
        spec.participants.push_back(p);
    }
    return spec;
}

}  // namespace

TEST_CASE("Berger tables: every pair once per cycle, every player at most once per round") {
    for (int n = 2; n <= 11; ++n) {
        CAPTURE(n);
        const auto rounds = bergerRounds(n);
        CHECK(rounds.size() == static_cast<std::size_t>(n % 2 == 0 ? n - 1 : n));

        std::map<std::pair<int, int>, int> met;
        std::map<int, int> whites;
        for (const auto& round : rounds) {
            std::set<int> seen;
            for (const auto& [a, b] : round) {
                CHECK(a != b);
                CHECK(seen.insert(a).second);
                CHECK(seen.insert(b).second);
                met[{std::min(a, b), std::max(a, b)}]++;
                whites[a]++;
            }
            // A bye at most when n is odd.
            CHECK(seen.size() >= static_cast<std::size_t>(n - n % 2));
        }

        CHECK(met.size() == static_cast<std::size_t>(n * (n - 1) / 2));
        for (const auto& [pair, count] : met) CHECK(count == 1);

        // The first game's colours are spread: nobody has White in every encounter (n > 3).
        if (n > 3) {
            for (const auto& [player, count] : whites) CHECK(count < n - 1);
        }
    }
}

TEST_CASE("round robin schedule: cycles, colours alternating, participant ids") {
    const auto spec = specWith(Format::RoundRobin, 5, 4);
    const Schedule schedule(spec);

    REQUIRE(schedule.size());
    CHECK(*schedule.size() == 10u * 4u);

    std::map<std::pair<int, int>, int> as_first;  // (first, second) -> count
    for (uint32_t i = 0; i < *schedule.size(); ++i) {
        const auto e = schedule.at(i);
        REQUIRE(e);
        CHECK(e->index == i);
        CHECK(e->first >= 10);
        CHECK(e->second >= 10);
        as_first[{e->first, e->second}]++;
    }
    CHECK_FALSE(schedule.at(*schedule.size()));

    // Each pair is met 4 times, twice with each participant as `first`.
    for (const auto& [pair, count] : as_first) {
        CHECK(count == 2);
        CHECK(as_first[{pair.second, pair.first}] == 2);
    }
}

TEST_CASE("gauntlet schedule: seeds meet everyone else, not each other") {
    const auto spec = specWith(Format::Gauntlet, 6, 2, 2);
    const Schedule schedule(spec);

    REQUIRE(schedule.size());
    CHECK(*schedule.size() == 2u * 4u * 2u);

    std::map<int, std::set<int>> opponents;
    for (uint32_t i = 0; i < *schedule.size(); ++i) {
        const auto e    = schedule.at(i);
        const int seed  = std::min(e->first, e->second);
        const int other = std::max(e->first, e->second);
        CHECK(seed < 12);
        CHECK(other >= 12);
        opponents[seed].insert(other);
    }
    CHECK(opponents.size() == 2);
    for (const auto& [seed, set] : opponents) CHECK(set.size() == 4);
}

TEST_CASE("match schedule: unbounded, or half the game limit rounded up") {
    auto spec = specWith(Format::Match, 2);
    {
        const Schedule schedule(spec);
        CHECK_FALSE(schedule.size());
        const auto e = schedule.at(123456);
        REQUIRE(e);
        CHECK(e->first == 10);
        CHECK(e->second == 11);
    }
    spec.max_games = 7;
    {
        const Schedule schedule(spec);
        REQUIRE(schedule.size());
        CHECK(*schedule.size() == 4);
    }

    CHECK_THROWS_AS(Schedule(specWith(Format::Match, 3)), std::invalid_argument);
    CHECK_THROWS_AS(Schedule(specWith(Format::RoundRobin, 1)), std::invalid_argument);
}
