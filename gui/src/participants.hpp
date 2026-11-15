#pragma once

// The participants of the setup as the New tournament page edits them: engines added from the library, moved (seeds
// first), removed, and what each plays differently from the common settings.

#include <string>
#include <utility>
#include <vector>

#include "model.hpp"

namespace fcg {

// Moves the participants at `moved` (indices, in any order) before the one at `to`, an index of the list as it is
// (its size: to the end), keeping their order. Returns their new indices, in order.
std::vector<int> moveParticipants(std::vector<ParticipantSetup>& list, std::vector<int> moved, int to);

// Removes the participants at `indices`; returns how many were.
int removeParticipants(std::vector<ParticipantSetup>& list, std::vector<int> indices);

// Library engines added to the setup: a match takes them as A, then B (B replaced when both are chosen); the other
// formats insert them before participant `at` (-1: at the end), leaving out those already there. True when the setup
// changed.
bool addToSetup(MatchSetup& setup, const std::vector<std::string>& names, int at = -1);

// A team match's teams, by participant: 0 for team A, 1 for team B. Those put in a team keep it; the others make the
// upper half of the list team A and the lower half team B when none was put in one, else each joins the smaller
// team (team A when they are level).
[[nodiscard]] std::vector<int> setupTeams(const MatchSetup& setup);

// Puts the participants at `indices` in team `team` (0: A, 1: B); the others stay where they are, put there now.
void setTeam(MatchSetup& setup, const std::vector<int>& indices, int team);

// The teams made again from the list: its upper half against its lower half.
void splitTeams(MatchSetup& setup);

// The number of participants of each team, A then B.
[[nodiscard]] std::pair<int, int> teamSizes(const MatchSetup& setup);

// What a participant plays with that differs from the common settings: "Hash 512 MB", "4 threads", "80 % of the time",
// "book: Perfect 2023, 12 plies". Only what the setup's limit uses: nodes for a node count, depth for a depth, time
// odds for a clock.
[[nodiscard]] std::vector<std::string> participantDifferences(const ParticipantSetup& p, const MatchSetup& setup);

}  // namespace fcg
