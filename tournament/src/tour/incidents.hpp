#pragma once

// The games of recent tournaments that ended badly: a crash or a hang, a loss on time, an illegal move. Read from
// the journals, and only from what they gained since the last look, so that a health panel can ask often.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <utility>
#include <vector>

#include <types/match_data.hpp>

namespace fcg::tour {

struct Incidents {
    int crashes       = 0;  // an engine disconnected (crashed) or stopped responding
    int time_losses   = 0;
    int illegal_moves = 0;

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
    struct Journal {
        std::uintmax_t read = 0;                                             // bytes read: up to the last complete line
        std::vector<std::pair<int64_t, fastchess::MatchTermination>> found;  // (finished at, how)
    };
    std::map<std::filesystem::path, Journal> journals_;
};

}  // namespace fcg::tour
