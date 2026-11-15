#pragma once

// The games of recent tournaments that ended badly: a crash or a hang, a loss on time, an illegal move. Read from
// the journals, and only from what they gained since the last look, so that a health panel can ask often.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <types/match_data.hpp>

namespace fcg::tour {

// A loss on time within this many plies (the opening included) is a loss in the first moves: more often an engine
// set up wrong (too many threads, a hash too large to allocate in time, a network loaded at every game, a start too
// slow) than its time management.
inline constexpr uint32_t EARLY_TIME_LOSS_PLIES = 40;

// The incidents of the engine that lost those games, as its tournaments name it.
struct EngineIncidents {
    std::string engine;
    int crashes           = 0;
    int time_losses       = 0;
    int early_time_losses = 0;  // of time_losses, those within EARLY_TIME_LOSS_PLIES
    int illegal_moves     = 0;
    std::filesystem::path dir;  // the tournament of its last incident: its logs/ folder keeps the engine logs

    [[nodiscard]] int total() const noexcept { return crashes + time_losses + illegal_moves; }
};

struct Incidents {
    int crashes       = 0;  // an engine disconnected (crashed) or stopped responding
    int time_losses   = 0;
    int illegal_moves = 0;
    // Games an engine on a machine lost to the network (its connection broke), voided and played again: not counted
    // above, nor against the engine.
    int network_cuts = 0;
    // By engine, the most incidents first (by name at the same count). A game whose loser is not known (a draw by
    // a timeout, an old journal) counts above only.
    std::vector<EngineIncidents> engines;

    [[nodiscard]] int total() const noexcept { return crashes + time_losses + illegal_moves; }
};

// Not thread safe: one scan at a time.
class IncidentScanner {
   public:
    // The incidents of the games finished at `since_ms` (unix time, milliseconds) or later, in the tournament
    // folders of `root` whose journal was written in the last `window` (journals untouched for longer are not
    // opened).
    [[nodiscard]] Incidents scan(const std::filesystem::path& root, int64_t since_ms,
                                 std::chrono::seconds window = std::chrono::hours(24));

   private:
    struct Found {
        int64_t at = 0;  // finished at
        fastchess::MatchTermination how{};
        int loser       = -1;  // participant id, -1 when not known
        uint32_t plies  = 0;
        uint64_t number = 0;  // GameTask::number()
        bool network    = false;  // voided since for its machine's broken connection
    };
    struct Journal {
        std::uintmax_t read = 0;  // bytes read: up to the last complete line
        std::vector<Found> found;
        std::map<int, std::string> names;  // participant id -> name: tournament.json's, and those added since
        bool named = false;                // tournament.json read
    };
    std::map<std::filesystem::path, Journal> journals_;
};

}  // namespace fcg::tour
