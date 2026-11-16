#pragma once

// Named profiles for the sections of the setup: a format, a time control, openings, an adjudication and the settings
// every engine plays with. A tester reuses the same few of each (a rating list's time control, a book, an
// adjudication rule) in different mixes; a template names one profile per section, plus what no profile holds (the
// name, the participants, the concurrency, the PGN files). The profiles are kept in profiles.json in the profile
// folder, with a few presets of the program's.

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "model.hpp"

namespace fcg {

enum class SetupSection { Format = 0, Time, Openings, Adjudication, Engines };
inline constexpr std::array<SetupSection, 5> SETUP_SECTIONS{SetupSection::Format, SetupSection::Time,
                                                            SetupSection::Openings, SetupSection::Adjudication,
                                                            SetupSection::Engines};

// The section's name in profiles.json, in templates and in MatchSetup::profiles: "format", "time", "openings",
// "adjudication", "engines".
[[nodiscard]] const char* sectionKey(SetupSection section);
[[nodiscard]] std::optional<SetupSection> sectionOfKey(std::string_view key);
// As the setup shows it, translated: "Time control".
[[nodiscard]] const char* sectionTitle(SetupSection section);

// The fields of MatchSetup a section's profile holds, as its JSON names them. The Syzygy directories are in none:
// they are this machine's, shared by the adjudication and the engines, and a profile goes from one machine to another.
[[nodiscard]] const std::vector<std::string>& sectionFields(SetupSection section);

// The section's fields of `setup`, as a JSON object.
[[nodiscard]] nlohmann::json sectionValues(const MatchSetup& setup, SetupSection section);

struct SetupProfile {
    std::string name;       // a preset's in English: the setups and templates name it so in any language
    nlohmann::json values;  // the section's fields (sectionFields), those missing at their default
    bool preset = false;    // one of the program's, not in profiles.json
    MatchSetup setup;       // a default setup with `values`: what the setup shown is compared with

    // The name shown: a preset's translated.
    [[nodiscard]] std::string label() const;
};

[[nodiscard]] SetupProfile makeProfile(std::string name, SetupSection section, const nlohmann::json& values,
                                       bool preset = false);

// Whether `a` and `b` play the same in this section. Only the fields that matter are compared: the increment of a
// node count does not, nor the score of a draw adjudication that is off, nor the SPRT calculator's guesses.
[[nodiscard]] bool sameSection(const MatchSetup& a, const MatchSetup& b, SetupSection section);

// Copies the section's fields (those of sectionFields(), which the tests check it covers) from `from` to `to`.
void copySection(MatchSetup& to, const MatchSetup& from, SetupSection section);

// Gives the section of `setup` the profile's values, and names it as the section's profile.
void applyProfile(MatchSetup& setup, SetupSection section, const SetupProfile& profile);

// The user's profiles of each section, and the presets.
class ProfileStore {
   public:
    ProfileStore();

    // The user's profiles, by name, then the presets none of them replaces.
    [[nodiscard]] const std::vector<SetupProfile>& list(SetupSection section) const;
    [[nodiscard]] const SetupProfile* find(SetupSection section, const std::string& name) const;
    // The section of `setup` saved as the user's profile `name`: a new one, or the one of that name replaced. A preset
    // of that name is then hidden by it.
    const SetupProfile& save(SetupSection section, const std::string& name, const MatchSetup& setup);
    // Removes the user's profile `name`; a preset it hid is listed again. False when the user has none of that name.
    bool remove(SetupSection section, const std::string& name);
    // Whether `name` is one of the user's profiles (rather than a preset or nothing).
    [[nodiscard]] bool isUsers(SetupSection section, const std::string& name) const;

    // profiles.json: the user's profiles only, {"time": [{"name": ..., "values": {...}}, ...], ...}.
    [[nodiscard]] nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

   private:
    void relist(SetupSection section);

    std::array<std::vector<SetupProfile>, SETUP_SECTIONS.size()> users_, presets_, listed_;
};

// The profile a section of the setup names, empty when none.
[[nodiscard]] std::string sectionProfile(const MatchSetup& setup, SetupSection section);

enum class ProfileState {
    None,     // the section names no profile
    Same,     // it plays as its profile
    Changed,  // changed for this tournament only
    Missing,  // it names a profile that is not there any more (removed, or a template from another profile folder)
};
[[nodiscard]] ProfileState profileState(const MatchSetup& setup, SetupSection section, const ProfileStore& store);

// The sections that name no profile get the first one they play like: a setup or a template saved before there were
// profiles. One that names a profile removed since keeps its name, shown as missing.
void recognizeProfiles(MatchSetup& setup, const ProfileStore& store);

// A template: the whole setup, the profile each section came from, and "follow": the sections that played as their
// profile when it was saved. Loading it gives those the profile's values of the day, so that a profile updated since
// reaches every template that uses it; a section changed for that tournament only keeps its own values.
[[nodiscard]] nlohmann::json templateJson(const MatchSetup& setup, const ProfileStore& store);
// The setup of a template (or of a setup saved before templates named profiles), upgraded as readSavedSetup() does.
// Throws nlohmann::json::exception when it is no setup.
[[nodiscard]] MatchSetup templateSetup(const nlohmann::json& j, const ProfileStore& store);
// The same, read from a file: nullopt when it is missing or unreadable.
[[nodiscard]] std::optional<MatchSetup> readTemplate(const std::filesystem::path& file, const ProfileStore& store);

// The setup a tournament started from, kept in its folder for Clone and Relaunch with…: every value as it played, with
// the names of its profiles. Unlike a template's, no section follows its profile: a profile changed since shows as
// changed for this tournament, the values it played kept.
inline constexpr const char* TOURNAMENT_SETUP_FILE = "setup.json";
bool writeTournamentSetup(const std::filesystem::path& dir, const MatchSetup& setup);
// nullopt for a tournament without it (started by a version that did not keep it) or when it cannot be read.
[[nodiscard]] std::optional<MatchSetup> readTournamentSetup(const std::filesystem::path& dir,
                                                            const ProfileStore& store);

// One line saying what a section of the setup plays: "40 moves in 15 min, repeated", "draw 40/8/10 cp · TB 6 pieces".
// `books` gives the library's names of the books.
[[nodiscard]] std::string sectionSummary(const MatchSetup& setup, SetupSection section,
                                         const std::vector<BookEntry>& books);
// The same section as cutechess and fastchess write it on their command line, for the testers who know those:
// "tc=40/900", "-draw movenumber=40 movecount=8 score=10". Empty when there is nothing to say.
[[nodiscard]] std::string sectionCommandLine(const MatchSetup& setup, SetupSection section);

// A time in seconds as the summaries say it: "15 min", "1 min 30 s", "0.1 s".
[[nodiscard]] std::string durationText(double seconds);

// How the moves of the Polyglot books are chosen (MatchSetup::book_randomness), as the summaries say it: "moves by
// their weights", "the best moves only", "every move alike", "randomness -40".
[[nodiscard]] std::string randomnessText(int randomness);

// An SPRT as the summaries and the overview say it: "SPRT [0, 3]", "SPRT [0, 3] + 5, no decision before 2000 games",
// the bounds the tester gave (those the SPRT is given have the bias on them).
[[nodiscard]] std::string sprtText(double elo0, double elo1, double bias, uint64_t min_games);

// Who has White in a match (MatchSetup::colours), as its choice and the summaries say it: "Alternate", "A always
// White", "B always White".
[[nodiscard]] const char* coloursText(tour::Colours colours);

// The rule for the unbalanced openings, as the summaries say it: "unbalanced openings flagged (±200 cp for 4 moves)",
// "unbalanced openings replaced (…)"; empty when it is off.
[[nodiscard]] std::string unbalancedText(const MatchSetup& setup);

}  // namespace fcg
