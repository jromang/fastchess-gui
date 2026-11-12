#pragma once

// A quick look at a tournament folder, without reading its opening book or replaying its games: enough to list
// tournaments and offer to resume them.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "model.hpp"

namespace fcg::tour {

struct Summary {
    std::filesystem::path dir;
    TournamentSpec spec;
    uint64_t played = 0;            // games recorded in the journal
    std::optional<uint64_t> total;  // nullopt for a match without a game limit
    bool finished = false;
    std::string finish_reason;
};

// nullopt when `dir` holds no readable tournament.
[[nodiscard]] std::optional<Summary> summarize(const std::filesystem::path& dir);

// The tournament folders directly inside `root`, newest name first (folder names start with a timestamp).
[[nodiscard]] std::vector<std::filesystem::path> tournamentDirs(const std::filesystem::path& root);

// summarize() of each of tournamentDirs(root).
[[nodiscard]] std::vector<Summary> listTournaments(const std::filesystem::path& root);

// listTournaments() that keeps the summaries between two calls and reads a tournament again only when its
// journal or its tournament.json changed (size or time): a list of hundreds of tournaments, most of them over.
// Not thread safe: one thread at a time.
class SummaryCache {
   public:
    [[nodiscard]] std::vector<Summary> list(const std::filesystem::path& root);

   private:
    struct Entry {
        std::uintmax_t journal_size = 0;
        std::filesystem::file_time_type journal_time, spec_time;
        std::optional<Summary> summary;
    };
    std::map<std::filesystem::path, Entry> entries_;
};

}  // namespace fcg::tour
