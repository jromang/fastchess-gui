#pragma once

// Changes to a round robin or a gauntlet while it runs. Each one is a journal event, applied the same way when
// the journal is replayed. An edit only ever changes encounters from position `at` on, the first encounter not
// started when it was made: what was played, and the openings of started encounters, never move.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include <types/engine_config.hpp>

#include "model.hpp"
#include "schedule.hpp"

namespace fcg::tour {

struct Edit {
    enum class Kind { AddParticipant, ReplaceParticipant, Withdraw, SetCycles, VoidGames };
    Kind kind = Kind::AddParticipant;

    fastchess::EngineConfiguration engine;        // add, replace: the new engine (its name is made unique if needed)
    int elo          = 0;                         // add, replace: the new engine's reference rating, 0 = none
    bool catch_up    = true;                      // add: also play the cycles already started
    int participant  = -1;                        // replace, withdraw: the participant's id
    bool void_played = false;                     // withdraw: its played games stop counting
    int cycles       = 0;                         // set cycles: the new number
    std::vector<std::pair<uint32_t, int>> games;  // void: (encounter, game)
    bool replay = true;                           // void: play these games again
    std::string why;                              // void: shown with the voided games
};

// What an edit changes, computed before it is made.
struct Impact {
    uint64_t games_before = 0;  // games of the whole tournament
    uint64_t games_after  = 0;
    uint64_t replayed     = 0;  // played games voided and played again
    uint64_t voided       = 0;  // played games that stop counting, without replay
    std::string error;          // why the edit cannot be made; empty when it can
};

// The event for `edit`, made at position `at`; `new_id` is the id a new participant gets.
[[nodiscard]] nlohmann::json editEvent(const Edit& edit, uint32_t at, int new_id, const TournamentSpec& spec);

// True for the journal events editEvent() makes.
[[nodiscard]] bool isEditEvent(const std::string& type);

// Applies what an edit event does to the participants and the schedule. What it does to the results (voided
// games, games to play again or not at all) is the TournamentState's business. Throws on an invalid event.
void applyEditEvent(const nlohmann::json& event, TournamentSpec& spec, Schedule& schedule);

}  // namespace fcg::tour
