#pragma once

// What the tournament adds to fastchess' SPRT: an Elo bias, which moves both bounds (to test against a version known
// to be stronger or weaker by that much), and a minimum number of games before a decision, which guards a short test
// against an early lucky stop. The tournament layer decides when a match ends (TournamentState::checkSprt()), so the
// minimum needs nothing of fastchess; the LLR is computed and shown all along.

#include <cstdint>
#include <string>

#include <matchmaking/sprt/sprt.hpp>

namespace fcg::tour {

// The bounds the SPRT is given: the tester's, both moved by the bias.
struct SprtBounds {
    double elo0 = 0;
    double elo1 = 0;
};
[[nodiscard]] SprtBounds biasedBounds(double elo0, double elo1, double bias) noexcept;

// The tester's bounds back from those the SPRT was given.
[[nodiscard]] SprtBounds unbiasedBounds(const SprtBounds& given, double bias) noexcept;

// What the SPRT says after `games` games whose LLR is `llr`: its verdict once there are at least `min_games` (0: no
// minimum), SPRT_CONTINUE before, however far the LLR is beyond a bound.
[[nodiscard]] fastchess::SPRTResult sprtDecision(const fastchess::SPRT& sprt, double llr, uint64_t games,
                                                 uint64_t min_games);

// The test in a few English words, for the console: "SPRT [0, 3]", "SPRT [0, 3] + 5, no decision before 2000 games".
[[nodiscard]] std::string sprtText(double elo0, double elo1, double bias, uint64_t min_games);

}  // namespace fcg::tour
