#include "stats.hpp"

#include <algorithm>
#include <array>
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

namespace {

constexpr double Z975        = 1.959963984540054;  // the 97.5 % quantile of the normal law
constexpr double NELO_PER_NT = 800.0 / 2.302585092994046;

double scoreElo(double score) {
    score = std::clamp(score, 1e-3, 1 - 1e-3);
    return -400.0 * std::log10(1.0 / score - 1.0);
}

// fishtest's figures for a distribution of results: counts of the scores 0, 1/(n-1), ..., 1, each count raised to
// 1e-3 when zero (LLRcalc.regularize). `nt_scale` turns the t-value into the normalized Elo's: 1 for games,
// 1/sqrt(2) for pairs.
template <std::size_t N>
MatchStatistics::Common distribution(const std::array<double, N>& counts, double nt_scale) {
    MatchStatistics::Common c;
    std::array<double, N> r = counts;
    double n                = 0;
    for (auto& x : r) {
        if (x == 0) x = 1e-3;
        n += x;
    }
    double mean = 0, var = 0, m3 = 0, m4 = 0;
    for (std::size_t i = 0; i < N; ++i) mean += r[i] / n * static_cast<double>(i) / (N - 1);
    for (std::size_t i = 0; i < N; ++i) {
        const double d = static_cast<double>(i) / (N - 1) - mean, p = r[i] / n;
        var += p * d * d;
        m3 += p * d * d * d;
        m4 += p * d * d * d * d;
    }
    if (!(var > 0)) return c;
    const double skewness = m3 / std::pow(var, 1.5), exkurt = m4 / (var * var) - 3;

    const double half = Z975 * std::sqrt(var / n);
    c.score           = {mean, mean - half, mean + half};
    c.elo             = {scoreElo(mean), scoreElo(mean - half), scoreElo(mean + half)};

    const double t     = (mean - 0.5) / std::sqrt(var);
    const double var_t = std::max(1 - t * skewness + 0.25 * t * t * (exkurt + 2), 0.0);
    const double t_err = Z975 * std::sqrt(var_t / n);
    const double k     = NELO_PER_NT * nt_scale;
    c.nelo             = {k * t, k * (t - t_err), k * (t + t_err)};
    // A pair's score is the mean of two games: its variance is half a game's, were they independent.
    c.stdev = std::sqrt(N == 3 ? var : 2 * var);
    return c;
}

}  // namespace

MatchStatistics matchStatistics(const fastchess::Stats& s, const fastchess::SPRT* sprt) {
    MatchStatistics m;
    const double ll = s.penta_LL, ld = s.penta_LD, balanced = s.penta_WL + s.penta_DD, wd = s.penta_WD, ww = s.penta_WW;

    const auto games = static_cast<uint64_t>(s.wins + s.draws + s.losses);
    const auto pairs = static_cast<uint64_t>(ll + ld + balanced + wd + ww);
    if (games) m.games = distribution<3>({double(s.losses), double(s.draws), double(s.wins)}, 1.0);
    if (pairs) m.pairs = distribution<5>({ll, ld, balanced, wd, ww}, 1.0 / std::sqrt(2.0));
    m.games.count      = games;
    m.games.draw_ratio = games ? double(s.draws) / games : 0;
    m.pairs.count      = pairs;
    if (m.pairs.count) {
        m.pairs.draw_ratio = balanced / m.pairs.count;
        m.two_draws        = s.penta_DD / static_cast<double>(m.pairs.count);
        m.win_and_loss     = s.penta_WL / static_cast<double>(m.pairs.count);
    }
    if (ll + ld > 0) m.pairs_ratio = (wd + ww) / (ll + ld);

    // BayesElo and DrawElo from the regularized game counts.
    if (m.games.count) {
        const double w = std::max<double>(s.wins, 1e-3), l = std::max<double>(s.losses, 1e-3),
                     d = std::max<double>(s.draws, 1e-3), n = w + d + l;
        const double pw = w / n, pl = l / n;
        m.bayeselo = 200 * std::log10(pw / pl * (1 - pl) / (1 - pw));
        m.drawelo  = 200 * std::log10((1 - pl) / pl * (1 - pw) / pw);
    }

    // How much playing in pairs reduced the variance, and the opening bias it reveals.
    if (std::isfinite(m.games.stdev) && std::isfinite(m.pairs.stdev) && m.games.stdev > 0) {
        const double var3 = m.games.stdev * m.games.stdev, var5 = m.pairs.stdev * m.pairs.stdev;
        m.variance_ratio = var5 / var3;
        m.rms_bias       = std::abs(scoreElo(0.5 + std::sqrt(std::max(var3 - var5, 0.0))));
    }

    if (sprt && sprt->isEnabled()) {
        if (m.games.count) m.games.llr = sprt->getLLR(s, false);
        if (m.pairs.count) m.pairs.llr = sprt->getLLR(s, true);
    }
    return m;
}

std::string statisticsText(const MatchStatistics& m) {
    const auto interval = [](const Interval& i, const char* format) {
        if (!std::isfinite(i.value)) return std::string("-");
        return fmt::format(fmt::runtime(fmt::format("{0} [{0}, {0}]", format)), i.value, i.low, i.high);
    };
    std::string out;
    for (const auto* c : {&m.games, &m.pairs}) {
        const bool games = c == &m.games;
        out += fmt::format("{}: {}, {} {:.2f} %, score {}, Elo {}, nElo {}, stdev/game {:.4f}",
                           games ? "Games" : "Pairs", c->count, games ? "draws" : "balanced", 100 * c->draw_ratio,
                           interval({100 * c->score.value, 100 * c->score.low, 100 * c->score.high}, "{:.2f}"),
                           interval(c->elo, "{:.2f}"), interval(c->nelo, "{:.2f}"), c->stdev);
        if (std::isfinite(c->llr)) out += fmt::format(", LLR {:.2f}", c->llr);
        out += "\n";
    }
    out += fmt::format("BayesElo: {:.2f}, DrawElo: {:.1f}\n", m.bayeselo, m.drawelo);
    out += fmt::format("Pairs: two draws {:.2f} %, a win and a loss {:.2f} %, won/lost {:.2f}\n", 100 * m.two_draws,
                       100 * m.win_and_loss, m.pairs_ratio);
    out += fmt::format("Variance ratio (pairs/games): {:.3f}, RMS bias: {:.1f} Elo\n", m.variance_ratio, m.rms_bias);
    return out;
}

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
