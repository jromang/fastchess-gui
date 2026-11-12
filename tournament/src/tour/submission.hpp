#pragma once

// Games for a rating list, packaged the way its testers send them: the file named after the tester, the date and
// the conditions, the players named as the lists write them, the rounds numbered again. TorsGUI's export does the
// same, after the testers' own export script.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "model.hpp"
#include "state.hpp"

namespace fcg::tour {

// A player as the lists write it: "Stockfish 19" on 4 threads is "Stockfish 19 64-bit 4CPU", on one thread
// "Stockfish 19 64-bit". A name that already ends with "64-bit" (any case) or "NCPU" does not get them twice.
[[nodiscard]] std::string listName(const std::string& name, int threads);

// The integer value of a UCI option of the engine (Threads, Hash), `fallback` when it has none or it is no number.
[[nodiscard]] int optionValue(const fastchess::EngineConfiguration& engine, std::string_view option, int fallback);

struct SubmissionExport {
    std::string tester;
    std::string site;   // the Site tag of every game; empty: as played
    std::string date;   // of the submission, YYYY-MM-DD
    std::string event;  // the Event tag of every game, and the file name
    int hash_mb = 0;
    std::string book;                  // its name in the file name
    int egtb = 0;                      // tablebase pieces, 0 without
    std::map<int, std::string> names;  // participant id -> player name in the games; missing: the engine's name
    bool leave_out_crashes = true;     // the games an engine lost by a crash or a stall (Termination "abandoned")
};

// The file name without its extension: "[Tester 2026-11-12] Event (hash 256MB) (book UHO) (egtb 5-man)".
[[nodiscard]] std::string submissionFileStem(const SubmissionExport& e);

// The zip file of a stem: spaces, brackets and parentheses made one "_", none at the ends, ".zip" after it:
// "Tester_2026-11-12_Event_hash_256MB_book_UHO_egtb_5-man.zip".
[[nodiscard]] std::string submissionZipName(const std::string& stem);

// The zip of the export written to `pgn`: next to it, named by submissionZipName() after its stem. Empty when that
// would be the PGN file itself (a PGN named "x.zip"), the names compared as Windows does, letter case aside.
[[nodiscard]] std::filesystem::path submissionZipPath(const std::filesystem::path& pgn);

// The event of a gauntlet as the testers name it, after its seed and the day its first game started (local
// time): "Stockfish 19 64-bit 4CPU - Nov 12".
[[nodiscard]] std::string submissionEvent(const std::string& seed, int64_t first_game_ms);

// What went into the export, and what was left out.
struct SubmissionGames {
    std::string pgn;
    std::size_t games      = 0;
    std::size_t voided     = 0;  // games that no longer count (replayed, of a withdrawn or replaced engine)
    std::size_t duplicates = 0;  // a game of the schedule recorded twice: the first one is kept
    std::size_t crashes    = 0;  // games lost by a crash or a stall, left out with leave_out_crashes
    std::size_t unreadable = 0;  // games the PGN file no longer holds
};

// The games of a tournament for a rating list, from its cutechess-style PGN file (`pgn`, where results point): each
// game that counts once, in the order they ended, with its Event, Site, Round (1 to N), White and Black tags
// rewritten, and the PlyCount of a game lost by an illegal move giving the moves played (fastchess counts the
// illegal one, which is not in the movetext, and the testers' scripts take such a game for a broken one). Every other
// tag stays as played.
[[nodiscard]] SubmissionGames submissionGames(const std::filesystem::path& pgn, const std::vector<GameResult>& results,
                                              const TournamentSpec& spec, const SubmissionExport& e);

// What a tester checks before sending a tournament's games to a rating list (TorsGUI's guide, §7), as found in its
// settings and results: the GUI writes the checklist from it.
struct SubmissionFacts {
    uint64_t played = 0;  // games that count
    std::optional<uint64_t> planned;
    // Encounters of which one game only counts (the game with colours reversed missing), by participant ids.
    std::vector<std::pair<int, int>> unpaired;
    std::size_t duplicates = 0;  // games of the schedule recorded twice
    std::size_t voided     = 0;
    // The games that count lost by a crash or a stall, on time, by an illegal move: their numbers (number()).
    std::vector<uint64_t> crashes, timeouts, illegal;

    struct Engine {
        int id = 0;
        std::string name;
        int hash    = 0;  // MB, 0: the engine's default
        int threads = 1;
        bool ponder = false;  // its Ponder option is on
        std::string syzygy;   // its SyzygyPath
    };
    std::vector<Engine> engines;  // the active participants
    std::string book;             // the opening book's file name, empty without
    std::string adjudication_tb;  // the tablebases of the adjudication, empty without
};

[[nodiscard]] SubmissionFacts submissionFacts(const TournamentSpec& spec, const std::vector<GameResult>& results,
                                              std::optional<uint64_t> planned);

// The value of a tag of one game's PGN text, empty when it has none.
[[nodiscard]] std::string pgnTag(std::string_view game, std::string_view tag);

}  // namespace fcg::tour
