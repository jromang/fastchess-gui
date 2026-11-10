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

// What an SPRT is likely to do, from fishtest's SPRT calculator (http://hardy.uhasselt.be/Fishtest/sprta.pdf): the
// log-likelihood ratio taken as a Brownian motion with drift between its two bounds.
struct SprtPlan {
    bool valid  = false;  // false when the draw ratio and the RMS bias are not compatible
    double elo0 = 0;      // the bounds, in logistic Elo
    double elo1 = 0;

    struct Outcome {
        double pass  = 0;  // the probability that H1 is accepted
        double games = 0;  // the expected number of games
    };
    // Against a true difference of `elo`, in logistic Elo.
    [[nodiscard]] Outcome at(double elo) const;

    double score0 = 0, score1 = 0, variance = 0, la = 0, lb = 0;
};

// `model` is fastchess': "normalized", "logistic" or "bayesian". The draw ratio and the RMS bias of the openings
// (in Elo) give the variance of a game; the RMS bias does not apply to the bayesian model, whose likelihood is
// computed from single games.
[[nodiscard]] SprtPlan sprtPlan(double alpha, double beta, double elo0, double elo1, const std::string& model,
                                double draw_ratio, double rms_bias);

// "+4.2 ±3.1" style helpers.
[[nodiscard]] std::string formatElo(double elo, double err);

}  // namespace fcg
