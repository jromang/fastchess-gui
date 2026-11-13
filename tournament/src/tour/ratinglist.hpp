#pragma once

// A rating list built from every tournament: all the games that count, engines known by name, ratings by maximum
// likelihood. Read only; tournaments that are running can be included.

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <types/engine_config.hpp>

#include "game_db.hpp"
#include "standings.hpp"

namespace fcg::tour {

struct RatingEntry {
    std::string name;
    int games       = 0;
    int wins        = 0;
    int draws       = 0;
    int losses      = 0;
    double points   = 0;
    int tournaments = 0;
    Rating rating;
    double opponents = 0;  // the average rating of the opponents met, game by game, as the list rates them
    // Over the next engine of the list, none for the last: the rating difference, and the likelihood of superiority
    // (0 to 1) from the covariance of the two ratings.
    std::optional<double> gap;
    std::optional<double> los;

    [[nodiscard]] double score() const noexcept { return games ? points / games : 0.0; }
    [[nodiscard]] double drawRate() const noexcept { return games ? static_cast<double>(draws) / games : 0.0; }
};

struct RatingList {
    std::vector<RatingEntry> entries;        // best first
    std::vector<std::string> time_controls;  // every time control met, to filter by
    int tournaments = 0;                     // tournaments counted
    uint64_t games  = 0;
    // The LOS from the exact covariance of the ratings; false beyond 1000 engines, where it is approximated.
    bool exact_los = true;
};

// "10+0.1", "40/900+0", "5000 nodes": how a time control is shown and filtered.
[[nodiscard]] std::string timeControlText(const fastchess::Limit& limit);

using NamedAnchor = std::optional<std::pair<std::string, double>>;  // an engine name and its rating

// The games of `db` that `filter` accepts. Without an anchor the ratings average 0. Engines with fewer than
// `min_games` games are left out of the list (their games still count for their opponents).
[[nodiscard]] RatingList ratingList(const GameDb& db, const GameFilter& filter, const NamedAnchor& anchor,
                                    int min_games = 0);

// The tournaments in `dirs` at `time_control` (empty for all).
[[nodiscard]] RatingList buildRatingList(const std::vector<std::filesystem::path>& dirs,
                                         const std::string& time_control, const NamedAnchor& anchor);

// An engine's rating after each tournament (or imported file) it played in, in the order they were played: the
// list computed on the games up to that one.
struct RatingPoint {
    int64_t time = 0;  // when the last game counted was played
    std::string source;
    Rating rating;
    int games = 0;
};
[[nodiscard]] std::vector<RatingPoint> ratingHistory(const GameDb& db, const GameFilter& filter,
                                                     const NamedAnchor& anchor, const std::string& engine);

// The games accepted, as a PGN of headers only (White, Black, Result, Date, TimeControl), which Ordo and
// BayesElo read.
[[nodiscard]] std::string ratingPgn(const GameDb& db, const GameFilter& filter);

// The list as text, for the clipboard; and as CSV.
[[nodiscard]] std::string ratingListText(const RatingList& list);
[[nodiscard]] std::string ratingListCsv(const RatingList& list);

}  // namespace fcg::tour
