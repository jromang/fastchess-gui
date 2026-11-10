#include "stats.hpp"

#include <cmath>
#include <string>

#include <fmt/include/fmt/format.h>
#include <matchmaking/elo/elo_pentanomial.hpp>
#include <matchmaking/elo/elo_wdl.hpp>

namespace fcg {

namespace {

bool finite(double v) { return std::isfinite(v); }

template <typename Elo>
void fill(const Elo& elo, StatPoint& p) {
    p.elo      = elo.diff();
    p.elo_err  = elo.error();
    p.nelo     = elo.nEloDiff();
    p.nelo_err = elo.nEloError();

    // los() is only exposed formatted, as "97.21 %".
    try {
        p.los = std::stod(elo.los());
    } catch (...) {
        p.los = 0;
    }

    p.valid = finite(p.elo) && finite(p.elo_err) && finite(p.nelo) && finite(p.nelo_err);
}

}  // namespace

StatPoint computeStats(const fastchess::Stats& stats, bool penta, const fastchess::SPRT* sprt) {
    StatPoint p;
    p.games = static_cast<uint32_t>(stats.sum());
    if (p.games == 0) return p;

    const bool use_penta = penta && stats.totalPairs() > 0;

    // Both Elo classes divide by the variance, which is zero until results differ.
    if (use_penta) {
        fill(fastchess::elo::EloPentanomial(stats), p);
    } else {
        fill(fastchess::elo::EloWDL(stats), p);
    }

    if (sprt && sprt->isEnabled()) p.llr = sprt->getLLR(stats, use_penta);
    if (!finite(p.llr)) p.llr = 0;

    return p;
}

namespace {

double logistic(double elo) { return 1.0 / (1.0 + std::pow(10.0, -elo / 400.0)); }
double logisticInverse(double score) { return -400.0 * std::log10(1.0 / score - 1.0); }

}  // namespace

SprtPlan sprtPlan(double alpha, double beta, double elo0, double elo1, const std::string& model, double draw_ratio,
                  double rms_bias) {
    SprtPlan plan;
    const bool bayesian = model == "bayesian";
    const double bias   = bayesian ? 0.0 : logistic(rms_bias) - 0.5;
    plan.variance       = (1.0 - draw_ratio) / 4.0 - bias * bias;
    if (!(plan.variance > 0) || elo1 <= elo0 || draw_ratio <= 0 || draw_ratio >= 1) return plan;

    if (model == "normalized") {
        constexpr double NELO_PER_NT = 800.0 / 2.302585092994046;  // 800 / ln 10
        const double sigma           = std::sqrt(plan.variance);
        plan.score0                  = elo0 / NELO_PER_NT * sigma + 0.5;
        plan.score1                  = elo1 / NELO_PER_NT * sigma + 0.5;
    } else if (bayesian) {
        // The draw Elo that gives this draw ratio between equal engines.
        const double drawelo = 400.0 * std::log10((1.0 + draw_ratio) / (1.0 - draw_ratio));
        plan.score0          = fastchess::SPRT::bayeseloToScore(elo0, drawelo);
        plan.score1          = fastchess::SPRT::bayeseloToScore(elo1, drawelo);
    } else {
        plan.score0 = logistic(elo0);
        plan.score1 = logistic(elo1);
    }
    plan.elo0  = logisticInverse(plan.score0);
    plan.elo1  = logisticInverse(plan.score1);
    plan.la    = std::log(beta / (1.0 - alpha));
    plan.lb    = std::log((1.0 - beta) / alpha);
    plan.valid = std::isfinite(plan.elo0) && std::isfinite(plan.elo1) && plan.score1 > plan.score0;
    return plan;
}

SprtPlan::Outcome SprtPlan::at(double elo) const {
    if (!valid) return {};
    const double score = logistic(elo);
    const double h     = (2.0 * score - (score0 + score1)) / (score1 - score0);
    const double w2    = (score1 - score0) * (score1 - score0) / variance;

    Outcome out;
    double time = 0;
    if (std::abs(h * (la - lb)) < 1e-6) {
        out.pass = -la / (lb - la);
        time     = -la * lb;
    } else {
        const double exp_a = std::exp(-h * la);
        const double exp_b = std::exp(-h * lb);
        out.pass           = (1.0 - exp_a) / (exp_b - exp_a);
        time               = 2.0 / h * (lb * out.pass + la * (1.0 - out.pass));
    }
    out.games = time / w2;
    return out;
}

std::string formatElo(double elo, double err) {
    if (!finite(elo)) return "—";
    if (!finite(err)) return fmt::format("{:+.1f}", elo);
    return fmt::format("{:+.1f} ±{:.1f}", elo, err);
}

}  // namespace fcg
