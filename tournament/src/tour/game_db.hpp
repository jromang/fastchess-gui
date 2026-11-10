#pragma once

// Every game of every tournament of the profile, and of PGN files imported for the ratings (CCRL, CEGT, other
// programs): who played whom, at which time control, when, the result and how the game ended. What the rating
// list, the engine cards and the head to head records are computed from. Read only; tournaments that are running
// can be included.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <matchmaking/stats.hpp>
#include <types/match_data.hpp>

#include "model.hpp"
#include "pgn_file.hpp"
#include "standings.hpp"

namespace fcg::tour {

struct DbSource {
    std::filesystem::path path;  // a tournament folder, or an imported PGN file
    std::string name;
    bool external = false;  // an imported PGN file
};

struct DbGame {
    uint32_t source                         = 0;  // index into GameDb::sources
    uint32_t white                          = 0;  // indexes into GameDb::engines
    uint32_t black                          = 0;
    uint16_t time_control                   = 0;  // index into GameDb::time_controls
    float white_score                       = 0.5f;
    fastchess::MatchTermination termination = fastchess::MatchTermination::NORMAL;
    uint32_t plies                          = 0;
    int64_t played_at                       = 0;    // unix time, milliseconds (an imported game: its date)
    uint64_t number                         = 0;    // 1-based game number in its tournament; 0 for an imported game
    uint32_t encounter                      = ~0u;  // the game pair it belongs to in its tournament
    PgnSpan pgn;                                    // in the tournament's games.pgn
};

struct GameDb {
    std::vector<DbSource> sources;
    std::vector<std::string> engines;
    std::vector<std::string> time_controls;
    std::vector<DbGame> games;  // sources in order, then each source's games in order

    [[nodiscard]] std::optional<uint32_t> engine(const std::string& name) const;
};

// The tournaments in `dirs` (folders that are not tournaments are skipped) and the PGN files in `pgns`.
[[nodiscard]] GameDb readGameDb(const std::vector<std::filesystem::path>& dirs,
                                const std::vector<std::filesystem::path>& pgns = {});

// Which games count.
struct GameFilter {
    std::string time_control;       // empty: every one
    std::set<std::string> engines;  // games between two of them; empty: every engine
    int64_t from = 0, to = 0;       // played between these times (unix ms), 0: no bound
    bool external = true;           // with the imported games

    [[nodiscard]] bool accepts(const GameDb& db, const DbGame& game) const;
};

// What two engines did against each other, from the first one's point of view: every game, and the pentanomial
// counts of the game pairs they played both games of (same tournament, same opening, colours reversed).
struct HeadToHead {
    fastchess::Stats total;
    std::vector<std::pair<std::string, fastchess::Stats>> by_time_control;
    std::vector<std::pair<uint32_t, fastchess::Stats>> by_source;  // source index, in order
    std::vector<std::size_t> games;                                // indexes into GameDb::games, in order
};
[[nodiscard]] HeadToHead headToHead(const GameDb& db, const GameFilter& filter, const std::string& a,
                                    const std::string& b);

// One engine over every source: its results, how its games were lost, the sources it played in.
struct EngineRecord {
    fastchess::Stats total;  // wins, draws and losses (no pairs)
    std::vector<std::pair<std::string, fastchess::Stats>> by_time_control;
    struct Source {
        uint32_t source = 0;
        fastchess::Stats stats;
        int64_t last_game = 0;
    };
    std::vector<Source> sources;  // by the time of their last game
    int timeouts = 0, crashes = 0, illegal = 0, adjudicated = 0;
    std::vector<std::size_t> problem_games;  // lost on time, by a crash, a stall or an illegal move
};
[[nodiscard]] EngineRecord engineRecord(const GameDb& db, const GameFilter& filter, const std::string& engine);

}  // namespace fcg::tour
