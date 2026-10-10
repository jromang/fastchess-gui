#include "stats.hpp"

#include <cmath>
#include <string>

#include <fmt/include/fmt/core.h>
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

std::string formatElo(double elo, double err) {
    if (!finite(elo)) return "—";
    if (!finite(err)) return fmt::format("{:+.1f}", elo);
    return fmt::format("{:+.1f} ±{:.1f}", elo, err);
}

}  // namespace fcg
