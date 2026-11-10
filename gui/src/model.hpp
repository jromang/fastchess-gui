#pragma once

// Plain data the GUI persists between sessions: the engine list and the last match setup.

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include <tour/model.hpp>

namespace fcg {

// Whether an engine's program is where its entry says, and still the one last read.
enum class ProgramState { Unknown, Ok, Changed, Missing, MissingDir };

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
    std::string protocol;  // "uci", "xboard" (WinBoard), empty = not known yet: found out by the next probe

    // Only the options whose value differs from the engine default.
    std::vector<std::pair<std::string, std::string>> options;

    // What the last probe reported, and the program it read (to notice a rebuild).
    tour::FileFingerprint program;
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
    std::string check_report;  // the UCI compliance steps

    // Last quick test (a short match): -1 never, 0 something went wrong, 1 passed.
    int test_ok = -1;
    std::string test_message;
    std::string test_when;

    ProgramState program_state = ProgramState::Unknown;  // not saved: refreshProgram() finds it out

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
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EngineEntry, name, cmd, args, dir, restart, protocol, options, program,
                                                id_name, id_author, uci_options, groups, elo, check_ok, check_message,
                                                check_when, check_report, test_ok, test_message, test_when)

// An opening book of the library: registered once, chosen by name in the setup.
struct BookEntry {
    std::string name;
    std::string path;
    std::string description;
    int plies = -1;  // PGN: plies kept, -1 all; Polyglot: depth, -1 the default

    // What the last reading found; positions is 0 until the book has been read.
    tour::FileFingerprint file;
    uint64_t positions = 0;
    uint64_t problems  = 0;  // illegal, duplicate, mirrored or finished positions
    bool chess960      = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BookEntry, name, path, description, plies, file, positions, problems,
                                                chess960)

enum class MatchMode { Fixed = 0, Infinite = 1, Sprt = 2 };
enum class TournamentFormat { Match = 0, RoundRobin = 1, Gauntlet = 2, Swiss = 3, Knockout = 4 };

// One engine of a round robin or gauntlet, with what it does differently from the others.
struct ParticipantSetup {
    std::string name;        // engine list entry
    int hash         = 0;    // MB, 0 = the common setting (or the engine's own)
    int threads      = 0;    // 0 = the common setting (or the engine's own)
    int time_percent = 100;  // time odds: share of the time control it gets
    uint64_t nodes   = 0;    // fixed nodes: 0 = the common value
    int depth        = 0;    // fixed depth: 0 = the common value
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ParticipantSetup, name, hash, threads, time_percent, nodes, depth)
enum class LimitKind { Time = 0, Nodes = 1, MoveTime = 2, Depth = 3 };
// A node count or a depth: the same moves however fast the machine, so no clock to share fairly.
inline bool clockFree(LimitKind limit) { return limit == LimitKind::Nodes || limit == LimitKind::Depth; }
enum class PgnStyle { Fastchess = 0, ChessBase = 1 };

struct MatchSetup {
    TournamentFormat format = TournamentFormat::Match;
    std::string name;  // tournament name, empty = made from the engines

    std::string engine_a;  // match: the engine under test, statistics are from its point of view
    std::string engine_b;  // match: the reference

    std::vector<ParticipantSetup> participants;  // round robin and gauntlet, seeds first
    // Round robin, gauntlet: every pairing is played 2 x cycles times; Swiss: number of rounds; knockout: game
    // pairs per match.
    int cycles         = 1;
    int seeds          = 1;  // gauntlet: the first `seeds` participants play all the others
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
    // The SPRT calculator's guesses about the games to come; fishtest's values for its openings by default.
    double sprt_draw_ratio = 0.49;
    double sprt_rms_bias   = 191;  // Elo

    LimitKind limit  = LimitKind::Nodes;
    double tc_base_s = 10.0;
    double tc_inc_s  = 0.1;
    int tc_moves     = 0;  // moves per period, 0 = whole game
    int tc_margin_ms = 0;  // clock and time per move
    uint64_t nodes   = 200000;
    double move_s    = 1.0;  // time per move
    int depth        = 10;   // plies

    bool concurrency_auto = true;
    int concurrency       = 1;
    bool affinity         = false;

    std::string book;
    bool book_random = true;
    int book_plies   = -1;
    int book_start   = 1;
    bool chess960    = false;  // Fischer random; without a book, the 960 starting positions
    uint64_t seed    = 0;      // 0 = new seed for every run

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
    bool pgn_tbhits       = false;
    bool pgn_decision     = false;  // dt=: when the engine settled on its move

    bool recover        = true;
    bool log_every_game = false;  // the engine log of every game, not only of those that end badly
    bool check_mate_pvs = false;  // fastchess' -check-mate-pvs: a mate score's PV must end in mate
    bool notify         = true;

    std::string language = "auto";   // "auto" (system), "en" or "fr"
    std::string theme    = "light";  // "light", "dark" or "system"
    bool tile_info       = false;    // live mosaic: the 50-move counter, material and pieces under every board

    // Broadcast.
    bool broadcast_files = false;  // live.pgn, standings.html, tournament.json
    std::string broadcast_dir;     // empty = "live" in the tournament folder
    bool broadcast_tlcs  = false;
    int tlcs_port        = 16001;
    bool broadcast_web   = false;
    int web_port         = 8080;
    int broadcast_follow = 0;  // TLCS game: 0 automatic, 1 the game shown in the Live tab
};
// More fields than NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT takes (64): its expansion, in two halves.
#define FCG_MATCH_SETUP_FIELDS_1                                                                                   \
    format, name, engine_a, engine_b, participants, cycles, seeds, common_hash, common_threads, mode, fixed_games, \
        sprt_elo0, sprt_elo1, sprt_alpha, sprt_beta, sprt_model, sprt_max_games, sprt_draw_ratio, sprt_rms_bias,   \
        limit, tc_base_s, tc_inc_s, tc_moves, tc_margin_ms, nodes, move_s, depth, concurrency_auto, concurrency,   \
        affinity, book, book_random, book_plies, book_start, chess960, seed
#define FCG_MATCH_SETUP_FIELDS_2                                                                               \
    draw_enabled, draw_movenumber, draw_movecount, draw_score, resign_enabled, resign_movecount, resign_score, \
        resign_twosided, maxmoves_enabled, maxmoves, tb_enabled, tb_paths, tb_pieces, tb_ignore50, tb_result,  \
        pgn_event, export_style, pgn_nodes, pgn_seldepth, pgn_nps, pgn_pv, pgn_tbhits, pgn_decision, recover,  \
        log_every_game, check_mate_pvs, notify, language, theme, tile_info, broadcast_files, broadcast_dir,    \
        broadcast_tlcs, tlcs_port, broadcast_web, web_port, broadcast_follow
template <typename BasicJsonType,
          nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0>
void to_json(BasicJsonType& nlohmann_json_j, const MatchSetup& nlohmann_json_t) {
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, FCG_MATCH_SETUP_FIELDS_1))
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, FCG_MATCH_SETUP_FIELDS_2))
}
template <typename BasicJsonType,
          nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0>
void from_json(const BasicJsonType& nlohmann_json_j, MatchSetup& nlohmann_json_t) {
    const MatchSetup nlohmann_json_default_obj{};
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM_WITH_DEFAULT, FCG_MATCH_SETUP_FIELDS_1))
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM_WITH_DEFAULT, FCG_MATCH_SETUP_FIELDS_2))
}
#undef FCG_MATCH_SETUP_FIELDS_1
#undef FCG_MATCH_SETUP_FIELDS_2

// A tournament waiting to be started after the running one.
struct QueuedTournament {
    std::string label;
    MatchSetup setup;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(QueuedTournament, label, setup)

}  // namespace fcg
