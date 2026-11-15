#include "sprt_rules.hpp"

#include <fmt/include/fmt/format.h>

namespace fcg::tour {

SprtBounds biasedBounds(double elo0, double elo1, double bias) noexcept { return {elo0 + bias, elo1 + bias}; }

SprtBounds unbiasedBounds(const SprtBounds& given, double bias) noexcept {
    return {given.elo0 - bias, given.elo1 - bias};
}

fastchess::SPRTResult sprtDecision(const fastchess::SPRT& sprt, double llr, uint64_t games, uint64_t min_games) {
    if (!sprt.isEnabled() || games < min_games) return fastchess::SPRT_CONTINUE;
    return sprt.getResult(llr);
}

std::string sprtText(double elo0, double elo1, double bias, uint64_t min_games) {
    auto text = fmt::format("SPRT [{:g}, {:g}]", elo0, elo1);
    if (bias != 0) text += fmt::format(" {} {:g}", bias > 0 ? "+" : "-", bias > 0 ? bias : -bias);
    if (min_games > 0) text += fmt::format(", no decision before {} games", min_games);
    return text;
}

}  // namespace fcg::tour
