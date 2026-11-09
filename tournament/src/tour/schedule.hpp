#pragma once

// The order in which a tournament plays its encounters. Pure functions of the settings and the participants,
// so the schedule is never stored: it is rebuilt identically whenever a tournament is opened.

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "model.hpp"

namespace fcg::tour {

// A pair of games between two participants on the same opening, colours swapped: the pentanomial unit.
struct Encounter {
    uint32_t index = 0;  // position in the schedule, also the encounter's id and its opening number
    uint32_t cycle = 1;  // 1-based
    uint32_t round = 1;  // 1-based, inside the cycle
    int first      = 0;  // participant ids; `first` has White in game 0, `second` in game 1
    int second     = 0;
};

class Schedule {
   public:
    // Throws std::invalid_argument when the participants do not fit the format.
    explicit Schedule(const TournamentSpec& spec);

    // The encounter at `index`, nullopt past the end.
    [[nodiscard]] std::optional<Encounter> at(uint32_t index) const;

    // Number of encounters, nullopt for a match without a game limit.
    [[nodiscard]] std::optional<uint32_t> size() const noexcept { return size_; }

   private:
    Format format_ = Format::Match;
    std::vector<Encounter> list_;  // round robin and gauntlet
    int match_a_ = 0;              // match: the two participant ids
    int match_b_ = 0;
    std::optional<uint32_t> size_;
};

// One cycle of a round robin between players 0..n-1 (Berger tables, circle method): every round meets each
// player once, a player has a bye when n is odd. Pairs are (first, second).
[[nodiscard]] std::vector<std::vector<std::pair<int, int>>> bergerRounds(int n);

// One cycle of a gauntlet: players 0..seeds-1 each meet every other player once, one opponent per round.
[[nodiscard]] std::vector<std::vector<std::pair<int, int>>> gauntletRounds(int n, int seeds);

}  // namespace fcg::tour
