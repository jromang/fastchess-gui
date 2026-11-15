// The participants as the New tournament page edits them: added from the library, moved, removed, and what each one
// plays differently from the common settings.

#include <doctest/doctest.hpp>

#include <string>
#include <vector>

#include "participants.hpp"

using namespace fcg;

namespace {

std::vector<std::string> namesOf(const std::vector<ParticipantSetup>& list) {
    std::vector<std::string> out;
    for (const auto& p : list) out.push_back(p.name);
    return out;
}

std::vector<ParticipantSetup> make(const std::vector<std::string>& names) {
    std::vector<ParticipantSetup> out;
    for (const auto& n : names) out.push_back({n});
    return out;
}

}  // namespace

TEST_CASE("participants: moved before another one, several at once in their order, seeds first") {
    auto list = make({"A", "B", "C", "D", "E"});
    // Dragged to the top: the new seed.
    CHECK(moveParticipants(list, {3}, 0) == std::vector<int>{0});
    CHECK(namesOf(list) == std::vector<std::string>{"D", "A", "B", "C", "E"});
    // Two, given in any order, dropped before the last one: they keep their order.
    CHECK(moveParticipants(list, {3, 1}, 4) == std::vector<int>{2, 3});
    CHECK(namesOf(list) == std::vector<std::string>{"D", "B", "A", "C", "E"});
    // To the end, and one step down (before the one after the next).
    CHECK(moveParticipants(list, {0}, 5) == std::vector<int>{4});
    CHECK(namesOf(list) == std::vector<std::string>{"B", "A", "C", "E", "D"});
    CHECK(moveParticipants(list, {1}, 3) == std::vector<int>{2});
    CHECK(namesOf(list) == std::vector<std::string>{"B", "C", "A", "E", "D"});
    // Onto itself, or with indices that are not there: nothing moves.
    CHECK(moveParticipants(list, {2}, 2) == std::vector<int>{2});
    CHECK(moveParticipants(list, {2}, 3) == std::vector<int>{2});
    CHECK(moveParticipants(list, {7, -1}, 0).empty());
    CHECK(namesOf(list) == std::vector<std::string>{"B", "C", "A", "E", "D"});
    // A participant's own settings go with it.
    list[3].hash = 512;
    moveParticipants(list, {3}, 0);
    CHECK(list[0].name == "E");
    CHECK(list[0].hash == 512);
}

TEST_CASE("participants: removed, several at once") {
    auto list = make({"A", "B", "C", "D"});
    CHECK(removeParticipants(list, {3, 1, 1, 9}) == 2);
    CHECK(namesOf(list) == std::vector<std::string>{"A", "C"});
    CHECK(removeParticipants(list, {}) == 0);
}

TEST_CASE("participants: library engines added where they are dropped; a match takes them as A, then B") {
    MatchSetup setup;
    setup.format       = TournamentFormat::Gauntlet;
    setup.participants = make({"A", "B"});
    CHECK(addToSetup(setup, {"C", "D"}));
    CHECK(namesOf(setup.participants) == std::vector<std::string>{"A", "B", "C", "D"});
    // Dropped before the second one; one already there is left out.
    CHECK(addToSetup(setup, {"E", "A"}, 1));
    CHECK(namesOf(setup.participants) == std::vector<std::string>{"A", "E", "B", "C", "D"});
    CHECK(!addToSetup(setup, {"B"}));
    CHECK(!addToSetup(setup, {}));

    MatchSetup match;
    CHECK(addToSetup(match, {"New"}));
    CHECK(match.engine_a == "New");
    CHECK(match.engine_b.empty());
    CHECK(addToSetup(match, {"Old"}));
    CHECK(match.engine_b == "Old");
    // Both chosen: B is replaced.
    CHECK(addToSetup(match, {"Other"}));
    CHECK(match.engine_a == "New");
    CHECK(match.engine_b == "Other");
    CHECK(!addToSetup(match, {"Other"}));
}

TEST_CASE("participants: what one plays differently from the common settings, for the limit of the setup") {
    MatchSetup setup;
    setup.format      = TournamentFormat::RoundRobin;
    setup.common_hash = 256;
    setup.limit       = LimitKind::Time;
    ParticipantSetup p{"A"};
    CHECK(participantDifferences(p, setup).empty());
    p.hash = 256;  // the common value, given again: no difference
    CHECK(participantDifferences(p, setup).empty());
    p.hash         = 512;
    p.threads      = 4;
    p.time_percent = 80;
    p.nodes        = 5000;  // not used with a clock
    CHECK(participantDifferences(p, setup) == std::vector<std::string>{"Hash 512 MB", "4 threads", "80 % of the time"});
    setup.limit = LimitKind::Nodes;  // time odds mean nothing for a node count
    CHECK(participantDifferences(p, setup) ==
          std::vector<std::string>{"Hash 512 MB", "4 threads", "5.0k nodes per move"});
    p.book = {"/books/perfect.bin", "Perfect", 12};
    CHECK(participantDifferences(p, setup).back() == "own book Perfect, 12 plies");
}

TEST_CASE("participants: a team match's teams, the halves of the list until engines are put in a team") {
    MatchSetup setup;
    setup.format       = TournamentFormat::TeamMatch;
    setup.participants = make({"A", "B", "C", "D", "E"});
    // None chosen: the upper half, the larger one, against the lower half.
    CHECK(setupTeams(setup) == std::vector<int>{0, 0, 0, 1, 1});
    CHECK(teamSizes(setup) == std::pair{3, 2});

    // One moved: the others keep the team they had, now chosen.
    setTeam(setup, {1}, 1);
    CHECK(setupTeams(setup) == std::vector<int>{0, 1, 0, 1, 1});
    CHECK(setup.participants[0].team == 1);
    CHECK(setup.participants[1].team == 2);
    // A newcomer joins the smaller team, team A when they are level.
    setup.participants.push_back({"F"});
    CHECK(setupTeams(setup).back() == 0);
    setup.participants.push_back({"G"});
    setup.participants.push_back({"H"});
    CHECK(setupTeams(setup) == std::vector<int>{0, 1, 0, 1, 1, 0, 0, 1});
    // Several at once; indices outside the list are left out.
    setTeam(setup, {0, 2, 9}, 1);
    CHECK(teamSizes(setup) == std::pair{2, 6});
    // The halves again.
    splitTeams(setup);
    CHECK(setupTeams(setup) == std::vector<int>{0, 0, 0, 0, 1, 1, 1, 1});
    // A team moved as a whole leaves the other one empty.
    setTeam(setup, {0, 1, 2, 3}, 1);
    CHECK(teamSizes(setup) == std::pair{0, 8});
}
