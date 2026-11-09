#pragma once

// What a tournament is: its settings and its participants, as stored in tournament.json.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include <types/engine_config.hpp>
#include <types/tournament.hpp>

namespace fcg::tour {

enum class Format { Match = 0, RoundRobin = 1, Gauntlet = 2, Swiss = 3, Knockout = 4 };

NLOHMANN_JSON_SERIALIZE_ENUM(Format, {{Format::Match, "match"},
                                      {Format::RoundRobin, "round_robin"},
                                      {Format::Gauntlet, "gauntlet"},
                                      {Format::Swiss, "swiss"},
                                      {Format::Knockout, "knockout"}})

// Formats whose rounds are paired from the results, as the tournament goes.
[[nodiscard]] inline bool pairedAsItGoes(Format f) noexcept { return f == Format::Swiss || f == Format::Knockout; }

struct Participant {
    int id = 0;  // never reused within a tournament

    // Resolved when the participant is added: name (unique in the tournament), command, options and limits.
    // Later edits of the engine list do not change a tournament.
    fastchess::EngineConfiguration engine;

    // Withdrawn or replaced participants stay in the list, so that their games keep a name.
    bool active = true;
    std::optional<int> replaced_by;
};

// Identifies the opening book a tournament was started with, to warn when it changed.
struct BookFingerprint {
    uint64_t size  = 0;
    uint32_t crc32 = 0;

    bool operator==(const BookFingerprint&) const = default;
};

struct TournamentSpec {
    int version = 1;
    std::string name;
    Format format = Format::Match;

    int cycles         = 1;  // round robin and gauntlet: how often every pairing is played (two games each time);
                             // knockout: game pairs per match before tiebreaks
    int rounds         = 0;  // Swiss: number of rounds
    int seeds          = 1;  // gauntlet: the first `seeds` participants play every other one
    uint64_t max_games = 0;  // match: 0 = until stopped or decided by the SPRT

    // Openings, adjudication, SPRT, PGN options, concurrency, seed, variant: fastchess' own settings.
    // games, rounds and the output file names are not used, the tournament layer decides them.
    fastchess::config::Tournament conditions;

    std::vector<Participant> participants;
    BookFingerprint book;

    [[nodiscard]] const Participant* participant(int id) const {
        for (const auto& p : participants) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }
    [[nodiscard]] Participant* participant(int id) {
        for (auto& p : participants) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }
};

void to_json(nlohmann::json& j, const Participant& p);
void from_json(const nlohmann::json& j, Participant& p);
void to_json(nlohmann::json& j, const BookFingerprint& b);
void from_json(const nlohmann::json& j, BookFingerprint& b);
void to_json(nlohmann::json& j, const TournamentSpec& s);
void from_json(const nlohmann::json& j, TournamentSpec& s);

}  // namespace fcg::tour
