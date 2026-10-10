#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <matchmaking/sprt/sprt.hpp>
#include <matchmaking/stats.hpp>

namespace fcg {

// The numbers shown for a head to head score, from engine A's point of view.
// Computed with fastchess' own classes, so they match its console output.
struct StatPoint {
    uint32_t games  = 0;
    double elo      = 0;
    double elo_err  = 0;
    double nelo     = 0;
    double nelo_err = 0;
    double los      = 0;  // percent
    double llr      = 0;
    bool valid      = false;  // false until the variance is defined
};

[[nodiscard]] StatPoint computeStats(const fastchess::Stats& stats, bool penta, const fastchess::SPRT* sprt);

// "+4.2 ±3.1" style helpers.
[[nodiscard]] std::string formatElo(double elo, double err);

}  // namespace fcg
