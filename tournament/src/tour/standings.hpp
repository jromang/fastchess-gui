#pragma once

// Standings, crosstable and ratings of a tournament, computed from its results.

#include <functional>
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
    int white  = 0;  // games the first one played with White
    // Encounters (a game and the one with colours reversed) of which one game only counts so far: played while the
    // other one was not, or the other one voided.
    int unpaired = 0;

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

// The ratings of maximumLikelihood() for a rating list, with what the error of the difference between two of them
// needs: their covariance, exact up to 1000 players, then approximated from each one's information and the games the
// two played together (`exact` false).
struct RatingSolution {
    std::vector<Rating> ratings;
    bool exact = true;
    std::function<double(std::size_t, std::size_t)> difference_error;

    // The half-width of the 95 % interval of ratings[a].elo - ratings[b].elo.
    [[nodiscard]] double differenceError(std::size_t a, std::size_t b) const;
};
[[nodiscard]] RatingSolution ratingSolution(std::size_t n, const std::vector<PairGames>& pairs,
                                            std::optional<std::pair<int, double>> anchor = std::nullopt);

// The likelihood of superiority, 0 to 1, of a rating `difference` known within `error` (the half-width of its 95 %
// interval): the probability that the difference is above 0 on the normal law, as fastchess' LOS of a match.
[[nodiscard]] double likelihoodOfSuperiority(double difference, double error);

// The orders a table can be shown in.
enum class Order {
    Points,  // the ranking of computeTable()
    Score,   // points per game (a Swiss bye counts as a drawn pair), the ranking breaking ties
    Seed,    // as the participants were entered, the seeds of a gauntlet first: its crosstable columns stay in place
};

// `table` with its rows in `order`.
[[nodiscard]] Table ordered(Table table, const TournamentSpec& spec, Order order);

// The table as plain text, for the clipboard or a forum post; an empty title or footer is left out, the footer
// (footerLine()) goes under the table after a blank line.
[[nodiscard]] std::string tableText(const Table& table, const std::string& title, const std::string& footer = "");

// A forum post in BBCode: the title in bold, a line of details, then `text` in a code block, so that its columns
// stay aligned on any forum (phpBB, Talkchess). An empty title or details line is left out.
[[nodiscard]] std::string bbcode(const std::string& title, const std::string& details, const std::string& text);

// One line describing the tournament under its standings in a forum post: format, engines, time control, games
// played, book. In English, like the table.
[[nodiscard]] std::string forumDetails(const TournamentSpec& spec, const Table& table);

// `s` with &, <, > and " as HTML entities.
[[nodiscard]] std::string escapeHtml(const std::string& s);

// The start of a self-contained HTML page, light or dark as the reader's system: its head and style, the body open
// with `title` as its heading. The pages of the standings and of the rating list.
[[nodiscard]] std::string htmlPageStart(const std::string& title);

// A self-contained HTML page with the standings and the crosstable, and the footer under them when there is one.
[[nodiscard]] std::string tableHtml(const Table& table, const std::string& title, const std::string& footer = "");

// The rating difference a score stands for, -400 log10(1 / score - 1), and the half-width of its 95 % interval from
// the spread of the results. Neither for a score of 0 or 100 %, nor without games.
struct ScoreElo {
    std::optional<double> elo;
    std::optional<double> error;
};
[[nodiscard]] ScoreElo scoreElo(int wins, int draws, int losses);

// A gauntlet as Gabor Szots posts it on the forums (TorsGUI's layout, asked for by him): for each seed, the seed
// with its whole score, then its opponents by their own points against it, each row from its own point of view:
// Points, %, the Elo difference its score stands for with its 95 % margin, and W-L-D (written +W-L=D), under a rule
// of = (other GUIs draw it with -, Gabor asked that ours be told apart). Then the footer (footerLine()) after a
// blank line. The seeds are the first spec.seeds participants. An empty title or footer is left out.
[[nodiscard]] std::string gauntletText(const TournamentSpec& spec, const Table& table, const std::string& title,
                                       const std::string& footer = "");

// The line under a table or a gauntlet's table, as Gabor Szots writes it: "480/480 games, 30% draws, time
// control:90+1, book:UHO.epd, event: Cup", the games played of those planned (when known), the time control of the
// first participant, the book and the event (the tournament's name), the last two only when there is one.
[[nodiscard]] std::string footerLine(const TournamentSpec& spec, const Table& table, std::optional<uint64_t> planned);

}  // namespace fcg::tour
