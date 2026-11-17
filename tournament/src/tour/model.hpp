#pragma once

// What a tournament is: its settings and its participants, as stored in tournament.json.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include <types/engine_config.hpp>
#include <types/tournament.hpp>

namespace fcg::tour {

enum class Format { Match = 0, RoundRobin = 1, Gauntlet = 2, Swiss = 3, Knockout = 4, TeamMatch = 5 };

NLOHMANN_JSON_SERIALIZE_ENUM(Format, {{Format::Match, "match"},
                                      {Format::RoundRobin, "round_robin"},
                                      {Format::Gauntlet, "gauntlet"},
                                      {Format::Swiss, "swiss"},
                                      {Format::Knockout, "knockout"},
                                      {Format::TeamMatch, "team_match"}})

// Formats whose rounds are paired from the results, as the tournament goes.
[[nodiscard]] inline bool pairedAsItGoes(Format f) noexcept { return f == Format::Swiss || f == Format::Knockout; }

// Who has White in a match: A and B in turn (each opening twice, colours reversed), or always the same engine
// (fastchess' -noswap, and -noswap -reverse for B).
enum class Colours { Alternate = 0, AWhite = 1, BWhite = 2 };

NLOHMANN_JSON_SERIALIZE_ENUM(Colours, {{Colours::Alternate, "alternate"},
                                       {Colours::AWhite, "a_white"},
                                       {Colours::BWhite, "b_white"}})

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

// An engine that runs on a remote machine (the window's Machines), played from this computer through the engine hub of
// the process that plays the tournament: the machine's id, the program there, its arguments as fastchess splits them,
// its working folder there (empty: the program's folder).
struct RemoteEngine {
    std::string machine;
    std::string program;
    std::vector<std::string> args;
    std::string dir;
    bool operator==(const RemoteEngine&) const = default;
};
void to_json(nlohmann::json& j, const RemoteEngine& r);
void from_json(const nlohmann::json& j, RemoteEngine& r);

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

    // Its own opening book, a Polyglot file, played by the book adapter (book_adapter.hpp) while the game is in it,
    // up to `book_depth` plies (0: the whole book). Empty: the engine plays every move. A tournament with one is
    // of version 3, which versions up to 0.5.3 (that would play the engine alone) refuse.
    std::string book;
    int book_depth = 0;

    // Its engine uses a GPU (a network engine such as Lc0): no more such engines play at once than the machine has
    // GPUs (budget.hpp). From the engine library; saved only when set.
    bool gpu = false;
    // A team match: 0 for team A, 1 for team B. A replacement takes the team of the participant it replaces.
    int team = 0;
    // Its engine runs on a machine: `engine` starts its relay (fastchess-gui --remote-engine), which the engine hub of
    // the process playing the tournament serves with this engine. Its program is not a file here, so it has no
    // fingerprint. Saved only when set.
    std::optional<RemoteEngine> remote;
};

// A tournament's own settings, those of a participant without settings of its own (time odds, its own Hash...): an
// engine added or brought in while it runs takes them, unless given its own.
struct Defaults {
    fastchess::Limit limit;  // the time control, time per move, nodes or depth
    int hash    = 0;         // MB; 0: each engine's own
    int threads = 0;         // 0: each engine's own
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
    // Match, SPRT (sprt_rules.hpp): the Elo bias added to both bounds (conditions.sprt has them with it), and no
    // decision before this many games; 0: none.
    double sprt_bias        = 0;
    uint64_t sprt_min_games = 0;
    // Match: who has White. Locked, an encounter is still two games, both with the same colours, each from an
    // opening of its own (TournamentState::ownOpenings(); as fastchess plays -noswap -games 1): the pairs, their
    // numbers and the pentanomial stay. A match with locked colours is of version 5, which versions up to 0.6.2 (that
    // would alternate them) refuse. Alternate in the other formats.
    Colours colours = Colours::Alternate;

    // A team match: board by board (the first engine of each team against the first of the other, and so on, the
    // teams of the same size) rather than every engine of a team against every engine of the other (a Scheveningen
    // system); `cycles` repeats it. `team_names`: team A's and team B's, "Team A" and "Team B" when empty. A
    // tournament with teams is of version 4, which versions up to 0.5.9 (that would take it for a match) refuse.
    bool team_boards = false;
    std::array<std::string, 2> team_names;

    // Openings, adjudication, SPRT, PGN options, concurrency, seed, variant: fastchess' own settings.
    // games, rounds and the output file names are not used, the tournament layer decides them.
    fastchess::config::Tournament conditions;

    std::vector<Participant> participants;
    FileFingerprint book;

    // A Polyglot book the openings were drawn from (conditions.opening.file is then the positions its lines lead to,
    // written to the tournament's folder): its file, and the depth its lines were followed to. Empty for another
    // book, and in the tournaments of 0.5.9 and earlier.
    std::string polyglot_book;
    int polyglot_plies = 0;
    // How the moves of a Polyglot book are chosen, the tournament's and the participants' own: from -100, the best
    // move only, through 0, by their weights, to 100, every move alike (choiceWeights() in polyglot.hpp).
    int book_randomness = 0;

    // Unbalanced openings (unbalanced.hpp): a game whose two engines both see the same side beyond `unbalanced_cp`
    // for their first `unbalanced_moves` moves after the book is flagged when it ends; 0 cp: not looked for. With
    // `unbalanced_replace`, its pair is voided and played again from another opening.
    int unbalanced_cp       = 0;
    int unbalanced_moves    = 4;
    bool unbalanced_replace = false;

    bool log_every_game = false;  // keep the engine log of every game, not only of those that end badly
    // An engine that cannot start stops the tournament once its tries are used up, rather than have its games set
    // aside while the others go on (start_failures.hpp).
    bool stop_on_start_failure = false;
    // Round robin, gauntlet, Swiss: every encounter of a round (in a cycle) plays the same opening, the next one of
    // the book for each new round, so that no engine gets easier openings than the others of its round.
    bool opening_per_round = false;

    // A PGN file of the user's, outside the folder, that every game is also appended to, in schedule order (see
    // TournamentState::writeOut()); empty = none. Several tournaments may share it.
    std::string pgn_out;
    bool pgn_out_chessbase = false;  // [%eval] [%emt] comments rather than cutechess' {+0.35/18 1.234s}

    // The names of the two PGN files of the folder (see state.hpp), given after the tournament's name when it is
    // created (namePgnFiles()) and never changed afterwards. Empty in the tournaments of 0.4.12 and earlier, whose
    // files are games.pgn and games-chessbase.pgn.
    std::string pgn_file;
    std::string pgn_chessbase_file;

    // The options the setup forced on the participants that have them (SyzygyPath, OwnBook), as name and value, so
    // that an engine added later gets them too; the engines' own options are in their configuration. Empty when
    // the setup forced none, and in the tournaments of 0.4.13 and earlier.
    std::vector<std::pair<std::string, std::string>> forced_options;

    // Its own settings, recorded when it is created. Not in the tournaments of 0.6.5 and earlier, where the first
    // participant's settings stand in for them (tournamentDefaults() on the GUI's side).
    std::optional<Defaults> defaults;

    // Its time control, time per move, nodes or depth: its own, else its first participant's (which may have had time
    // odds). Not for a tournament without participants.
    [[nodiscard]] const fastchess::Limit& limit() const {
        return defaults ? defaults->limit : participants.front().engine.limit;
    }

    // Whether an encounter's `first` participant (A in a match) has White in its game `game` (0 or 1).
    [[nodiscard]] bool firstHasWhite(int game) const noexcept {
        switch (colours) {
            case Colours::AWhite:
                return true;
            case Colours::BWhite:
                return false;
            default:
                return game == 0;
        }
    }
    [[nodiscard]] bool coloursLocked() const noexcept { return colours != Colours::Alternate; }

    [[nodiscard]] std::string pgnFile() const { return pgn_file.empty() ? "games.pgn" : pgn_file; }
    [[nodiscard]] std::string chessbasePgnFile() const {
        return pgn_chessbase_file.empty() ? "games-chessbase.pgn" : pgn_chessbase_file;
    }

    // A team match: the name of team `team` (0: A, 1: B), and its participants' ids in the list's order, the withdrawn
    // and replaced ones too unless `active_only`.
    [[nodiscard]] std::string teamName(int team) const;
    [[nodiscard]] std::vector<int> teamMembers(int team, bool active_only = false) const;

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

// A tournament's name as the stem of a file name: made fit for one by fileNameOf(), spaces around it removed, at
// most 64 bytes (whole characters). Empty when the name is.
[[nodiscard]] std::string fileStemOf(const std::string& name);

// Names the PGN files of a new tournament after it: <name>.pgn and <name>-chessbase.pgn, the name made a file stem
// by fileStemOf(); games.pgn and games-chessbase.pgn when it has no name.
void namePgnFiles(TournamentSpec& spec);

void to_json(nlohmann::json& j, const Participant& p);
void from_json(const nlohmann::json& j, Participant& p);
void to_json(nlohmann::json& j, const FileFingerprint& f);
void from_json(const nlohmann::json& j, FileFingerprint& f);
void to_json(nlohmann::json& j, const TournamentSpec& s);
void from_json(const nlohmann::json& j, TournamentSpec& s);

}  // namespace fcg::tour
