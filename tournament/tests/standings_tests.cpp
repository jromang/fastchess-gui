#include <doctest/doctest.hpp>

#include <cmath>

#include <tour/standings.hpp>

using namespace fcg::tour;

namespace {

TournamentSpec players(int n) {
    TournamentSpec spec;
    spec.format = Format::RoundRobin;
    for (int i = 0; i < n; ++i) {
        Participant p;
        p.id          = 100 + i;
        p.engine.name = std::string(1, static_cast<char>('A' + i));
        spec.participants.push_back(p);
    }
    return spec;
}

// `count` games between a and b, colours alternating, with the given result for a: 1, 0.5 or 0.
void add(std::vector<GameResult>& results, int a, int b, double score_a, int count,
         fastchess::MatchTermination term = fastchess::MatchTermination::NORMAL) {
    for (int i = 0; i < count; ++i) {
        GameResult r;
        r.encounter        = static_cast<uint32_t>(results.size() / 2);
        r.game             = i % 2;
        r.white            = i % 2 == 0 ? a : b;
        r.black            = i % 2 == 0 ? b : a;
        const bool a_white = r.white == a;
        r.result           = score_a == 0.5 ? "1/2-1/2" : ((score_a == 1.0) == a_white ? "1-0" : "0-1");
        r.termination      = term;
        results.push_back(r);
    }
}

}  // namespace

TEST_CASE("ratings: two players, the logistic formula") {
    // 60 % over 1000 games: 400 log10(0.6 / 0.4) = 70.4 Elo apart.
    std::vector<std::vector<double>> games  = {{0, 1000}, {1000, 0}};
    std::vector<std::vector<double>> points = {{0, 600}, {400, 0}};

    const auto mean = maximumLikelihood(games, points);
    CHECK(mean[0].elo - mean[1].elo == doctest::Approx(70.44).epsilon(0.002));
    CHECK(mean[0].elo + mean[1].elo == doctest::Approx(0).epsilon(1e-6));
    CHECK(mean[0].error > 0);
    CHECK(mean[0].error == doctest::Approx(mean[1].error));

    const auto anchored = maximumLikelihood(games, points, std::make_pair(1, 3000.0));
    CHECK(anchored[1].elo == doctest::Approx(3000));
    CHECK(anchored[1].error == 0);
    CHECK(anchored[0].elo == doctest::Approx(3070.44).epsilon(0.0001));
    // Relative to the anchor, the uncertainty is the whole difference's, twice the one around the mean.
    CHECK(anchored[0].error == doctest::Approx(2 * mean[0].error).epsilon(0.01));
}

TEST_CASE("ratings: a perfect score stays finite, a cycle is level") {
    const auto perfect = maximumLikelihood({{0, 10}, {10, 0}}, {{0, 10}, {0, 0}});
    CHECK(std::isfinite(perfect[0].elo));
    CHECK(perfect[0].elo > 300);

    // A beats B, B beats C, C beats A, by the same margin.
    const auto cycle = maximumLikelihood({{0, 10, 10}, {10, 0, 10}, {10, 10, 0}}, {{0, 7, 3}, {3, 0, 7}, {7, 3, 0}});
    for (const auto& r : cycle) CHECK(r.elo == doctest::Approx(0).epsilon(1e-6));
}

TEST_CASE("table: points, crosstable, Sonneborn-Berger, tiebreaks and health") {
    const auto spec = players(4);
    std::vector<GameResult> results;
    // A: beats B and C, draws D.  B: beats C, loses to D (on time).  C: beats D.
    add(results, 100, 101, 1.0, 2);
    add(results, 100, 102, 1.0, 2);
    add(results, 100, 103, 0.5, 2);
    add(results, 101, 102, 1.0, 2);
    add(results, 101, 103, 0.0, 2, fastchess::MatchTermination::TIMEOUT);
    add(results, 102, 103, 1.0, 2);

    const auto table = computeTable(spec, results);
    REQUIRE(table.rows.size() == 4);

    CHECK(table.rows[0].name == "A");
    CHECK(table.rows[0].points == 5);
    CHECK(table.rows[0].wins == 4);
    CHECK(table.rows[0].draws == 2);
    CHECK(table.rows[0].games == 6);

    // D has 3 points; B and C have 2 each, and Sonneborn-Berger puts C (who beat D) before B (who beat C).
    CHECK(table.rows[1].name == "D");
    CHECK(table.rows[1].points == 3);
    CHECK(table.rows[2].name == "C");
    CHECK(table.rows[3].name == "B");
    CHECK(table.rows[2].points == table.rows[3].points);
    CHECK(table.rows[2].sb == doctest::Approx(2 * 3));
    CHECK(table.rows[3].sb == doctest::Approx(2 * 2));

    // D: two wins over B (on time), draws with A, losses to C.
    const auto d = std::find_if(table.rows.begin(), table.rows.end(), [](const Standing& s) { return s.name == "D"; });
    CHECK(d->sb == doctest::Approx(1 * 5 + 2 * 2));  // a point against A (5), two against B (2)

    const auto b = std::find_if(table.rows.begin(), table.rows.end(), [](const Standing& s) { return s.name == "B"; });
    CHECK(b->timeouts == 2);

    CHECK(table.pair(100, 101).wins == 2);
    CHECK(table.pair(101, 100).losses == 2);
    CHECK(table.pair(100, 103).draws == 2);
    CHECK(table.pair(100, 100).games() == 0);

    // The leader has the best rating; ratings average 0 without an anchor.
    double sum = 0;
    for (const auto& r : table.rows) sum += r.rating.elo;
    CHECK(sum == doctest::Approx(0).epsilon(1e-6));
    CHECK(table.rows[0].rating.elo > 0);

    const auto anchored = computeTable(spec, results, Anchor{103, 3500});
    const auto da =
        std::find_if(anchored.rows.begin(), anchored.rows.end(), [](const Standing& s) { return s.id == 103; });
    CHECK(da->rating.elo == doctest::Approx(3500));

    const auto text = tableText(table, "Test");
    CHECK(text.find("A") != std::string::npos);
    CHECK(text.find("5") != std::string::npos);
    const auto html = tableHtml(table, "Test <1>");
    CHECK(html.find("Test &lt;1&gt;") != std::string::npos);
    CHECK(html.find("prefers-color-scheme") != std::string::npos);
}

TEST_CASE("bbcode: title in bold, details, the text in a code block") {
    CHECK(bbcode("Cup", "Round robin, 10+0.1", "1  A\n") == "[b]Cup[/b]\nRound robin, 10+0.1\n[code]\n1  A\n[/code]\n");
    CHECK(bbcode("", "", "x") == "[code]\nx\n[/code]\n");
    CHECK(tableText(Table{}, "").rfind("  #", 0) == 0);

    auto spec = players(3);
    for (auto& p : spec.participants) {
        p.engine.limit.tc.time      = 10000;
        p.engine.limit.tc.increment = 100;
    }
    spec.conditions.opening.file = "/books/UHO.epd";
    std::vector<GameResult> results;
    add(results, 100, 101, 1.0, 2);
    CHECK(forumDetails(spec, computeTable(spec, results)) == "Round robin, 3 engines, 10+0.1, 2 games, book UHO.epd");
}

TEST_CASE("table: a head-to-head result breaks an otherwise complete tie") {
    const auto spec = players(2);
    std::vector<GameResult> results;
    add(results, 100, 101, 0.5, 2);
    auto table = computeTable(spec, results);
    CHECK(table.rows[0].points == table.rows[1].points);

    add(results, 101, 100, 1.0, 1);
    table = computeTable(spec, results);
    CHECK(table.rows[0].name == "B");
}

#include <filesystem>

#include <tour/ratinglist.hpp>

TEST_CASE("rating list: games of several tournaments, engines by name, filters and anchor") {
    namespace fs    = std::filesystem;
    const auto root = fs::temp_directory_path() / "fcg-tour-ratinglist";
    fs::remove_all(root);

    // Two tournaments sharing B, at two time controls.
    const auto play = [&](const std::string& folder, std::vector<std::string> names, int64_t time_ms, double a_score) {
        TournamentSpec spec;
        spec.format = Format::RoundRobin;
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            Participant p;
            p.id                        = i;
            p.engine.name               = names[static_cast<std::size_t>(i)];
            p.engine.limit.tc.time      = time_ms;
            p.engine.limit.tc.increment = 100;
            spec.participants.push_back(p);
        }
        auto state = TournamentState::create(root / folder, spec);
        while (auto task = state->claimNext()) {
            fastchess::MatchData d(chess::constants::STARTPOS);
            using chess::GameResult;
            const bool first_white = task->game == 0;
            const auto r  = a_score == 1.0 ? (first_white ? GameResult::WIN : GameResult::LOSE) : GameResult::DRAW;
            const auto o  = r == GameResult::WIN ? GameResult::LOSE : r == GameResult::LOSE ? GameResult::WIN : r;
            d.players     = {fastchess::MatchData::PlayerInfo(task->white, r),
                             fastchess::MatchData::PlayerInfo(task->black, o)};
            d.termination = fastchess::MatchTermination::NORMAL;
            state->complete(*task, d);
        }
    };
    play("1", {"A", "B"}, 10000, 1.0);  // the first engine of each encounter wins: A and B win one game each
    play("2", {"B", "C"}, 60000, 0.5);  // draws

    const std::vector<fs::path> dirs = {root / "1", root / "2", root / "missing"};
    const auto all                   = buildRatingList(dirs, "", std::nullopt);
    CHECK(all.tournaments == 2);
    CHECK(all.games == 4);
    REQUIRE(all.entries.size() == 3);
    CHECK(all.time_controls.size() == 2);
    const auto b = std::find_if(all.entries.begin(), all.entries.end(), [](const auto& e) { return e.name == "B"; });
    CHECK(b->games == 4);
    CHECK(b->tournaments == 2);

    const auto fast = buildRatingList(dirs, "10+0.1", std::make_pair(std::string("A"), 3000.0));
    CHECK(fast.tournaments == 1);
    REQUIRE(fast.entries.size() == 2);
    const auto a = std::find_if(fast.entries.begin(), fast.entries.end(), [](const auto& e) { return e.name == "A"; });
    CHECK(a->rating.elo == doctest::Approx(3000));

    CHECK(ratingListText(all).find("B") != std::string::npos);
    CHECK(ratingListCsv(all).rfind("rank,engine", 0) == 0);
}
