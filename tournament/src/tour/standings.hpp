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

    // A bye is a drawn pair of games: one point out of two, so that it does not raise the score above 50 %.
    [[nodiscard]] double score() const noexcept {
        const int counted = games + 2 * byes;
        return counted ? points / counted : 0.0;
    }
};

// A participant whose rating is fixed, the others being measured against it.
struct Anchor {
    int id     = 0;
    double elo = 0;
};

struct Table {
    // Ranked: points, then Sonneborn-Berger (a Swiss: Buchholz first), then within a tie the points scored against
    // the others tied (head to head), then wins.
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

// The games of two players a and b, each pair once.
struct PairGames {
    std::size_t a = 0, b = 0;
    double games    = 0;
    double points_a = 0;  // what a scored
};

// The same for n players from the pairs that met, in time and memory that grow with the pairs rather than n²: a
// rating list of thousands of engines. Errors are exact up to 1000 players, then approximated from each player's
// games and its opponents'.
[[nodiscard]] std::vector<Rating> maximumLikelihood(std::size_t n, const std::vector<PairGames>& pairs,
                                                    std::optional<std::pair<int, double>> anchor = std::nullopt);

// The table as plain text, for the clipboard or a forum post; an empty title is left out.
[[nodiscard]] std::string tableText(const Table& table, const std::string& title);

// A forum post in BBCode: the title in bold, a line of details, then `text` in a code block, so that its columns
// stay aligned on any forum (phpBB, Talkchess). An empty title or details line is left out.
[[nodiscard]] std::string bbcode(const std::string& title, const std::string& details, const std::string& text);

// One line describing the tournament under its standings in a forum post: format, engines, time control, games
// played, book. In English, like the table.
[[nodiscard]] std::string forumDetails(const TournamentSpec& spec, const Table& table);

// A self-contained HTML page with the standings and the crosstable.
[[nodiscard]] std::string tableHtml(const Table& table, const std::string& title);

}  // namespace fcg::tour
