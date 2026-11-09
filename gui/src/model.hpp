#pragma once

// Plain data the GUI persists between sessions: the engine list and the last match setup.

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

namespace fcg {

// One "option name ..." line of an engine's uci output.
struct UciOptionInfo {
    std::string name;
    std::string type;  // spin, check, combo, string, button
    std::string default_value;
    std::string min;
    std::string max;
    std::vector<std::string> vars;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UciOptionInfo, name, type, default_value, min, max, vars)

struct EngineEntry {
    std::string name;  // unique, also the name fastchess and the PGN use
    std::string cmd;   // absolute path to the binary
    std::string args;
    std::string dir;  // working directory, empty = the binary's directory
    bool restart = false;

    // Only the options whose value differs from the engine default.
    std::vector<std::pair<std::string, std::string>> options;

    // What the last probe reported.
    std::string id_name;
    std::string id_author;
    std::vector<UciOptionInfo> uci_options;

    // Organisation.
    std::vector<std::string> groups;  // e.g. a pool, a rating list
    int elo = 0;                      // reference rating, 0 = none

    // Last health check: -1 never, 0 failed, 1 passed.
    int check_ok = -1;
    std::string check_message;
    std::string check_when;

    [[nodiscard]] const std::string* option(const std::string& key) const {
        for (const auto& [k, v] : options) {
            if (k == key) return &v;
        }
        return nullptr;
    }

    void setOption(const std::string& key, const std::string& value) {
        for (auto& [k, v] : options) {
            if (k == key) {
                v = value;
                return;
            }
        }
        options.emplace_back(key, value);
    }

    void clearOption(const std::string& key) {
        std::erase_if(options, [&](const auto& kv) { return kv.first == key; });
    }

    [[nodiscard]] int threads() const {
        if (const auto* v = option("Threads")) {
            try {
                return std::max(1, std::stoi(*v));
            } catch (...) {
            }
        }
        return 1;
    }
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EngineEntry, name, cmd, args, dir, restart, options, id_name, id_author,
                                                uci_options, groups, elo, check_ok, check_message, check_when)

enum class MatchMode { Fixed = 0, Infinite = 1, Sprt = 2 };
enum class TournamentFormat { Match = 0, RoundRobin = 1, Gauntlet = 2 };

// One engine of a round robin or gauntlet, with what it does differently from the others.
struct ParticipantSetup {
    std::string name;        // engine list entry
    int hash         = 0;    // MB, 0 = the common setting (or the engine's own)
    int threads      = 0;    // 0 = the common setting (or the engine's own)
    int time_percent = 100;  // time odds: share of the time control it gets
    uint64_t nodes   = 0;    // fixed nodes: 0 = the common value
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ParticipantSetup, name, hash, threads, time_percent, nodes)
enum class LimitKind { Time = 0, Nodes = 1 };
enum class PgnStyle { Fastchess = 0, ChessBase = 1 };

struct MatchSetup {
    TournamentFormat format = TournamentFormat::Match;
    std::string name;  // tournament name, empty = made from the engines

    std::string engine_a;  // match: the engine under test, statistics are from its point of view
    std::string engine_b;  // match: the reference

    std::vector<ParticipantSetup> participants;  // round robin and gauntlet, seeds first
    int cycles         = 1;                      // every pairing is played 2 x cycles times
    int seeds          = 1;                      // gauntlet: the first `seeds` participants play all the others
    int common_hash    = 0;  // MB, forced on every participant unless overridden; 0 = each engine's own
    int common_threads = 0;

    MatchMode mode         = MatchMode::Sprt;
    int fixed_games        = 1000;
    double sprt_elo0       = 0.0;
    double sprt_elo1       = 3.0;
    double sprt_alpha      = 0.05;
    double sprt_beta       = 0.05;
    std::string sprt_model = "normalized";
    int sprt_max_games     = 0;  // 0 = no limit

    LimitKind limit  = LimitKind::Nodes;
    double tc_base_s = 10.0;
    double tc_inc_s  = 0.1;
    int tc_moves     = 0;  // moves per period, 0 = whole game
    int tc_margin_ms = 0;
    uint64_t nodes   = 200000;

    bool concurrency_auto = true;
    int concurrency       = 1;
    bool affinity         = false;

    std::string book;
    bool book_random = true;
    int book_plies   = -1;
    int book_start   = 1;
    uint64_t seed    = 0;  // 0 = new seed for every run

    bool draw_enabled   = false;
    int draw_movenumber = 40;
    int draw_movecount  = 8;
    int draw_score      = 10;

    bool resign_enabled  = false;
    int resign_movecount = 3;
    int resign_score     = 600;
    bool resign_twosided = false;

    bool maxmoves_enabled = false;
    int maxmoves          = 200;

    bool tb_enabled = false;
    std::string tb_paths;
    int tb_pieces    = 0;  // 0 = every piece count the tables cover
    bool tb_ignore50 = false;
    int tb_result    = 3;  // 1 win/loss, 2 draw, 3 both

    std::string pgn_event = "fastchess-gui";
    PgnStyle export_style = PgnStyle::ChessBase;
    bool pgn_nodes        = false;
    bool pgn_seldepth     = false;
    bool pgn_nps          = false;
    bool pgn_pv           = false;

    bool recover = true;
    bool notify  = true;

    std::string language = "auto";    // "auto" (system), "en" or "fr"
    std::string theme    = "system";  // "system", "dark" or "light"
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    MatchSetup, format, name, engine_a, engine_b, participants, cycles, seeds, common_hash, common_threads, mode,
    fixed_games, sprt_elo0, sprt_elo1, sprt_alpha, sprt_beta, sprt_model, sprt_max_games, limit, tc_base_s, tc_inc_s,
    tc_moves, tc_margin_ms, nodes, concurrency_auto, concurrency, affinity, book, book_random, book_plies, book_start,
    seed, draw_enabled, draw_movenumber, draw_movecount, draw_score, resign_enabled, resign_movecount, resign_score,
    resign_twosided, maxmoves_enabled, maxmoves, tb_enabled, tb_paths, tb_pieces, tb_ignore50, tb_result, pgn_event,
    export_style, pgn_nodes, pgn_seldepth, pgn_nps, pgn_pv, recover, notify, language, theme)

// A tournament waiting to be started after the running one.
struct QueuedTournament {
    std::string label;
    MatchSetup setup;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(QueuedTournament, label, setup)

}  // namespace fcg
