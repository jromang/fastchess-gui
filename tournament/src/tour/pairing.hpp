#pragma once

// Pairings of the formats whose next round depends on the results: Swiss and knockout. Pure functions; the
// tournament state journals what they return, so a resumed tournament replays the same rounds.
//
// Every pairing is played as a pair of games with colours reversed, so colours need no allocation.

#include <map>
#include <set>
#include <utility>
#include <vector>

namespace fcg::tour {

struct RoundPairing {
    std::vector<std::pair<int, int>> pairs;  // participant ids, the higher placed first
    std::vector<int> byes;                   // participants without an opponent this round
    // Knockout: the round's places in bracket order, each a pair or a single player (a bye). The winners of
    // places 2k and 2k+1 meet in the next round.
    std::vector<std::vector<int>> bracket;
};

// The next Swiss round. `players` are the participants in seed order (the first is the strongest expected);
// `score` their points so far; `played` the pairs already met (smaller id first); `had_bye` who already had a
// bye. Players are ranked by score, then seed. Within each score group the top half meets the bottom half (Dutch
// style); a player who cannot be paired there floats to the next group. No two players meet twice unless there
// is no other way. With an odd number, the lowest ranked player without a bye so far gets it.
[[nodiscard]] RoundPairing swissPairing(const std::vector<int>& players, const std::map<int, double>& score,
                                        const std::set<std::pair<int, int>>& played, const std::set<int>& had_bye);

// Positions of seeds 1..size in a bracket of `size` (a power of two), so that the two first seeds can only meet
// in the final: 1, 8, 4, 5, 2, 7, 3, 6 for 8.
[[nodiscard]] std::vector<int> bracketOrder(int size);

// The first round of a knockout: players in seed order, byes for the top seeds when the field is not a power of
// two. Pairs are in bracket order: the winners of pairs 2k and 2k+1 meet in the next round.
[[nodiscard]] RoundPairing knockoutFirstRound(const std::vector<int>& players);

}  // namespace fcg::tour
