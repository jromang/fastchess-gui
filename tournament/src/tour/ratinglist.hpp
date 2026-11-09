#pragma once

// A rating list built from every tournament: all the games that count, engines known by name, ratings by maximum
// likelihood. Read only; tournaments that are running can be included.

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <types/engine_config.hpp>

#include "standings.hpp"

namespace fcg::tour {

struct RatingEntry {
    std::string name;
    int games       = 0;
    double points   = 0;
    int tournaments = 0;
    Rating rating;

    [[nodiscard]] double score() const noexcept { return games ? points / games : 0.0; }
};

struct RatingList {
    std::vector<RatingEntry> entries;        // best first
    std::vector<std::string> time_controls;  // every time control met, to filter by
    int tournaments = 0;                     // tournaments counted
    uint64_t games  = 0;
};

// "10+0.1", "40/900+0", "5000 nodes": how a time control is shown and filtered.
[[nodiscard]] std::string timeControlText(const fastchess::Limit& limit);

// The games of the tournaments in `dirs` (folders that are not tournaments are skipped). `time_control`: only the
// tournaments played at that time control, empty for all. `anchor`: an engine name and its rating; without it the
// ratings average 0.
[[nodiscard]] RatingList buildRatingList(const std::vector<std::filesystem::path>& dirs,
                                         const std::string& time_control,
                                         const std::optional<std::pair<std::string, double>>& anchor);

// The list as text, for the clipboard; and as CSV.
[[nodiscard]] std::string ratingListText(const RatingList& list);
[[nodiscard]] std::string ratingListCsv(const RatingList& list);

}  // namespace fcg::tour
