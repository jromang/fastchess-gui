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

// Identifies a file a tournament depends on, an opening book or an engine's program, to warn when it changed.
struct FileFingerprint {
    uint64_t size  = 0;  // 0: unknown, the file could not be read
    uint32_t crc32 = 0;
    int64_t mtime  = 0;  // modification time, in the file clock's units: when it has not moved, the CRC is not read
};

// Size, modification time and CRC32 of the file; all zero when it cannot be read.
[[nodiscard]] FileFingerprint fingerprint(const std::string& file);

// Whether `file` is no longer the one `recorded` describes (missing, or other contents). The CRC is only read when
// the size is the same and the modification time differs.
[[nodiscard]] bool changedSince(const std::string& file, const FileFingerprint& recorded);

struct Participant {
    int id = 0;  // never reused within a tournament

    // Resolved when the participant is added: name (unique in the tournament), command, options and limits.
    // Later edits of the engine list do not change a tournament.
    fastchess::EngineConfiguration engine;

    // Withdrawn or replaced participants stay in the list, so that their games keep a name.
    bool active = true;
    std::optional<int> replaced_by;

    FileFingerprint program;  // engine.cmd when the participant joined, to warn when it was rebuilt or moved
    int elo = 0;              // reference rating from the engine list, 0 = none; written in the PGN headers
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
    FileFingerprint book;

    bool log_every_game = false;  // keep the engine log of every game, not only of those that end badly
    // Round robin, gauntlet, Swiss: every encounter of a round (in a cycle) plays the same opening, the next one of
    // the book for each new round, so that no engine gets easier openings than the others of its round.
    bool opening_per_round = false;

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
void to_json(nlohmann::json& j, const FileFingerprint& f);
void from_json(const nlohmann::json& j, FileFingerprint& f);
void to_json(nlohmann::json& j, const TournamentSpec& s);
void from_json(const nlohmann::json& j, TournamentSpec& s);

}  // namespace fcg::tour
