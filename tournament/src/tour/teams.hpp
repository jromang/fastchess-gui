#pragma once

// The standings of a team match: each team's score, the Elo difference it stands for, and the crosstable of team A's
// engines against team B's. The engines' own standings and ratings are computeTable()'s.

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <matchmaking/stats.hpp>

#include "model.hpp"
#include "standings.hpp"
#include "state.hpp"

namespace fcg::tour {

struct TeamMember {
    int id = 0;
    std::string name;
    bool active = true;  // withdrawn or replaced: its games that count still count for the team
};

struct TeamScore {
    std::string name;
    std::vector<TeamMember> members;  // in the list's order: board order
    int games     = 0;
    int wins      = 0;
    int draws     = 0;
    int losses    = 0;
    double points = 0;

    [[nodiscard]] double score() const noexcept { return games ? points / games : 0.0; }
};

struct TeamTable {
    std::array<TeamScore, 2> teams;

    // Team A against team B: wins, draws and losses of every game that counts, and the pentanomial counts of the
    // encounters both games of which count, as fastchess counts a match.
    fastchess::Stats stats;

    // Team A's Elo over team B that the score stands for, the half-width of its 95 % interval and the likelihood of
    // superiority (0 to 1): from the pairs of games when there are some, as fastchess reports a match, else from the
    // games. Unset without games, and while the results do not vary (no interval).
    std::optional<double> elo;
    std::optional<double> error;
    std::optional<double> los;

    // The games of an engine of one team against an engine of the other, from the first one's point of view.
    std::map<std::pair<int, int>, PairRecord> pairs;

    [[nodiscard]] PairRecord pair(int a, int b) const;
    // The team of participant `id`: 0, 1, or -1 when it is in neither.
    [[nodiscard]] int teamOf(int id) const;
};

[[nodiscard]] TeamTable computeTeams(const TournamentSpec& spec, const std::vector<GameResult>& results);

// One line, in English like the copies: "Team A 12½ - 7½ Team B · 62.5 % · Elo +88 ± 40".
[[nodiscard]] std::string teamScoreLine(const TeamTable& teams);

// The teams' scores, then each team's engines with their scores and ratings (`engines`, computeTable()'s) and their
// points against each engine of the other team, as plain text for the clipboard or a forum post; an empty title or
// footer is left out.
[[nodiscard]] std::string teamText(const TeamTable& teams, const Table& engines, const std::string& title,
                                   const std::string& footer = "");

// The same as a self-contained HTML page, in tableHtml()'s style.
[[nodiscard]] std::string teamHtml(const TeamTable& teams, const Table& engines, const std::string& title,
                                   const std::string& footer = "");

}  // namespace fcg::tour
