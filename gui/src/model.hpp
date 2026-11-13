#pragma once

// Plain data the GUI persists between sessions: the engine list and the last match setup.

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
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

    // Seconds the engine may take to answer uciok when started, and the isready of each new game; 0 = the default,
    // ENGINE_STARTUP_TIME. Saved only when set.
    int start_timeout = 0;

    // Only the options whose value differs from the engine default.
    std::vector<std::pair<std::string, std::string>> options;

    // What the last probe reported, and the program it read (to notice a rebuild).
    tour::FileFingerprint program;
    std::string id_name;
    std::string id_author;
    std::vector<UciOptionInfo> uci_options;
    std::string read_by;  // the fastchess-gui version that last read them: each version reads an empty list once

    // Organisation.
    std::vector<std::string> groups;  // e.g. a pool, a rating list
    int elo = 0;                      // reference rating, 0 = none
    std::string notes;                // free text of the user's, found by the engine searches
    // Kept in the library, but out of the engine pickers and of the checks of the whole library. A tournament that
    // has it already plays it as before.
    bool inactive = false;

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
#define FCG_ENGINE_ENTRY_FIELDS                                                                                       \
    name, cmd, args, dir, restart, protocol, options, program, id_name, id_author, uci_options, read_by, groups, elo, \
        notes, inactive, check_ok, check_message, check_when, check_report, test_ok, test_message, test_when
template <typename BasicJsonType,
          nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0>
void to_json(BasicJsonType& nlohmann_json_j, const EngineEntry& nlohmann_json_t) {
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, FCG_ENGINE_ENTRY_FIELDS))
    if (nlohmann_json_t.start_timeout > 0) nlohmann_json_j["start_timeout"] = nlohmann_json_t.start_timeout;
}
template <typename BasicJsonType,
          nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0>
void from_json(const BasicJsonType& nlohmann_json_j, EngineEntry& nlohmann_json_t) {
    const EngineEntry nlohmann_json_default_obj{};
    NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM_WITH_DEFAULT, FCG_ENGINE_ENTRY_FIELDS))
    NLOHMANN_JSON_FROM_WITH_DEFAULT(start_timeout)
}
#undef FCG_ENGINE_ENTRY_FIELDS

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

// A participant's own opening book: a Polyglot book of the library, which plays for the engine while the game is
// in it (see tour/book_adapter.hpp). The tournament's openings, if any, come first.
struct OwnBook {
    std::string path;  // empty: none, the engine plays every move
    std::string name;  // the library's name for it when it was chosen: the participant's name tells it apart
    int depth = 0;     // plies from the start of the game, the opening's included; 0 = the whole book

    [[nodiscard]] bool empty() const noexcept { return path.empty(); }
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OwnBook, path, name, depth)

// One engine of a round robin or gauntlet, with what it does differently from the others.
struct ParticipantSetup {
    std::string name;        // engine list entry
    int hash         = 0;    // MB, 0 = the common setting (or the engine's own)
    int threads      = 0;    // 0 = the common setting (or the engine's own)
    int time_percent = 100;  // time odds: share of the time control it gets
    uint64_t nodes   = 0;    // fixed nodes: 0 = the common value
    int depth        = 0;    // fixed depth: 0 = the common value
    OwnBook book     = {};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ParticipantSetup, name, hash, threads, time_percent, nodes, depth, book)
enum class LimitKind { Time = 0, Nodes = 1, MoveTime = 2, Depth = 3 };
// A node count or a depth: the same moves however fast the machine, so no clock to share fairly.
inline bool clockFree(LimitKind limit) { return limit == LimitKind::Nodes || limit == LimitKind::Depth; }
enum class PgnStyle { Fastchess = 0, ChessBase = 1 };

struct MatchSetup {
    TournamentFormat format = TournamentFormat::Match;
    std::string name;  // tournament name, empty = made from the engines

    std::string engine_a;  // match: the engine under test, statistics are from its point of view
    std::string engine_b;  // match: the reference
    OwnBook book_a = {};   // match: their own books
    OwnBook book_b = {};

    std::vector<ParticipantSetup> participants;  // round robin and gauntlet, seeds first
    // Round robin, gauntlet: every pairing is played 2 x cycles times; Swiss: number of rounds; knockout: game
    // pairs per match.
    int cycles         = 1;
    int seeds          = 1;  // gauntlet: the first `seeds` participants play all the others
    int common_hash    = 0;  // MB, forced on every participant unless overridden; 0 = each engine's own
    int common_threads = 0;
    // Forced on the engines that have the option: SyzygyPath = tb_paths, OwnBook = false.
    bool engine_syzygy      = false;
    bool engine_ownbook_off = false;

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
    bool book_random       = true;
    int book_plies         = -1;
    int book_start         = 1;
    bool chess960          = false;  // Fischer random; without a book, the 960 starting positions
    bool opening_per_round = false;  // round robin, gauntlet, Swiss: one opening for every pair of a round
    uint64_t seed          = 0;      // 0 = new seed for every run

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
    std::string pgn_out;            // a PGN file every tournament also appends its games to; empty = none

    bool recover        = true;
    bool log_every_game = false;  // the engine log of every game, not only of those that end badly
    bool check_mate_pvs = false;  // fastchess' -check-mate-pvs: a mate score's PV must end in mate
    bool notify         = true;
    bool resume_at_launch = true;  // the tournament running when the program ended, then the queue

    std::string language = "auto";       // "auto" (system), "en" or "fr"
    std::string theme    = "light";      // "light", "dark" or "system"
    int ui_zoom          = 100;          // size of the interface, in percent of the default (on top of the screen's)
    bool tile_info       = false;        // live mosaic: the 50-move counter, material and pieces under every board
    bool live_seed_side  = true;         // live boards of a gauntlet turned to the seed's side
    bool live_arrows     = true;         // live board: the PV move of the side to move and the reply it expects
    std::map<std::string, float> panes;  // the list's share of the width on list + detail pages, by page

    // Broadcast.
    bool broadcast_files = false;  // live.pgn, standings.html, tournament.json
    std::string broadcast_dir;     // empty = "live" in the tournament folder
    bool broadcast_tlcs    = false;
    int tlcs_port          = 16001;
    bool tlcs_lanes        = false;  // a port per concurrent game, from tlcs_port on
    bool broadcast_web     = false;
    int web_port           = 8080;
    int broadcast_follow   = 0;  // TLCS game: 0 automatic, 1 the game shown in the Live tab
    bool broadcast_lichess = false;
    std::string lichess_round;    // a round's URL or id; empty: a new broadcast named after the tournament
    bool lichess_listed = false;  // among lichess' community broadcasts, rather than for those with the link
};

// Gives `next`, a setup loaded from a template or started from the queue, the settings of the program that
// `current` has: its look (language, theme, size, panes, live boards), the notifications and the resume at launch.
// These follow the settings, not a stored setup.
inline void keepProgramSettings(MatchSetup& next, const MatchSetup& current) {
    next.language         = current.language;
    next.theme            = current.theme;
    next.ui_zoom          = current.ui_zoom;
    next.tile_info        = current.tile_info;
    next.live_seed_side   = current.live_seed_side;
    next.live_arrows      = current.live_arrows;
    next.panes            = current.panes;
    next.notify           = current.notify;
    next.resume_at_launch = current.resume_at_launch;
}
// More fields than NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT takes (64): its expansion, in two halves.
#define FCG_MATCH_SETUP_FIELDS_1                                                                                   \
    format, name, engine_a, engine_b, participants, cycles, seeds, common_hash, common_threads, mode, fixed_games, \
        sprt_elo0, sprt_elo1, sprt_alpha, sprt_beta, sprt_model, sprt_max_games, sprt_draw_ratio, sprt_rms_bias,   \
        limit, tc_base_s, tc_inc_s, tc_moves, tc_margin_ms, nodes, move_s, depth, concurrency_auto, concurrency,   \
        affinity, book, book_random, book_plies, book_start, chess960, opening_per_round, seed, pgn_out,           \
        engine_syzygy, engine_ownbook_off, book_a, book_b
#define FCG_MATCH_SETUP_FIELDS_2                                                                                  \
    draw_enabled, draw_movenumber, draw_movecount, draw_score, resign_enabled, resign_movecount, resign_score,    \
        resign_twosided, maxmoves_enabled, maxmoves, tb_enabled, tb_paths, tb_pieces, tb_ignore50, tb_result,     \
        pgn_event, export_style, pgn_nodes, pgn_seldepth, pgn_nps, pgn_pv, pgn_tbhits, pgn_decision, recover,     \
        log_every_game, check_mate_pvs, notify, language, theme, ui_zoom, tile_info, live_seed_side, live_arrows, \
        panes, broadcast_files, broadcast_dir, broadcast_tlcs, tlcs_port, tlcs_lanes, broadcast_web, web_port,    \
        broadcast_follow, broadcast_lichess, lichess_round, lichess_listed, resume_at_launch
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

// The tournament the window was running, kept while it runs (running.json), so that one still there when the
// program starts again was interrupted: by a crash, a power cut, a reboot, or by quitting while it ran.
struct RunningMarker {
    std::string dir;
    bool paused = false;  // the user paused it: it waits for them at the next launch too
    // Resumed at launch and not yet seen playing for a while: if the program ends meanwhile (the resume crashed
    // it), the next launch does not resume it again.
    bool resuming = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RunningMarker, dir, paused, resuming)

// The marker names the tournament in `dir`: another window of the profile may have written its own since.
[[nodiscard]] inline bool markerNames(const std::optional<RunningMarker>& marker, const std::string& dir) {
    return marker && marker->dir == dir;
}

// What to do at launch about the tournament running when the program ended.
enum class LaunchResume {
    Nothing,  // it waits for the user, the marker removed
    Resume,
    Queue,    // it is over: the queue starts, the marker removed
    Busy,     // another window plays it: its marker, left alone
    Crashed,  // the program ended while it resumed it at the last launch: it waits for the user, the marker removed
};

// Resume it when the setting allows and it was not paused, unless --autostart starts the setup; when it is over or
// gone (it ended as the program did), the queue starts instead. `busy`: another program holds its folder.
[[nodiscard]] inline LaunchResume launchResume(bool enabled, bool autostart, const std::optional<RunningMarker>& marker,
                                               bool unfinished, bool queue_empty, bool busy = false) {
    if (!marker) return LaunchResume::Nothing;
    if (busy) return LaunchResume::Busy;
    if (!enabled || autostart || marker->paused) return LaunchResume::Nothing;
    if (unfinished) return marker->resuming ? LaunchResume::Crashed : LaunchResume::Resume;
    return queue_empty ? LaunchResume::Nothing : LaunchResume::Queue;
}

// Whether the marker stays when the run ends: only when the window quits while it runs, to be resumed at the next
// launch. A stop the user asked for removes it, even if they quit before the engines stopped.
[[nodiscard]] inline bool keepMarkerAtEnd(bool quitting, bool stopped_by_user) { return quitting && !stopped_by_user; }

// What a start (of a tournament, a resume, the queue, a quick test) does about the engine read and the check in flight.
// Both read fastchess' configuration, which the start replaces, so neither may go on while it starts. The start waits
// only for what it needs: the read of an engine it plays from the library (`needs`), whose options, protocol and name
// come from that read; a read still queued for one of them is done first. Any other read, and any check, is
// interrupted and done again later. A resume needs nothing from the library: it plays the engines copied in its
// folder. While a test suite runs, it waits for its end: the suite's searches read that configuration too, and the
// end of a tournament ends every engine process, the suite's with them. It also waits for the participants' PGN books
// being turned into Polyglot books (book_jobs.hpp), which involve no engine.
struct StartGate {
    enum class Action {
        Start,      // nothing to wait for
        Wait,       // for the read (or the check) of `engine`, for a book being prepared, or for the test suite
        Read,       // reads `engine` first, then waits for it
        Interrupt,  // ends the read and/or the check in flight, which are done again later
    };
    Action action = Action::Start;
    std::string engine;
    bool check           = false;  // Wait: `engine` is being checked, not read
    bool book            = false;  // Wait: for the book of the participant `engine`, being prepared
    bool suite           = false;  // Wait: for the test suite
    bool interrupt_read  = false;
    bool interrupt_check = false;
};

// The library engines a setup plays: A and B of a match, the participants otherwise.
[[nodiscard]] inline std::vector<std::string> enginesOfSetup(const MatchSetup& setup) {
    if (setup.format == TournamentFormat::Match) return {setup.engine_a, setup.engine_b};
    std::vector<std::string> names;
    for (const auto& p : setup.participants) names.push_back(p.name);
    return names;
}

// `reading` and `checking`: the engines being read and checked, empty when none; `queued`: those waiting to be read;
// `books`: the participants whose PGN books the start needs and that are still being turned into Polyglot books.
[[nodiscard]] inline StartGate startGate(const std::vector<std::string>& needs, const std::string& reading,
                                         const std::string& checking, const std::vector<std::string>& queued,
                                         bool suite_running, const std::vector<std::string>& books = {}) {
    const auto needed = [&](const std::string& name) {
        return std::find(needs.begin(), needs.end(), name) != needs.end();
    };
    StartGate gate;
    // Nothing is interrupted meanwhile: an interruption would end the suite's engines too.
    if (suite_running) {
        gate.action = StartGate::Action::Wait;
        gate.suite  = true;
        return gate;
    }
    if (!reading.empty() && needed(reading)) {
        gate.action = StartGate::Action::Wait;
        gate.engine = reading;
        return gate;
    }
    // A read it needs, while nothing is read nor checked: started now, the books go on meanwhile.
    if (reading.empty() && checking.empty()) {
        if (const auto it = std::find_if(queued.begin(), queued.end(), needed); it != queued.end()) {
            gate.action = StartGate::Action::Read;
            gate.engine = *it;
            return gate;
        }
    }
    // A book being prepared: the reads and checks in flight may end meanwhile, rather than be interrupted for
    // nothing.
    if (!books.empty()) {
        gate.action = StartGate::Action::Wait;
        gate.engine = books.front();
        gate.book   = true;
        return gate;
    }
    // Ended only when nothing it needs is in flight: an interruption ends every engine process of the reads and checks.
    if (!reading.empty() || !checking.empty()) {
        gate.action          = StartGate::Action::Interrupt;
        gate.interrupt_read  = !reading.empty();
        gate.interrupt_check = !checking.empty();
    }
    return gate;
}

// A PGN book a start needs turned into a Polyglot book first (tour/pgn_book.hpp): a participant's own book.
struct OwnBookNeed {
    std::string participant;  // the name it plays under
    std::string path;
    int depth     = 0;
    bool chess960 = false;
};

// A start put off (see startGate). `key` names what it starts: "start", "queue", "resume:" and the folder, "test:"
// and the engine of a quick test; `needs`: the engines whose reads it waits for; `books`: the PGN books it waits for.
struct StartRequest {
    std::string key;
    std::vector<std::string> needs;
    std::vector<OwnBookNeed> books;

    // An engine renamed while it waits (the first read of an engine gives it its id name): what it needs follows, and
    // so does the quick test of that engine. True when the key changed: the start must then find the engine under
    // its new name.
    bool rename(const std::string& old, const std::string& now) {
        for (auto& name : needs)
            if (name == old) name = now;
        if (key != "test:" + old) return false;
        key = "test:" + now;
        return true;
    }
};

// What a start asked for does while another one waits (`waiting`, the key of the start put off, empty when none).
enum class StartAsked {
    Go,       // nothing waits: it starts, or waits for what it needs itself
    Waiting,  // the same start, asked again: it goes on waiting, as it was
    Refused,  // another start waits: it is not dropped for this one, which does not start (Cancel it first)
};
[[nodiscard]] inline StartAsked startAsked(const std::string& waiting, const std::string& key) {
    if (waiting.empty()) return StartAsked::Go;
    return waiting == key ? StartAsked::Waiting : StartAsked::Refused;
}

// A test suite starts only when no tournament (nor quick test) runs and no start waits: it would replace nothing,
// but a tournament's end ends every engine process, the suite's too, and a start waiting for the suite would wait
// for it.
[[nodiscard]] inline bool suiteMayStart(bool tournament_running, bool start_waiting) {
    return !tournament_running && !start_waiting;
}

// The queue starts its tournaments one by one, the first it can from `from` on. A start waits only for the reads of
// the engines of the one it is about to start, never for those of the tournaments after it.
[[nodiscard]] inline std::vector<std::string> queueNeeds(const std::vector<QueuedTournament>& queue,
                                                         std::size_t index) {
    return index < queue.size() ? enginesOfSetup(queue[index].setup) : std::vector<std::string>{};
}

// After the tournament at `index` could not start (the options of one of its engines cannot be read, or the start
// refused it): it stays in the queue, and a queue going on by itself tries the next one; "Start now" tries only the
// first. None when nothing is left to try.
[[nodiscard]] inline std::optional<std::size_t> queueNextAfterFailure(std::size_t index, std::size_t size, bool go_on) {
    if (!go_on || index + 1 >= size) return std::nullopt;
    return index + 1;
}

}  // namespace fcg
