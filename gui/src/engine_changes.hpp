#pragma once

// When an engine's program is rebuilt or replaced: what its new read found against the read before (the engine card
// lists it until it is dismissed, see EngineEntry::changes), and whether an engine's program is still the one it
// played its last tournament with, or the one each participant of a tournament joined it with.

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <tour/model.hpp>
#include <tour/summary.hpp>

#include "model.hpp"

namespace fcg {

// What one read of an engine found: the names it reports and its options.
struct EngineRead {
    std::string id_name;
    std::string id_author;
    std::vector<UciOptionInfo> options;
};

// The read the entry keeps, its last.
[[nodiscard]] EngineRead lastRead(const EngineEntry& e);

// Two fingerprints of different contents (size or CRC32). An unknown one (size 0) tells nothing: false.
[[nodiscard]] bool otherProgram(const tour::FileFingerprint& a, const tour::FileFingerprint& b);

// Whether `value` fits the option: a whole number within the bounds of a spin (a bound that is not a number is
// none), true or false for a check, one of the choices of a combo (letter case aside). Any value for the others.
[[nodiscard]] bool fitsOption(const UciOptionInfo& option, const std::string& value);

// What changed from the read `before` to the read `after`: the id name and author, and each option that is new, gone,
// or described otherwise (type, default, bounds, choices; a button's default does not count). `values`: the library's
// values (EngineEntry::options) when the new read came, kept with the options they belong to. The options that wait
// for a choice come first, then those described otherwise, the new ones and the gone ones, each by name.
[[nodiscard]] ProgramChanges compareReads(const EngineRead& before, const EngineRead& after,
                                          const std::vector<std::pair<std::string, std::string>>& values);

// The read `changes` were found against, from the read `now` that found them. The options already decided count as
// seen: they are as in `now`.
[[nodiscard]] EngineRead readBefore(const ProgramChanges& changes, const EngineRead& now);

// Records in e.changes what the new read `after` of the entry's rebuilt or replaced program found, against the read
// the entry keeps, or, while the changes of an earlier build wait to be dismissed, against the read before them: one
// notice for several builds. Call it before the entry takes the new read, with the values set as they were then.
// Returns true when something changed; otherwise e.changes is cleared.
bool noteChanges(EngineEntry& e, const EngineRead& after, const std::string& when);

// An option of the changes that waits for the user's choice: its default changed, or the value set no longer fits
// it, and the engine played it with another value than the new default. Decided or not.
[[nodiscard]] bool needsChoice(const OptionChange& change);
// The options that wait for a choice not made yet.
[[nodiscard]] int choicesLeft(const ProgramChanges& changes);
// The changes tell something beside the choices: a new name or author, options new, gone, or described otherwise.
[[nodiscard]] bool tellsMore(const ProgramChanges& changes);

enum class ValueChoice {
    Keep       = 1,  // the value the engine played with: the one set, or the old default where none was
    NewDefault = 2,  // the new default: the value set is cleared
};
// The choice for the option named `option` of e.changes, if it waits for one. The notice goes once no choice is left
// and it tells nothing more (see tellsMore); otherwise it stays until dismissed. Returns true when an option changed.
bool chooseValue(EngineEntry& e, const std::string& option, ValueChoice choice);
// The same choice for every option that waits for one.
bool chooseAllValues(EngineEntry& e, ValueChoice choice);

// The program an engine played in a tournament, and that tournament.
struct PlayedProgram {
    tour::FileFingerprint program;
    std::string tournament;  // its name
    std::filesystem::path dir;
};

// A program and its arguments as sameEngine() compares them (Windows file names ignore case).
[[nodiscard]] std::string programKey(const std::string& cmd, const std::string& args);

// The program each engine played its last tournament with, by programKey(): the first tournament of `tournaments`
// (newest first, as tour::listTournaments gives them) that has a participant with that program and arguments.
// Participants whose program was not fingerprinted are left out.
[[nodiscard]] std::map<std::string, PlayedProgram> lastPlayedPrograms(const std::vector<tour::Summary>& tournaments);

// The last tournament of the entry's engine, when its program is no longer the one played there (a new read pending
// counts: the program changed since its last read); nullptr when it is the same, not known, or missing.
[[nodiscard]] const PlayedProgram* rebuiltSincePlayed(const EngineEntry& e,
                                                      const std::map<std::string, PlayedProgram>& played);

// A participant whose program changed or went missing since it joined the tournament.
struct ParticipantProgram {
    int id = 0;
    std::string name;
    std::string cmd;
    bool missing = false;
};
// Those of the active participants of `spec` (tour::changedSince: the CRC is read only when the size is the same and
// the date moved).
[[nodiscard]] std::vector<ParticipantProgram> changedPrograms(const tour::TournamentSpec& spec);

}  // namespace fcg
