#pragma once

#include <cstdint>
#include <limits>
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

// The numbers of fishtest's statistics page for a match (server/fishtest/http/template_helpers.py), from the game
// counts and from the pair counts. Intervals at 95 %; NaN where undefined (no game, no variance yet).
struct Interval {
    double value = std::numeric_limits<double>::quiet_NaN();
    double low   = std::numeric_limits<double>::quiet_NaN();
    double high  = std::numeric_limits<double>::quiet_NaN();
};
struct MatchStatistics {
    struct Common {
        uint64_t count    = 0;  // games, or pairs
        double draw_ratio = 0;  // draws, or pairs scoring 1 (a win and a loss, or two draws)
        Interval score;         // per game, 0 to 1
        Interval elo;           // logistic
        Interval nelo;
        double stdev = std::numeric_limits<double>::quiet_NaN();  // of one game's score
        double llr   = std::numeric_limits<double>::quiet_NaN();  // with the match's SPRT
    };
    Common games;
    Common pairs;
    double bayeselo = std::numeric_limits<double>::quiet_NaN();  // and the DrawElo that goes with it
    double drawelo  = std::numeric_limits<double>::quiet_NaN();
    // Pairs only.
    double two_draws      = 0;                                         // share of the pairs of two draws
    double win_and_loss   = 0;                                         // share of the pairs of a win and a loss
    double pairs_ratio    = std::numeric_limits<double>::quiet_NaN();  // pairs won / pairs lost
    double variance_ratio = std::numeric_limits<double>::quiet_NaN();  // per game variance, pairs / games
    double rms_bias       = std::numeric_limits<double>::quiet_NaN();  // Elo spread due to the openings
};

[[nodiscard]] MatchStatistics matchStatistics(const fastchess::Stats& stats, const fastchess::SPRT* sprt);

// MatchStatistics as text lines, for the copied summary.
[[nodiscard]] std::string statisticsText(const MatchStatistics& m);

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
