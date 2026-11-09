#pragma once

// Standings, crosstable and ratings of a tournament, computed from its results.

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model.hpp"
#include "state.hpp"

namespace fcg::tour {

// Games between two participants, from the first one's point of view.
struct PairRecord {
    int wins   = 0;
    int draws  = 0;
    int losses = 0;

    [[nodiscard]] int games() const noexcept { return wins + draws + losses; }
    [[nodiscard]] double points() const noexcept { return wins + 0.5 * draws; }
};

struct Rating {
    double elo   = 0;
    double error = 0;  // half-width of the 95 % confidence interval; 0 for the anchor
};

struct Standing {
    int id = 0;
    std::string name;

    int games       = 0;
    int wins        = 0;
    int draws       = 0;
    int losses      = 0;
    double points   = 0;
    double sb       = 0;  // Sonneborn-Berger: for each opponent, points scored against it times its total
    double buchholz = 0;  // the sum of the opponents' totals, one per game played against them
    int byes        = 0;  // Swiss: rounds without an opponent, a point each (in `points`)

    Rating rating;
    bool rated = false;  // has played at least one game

    // Games lost this way.
    int timeouts = 0;
    int crashes  = 0;
    int illegal  = 0;

    [[nodiscard]] double score() const noexcept { return games ? points / games : 0.0; }
};

// A participant whose rating is fixed, the others being measured against it.
struct Anchor {
    int id     = 0;
    double elo = 0;
};

struct Table {
    // Ranked: points, then Sonneborn-Berger, head-to-head and wins; a Swiss uses Buchholz before Sonneborn-Berger.
    std::vector<Standing> rows;

    // Record of `a` against `b`, from a's point of view.
    [[nodiscard]] PairRecord pair(int a, int b) const;

    std::map<std::pair<int, int>, PairRecord> pairs;
};

// Without an anchor, ratings are relative to the average of the participants. `byes`: Swiss byes by id.
[[nodiscard]] Table computeTable(const TournamentSpec& spec, const std::vector<GameResult>& results,
                                 std::optional<Anchor> anchor = std::nullopt, const std::map<int, int>& byes = {});

// Maximum likelihood ratings of n players on the logistic Elo model, a draw counting as half a win (like Ordo by
// default). points[i][j] is what i scored against j in games[i][j] games. A very wide prior (sigma 1000 Elo) keeps
// a 100 % score finite. Without an anchor the ratings average 0; the errors are those of each rating relative to
// that average (or to the anchor), from the inverse of the Fisher information.
[[nodiscard]] std::vector<Rating> maximumLikelihood(const std::vector<std::vector<double>>& games,
                                                    const std::vector<std::vector<double>>& points,
                                                    std::optional<std::pair<int, double>> anchor = std::nullopt);

// The table as plain text, for the clipboard or a forum post.
[[nodiscard]] std::string tableText(const Table& table, const std::string& title);

// A self-contained HTML page with the standings and the crosstable.
[[nodiscard]] std::string tableHtml(const Table& table, const std::string& title);

}  // namespace fcg::tour
