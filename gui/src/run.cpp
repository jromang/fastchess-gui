#include "run.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <string_view>

#include <cli/sanitize.hpp>
#include <core/config/config.hpp>
#include <core/globals/globals.hpp>
#include <core/observer.hpp>
#include <core/rand.hpp>
#include <matchmaking/syzygy.hpp>
#include <types/exception.hpp>

#include <tour/polyglot.hpp>
#include <tour/runner.hpp>

#include "console.hpp"
#include "engines.hpp"
#include "i18n.hpp"

namespace fcg {

namespace fs = std::filesystem;
using fastchess::fastchess_exception;

namespace {

constexpr std::size_t UNBOUNDED_ROUNDS = 500'000'000;

const EngineEntry* findEngine(const std::vector<EngineEntry>& engines, const std::string& name) {
    for (const auto& e : engines) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

std::string formatSeconds(double s) {
    if (std::fabs(s - std::round(s)) < 1e-9) return fmt::format("{}", static_cast<int64_t>(std::round(s)));
    return fmt::format("{:g}", s);
}

bool hasOption(const EngineEntry& entry, const std::string& name) {
    for (const auto& o : entry.uci_options) {
        if (o.name == name) return true;
    }
    return false;
}

// The participant's own setting, else the common one, else what the engine entry says.
void overrideOption(fastchess::EngineConfiguration& config, const EngineEntry& entry, const std::string& name, int own,
                    int common) {
    const int value = own > 0 ? own : common;
    if (value <= 0) return;
    // An engine that does not have the option would print an error for it.
    if (!entry.uci_options.empty() && !hasOption(entry, name)) return;

    for (auto& [k, v] : config.options) {
        if (k == name) {
            v = std::to_string(value);
            return;
        }
    }
    config.options.emplace_back(name, std::to_string(value));
}

fastchess::EngineConfiguration toConfig(const EngineEntry& entry, const MatchSetup& setup,
                                        const ParticipantSetup& participant = {}) {
    fastchess::EngineConfiguration config;
    config.name     = entry.name;
    config.cmd      = entry.cmd;
    config.args     = entry.args;
    config.dir      = engineWorkingDir(entry);
    config.restart  = entry.restart;
    config.options  = entry.options;
    config.protocol = entry.protocol;
    config.variant  = setup.chess960 ? fastchess::VariantType::FRC : fastchess::VariantType::STANDARD;

    overrideOption(config, entry, "Hash", participant.hash, setup.common_hash);
    overrideOption(config, entry, "Threads", participant.threads, setup.common_threads);

    const double share = std::max(1, participant.time_percent) / 100.0;
    switch (setup.limit) {
        case LimitKind::Time:
            config.limit.tc.time       = static_cast<int64_t>(std::llround(setup.tc_base_s * 1000.0 * share));
            config.limit.tc.increment  = static_cast<int64_t>(std::llround(setup.tc_inc_s * 1000.0 * share));
            config.limit.tc.moves      = setup.tc_moves;
            config.limit.tc.timemargin = setup.tc_margin_ms;
            break;
        case LimitKind::MoveTime:
            config.limit.tc.fixed_time = std::max<int64_t>(1, std::llround(setup.move_s * 1000.0 * share));
            config.limit.tc.timemargin = setup.tc_margin_ms;
            break;
        case LimitKind::Nodes:
            config.limit.nodes = participant.nodes > 0 ? participant.nodes : setup.nodes;
            break;
        case LimitKind::Depth:
            config.limit.plies = static_cast<uint64_t>(participant.depth > 0 ? participant.depth : setup.depth);
            break;
    }

    return config;
}

std::string timestamp() {
    const auto now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    return stamp;
}

std::string limitText(const fastchess::Limit& limit) {
    if (limit.nodes > 0) return trf("{} nodes", limit.nodes);
    if (limit.plies > 0) return trf("depth {}", limit.plies);
    const auto& tc = limit.tc;
    if (tc.fixed_time > 0) return trf("{} s/move", formatSeconds(tc.fixed_time / 1000.0));
    return fmt::format("{}{}+{}", tc.moves > 0 ? fmt::format("{}/", tc.moves) : "", formatSeconds(tc.time / 1000.0),
                       formatSeconds(tc.increment / 1000.0));
}

}  // namespace

RunInfo runInfo(const tour::TournamentSpec& spec, const fs::path& dir) {
    const auto& t = spec.conditions;

    RunInfo info;
    info.format       = spec.format;
    info.name         = spec.name;
    info.participants = static_cast<int>(spec.participants.size());
    info.engine_a     = spec.participants.at(0).engine.name;
    info.engine_b     = spec.participants.at(1).engine.name;
    info.dir          = dir;
    info.mode         = t.sprt.enabled ? MatchMode::Sprt : spec.max_games > 0 ? MatchMode::Fixed : MatchMode::Infinite;
    const auto& limit = spec.participants[0].engine.limit;
    info.limit        = limit.nodes > 0           ? LimitKind::Nodes
                        : limit.plies > 0         ? LimitKind::Depth
                        : limit.tc.fixed_time > 0 ? LimitKind::MoveTime
                                                  : LimitKind::Time;
    info.limit_text   = limitText(limit);
    const auto encounters = tour::Schedule(spec).size();
    info.total_games      = encounters ? uint64_t{*encounters} * 2 : 0;
    info.concurrency      = t.concurrency;
    info.affinity         = t.affinity;
    info.sprt             = t.sprt.enabled;
    info.elo0             = t.sprt.elo0;
    info.elo1             = t.sprt.elo1;
    info.alpha            = t.sprt.alpha;
    info.beta             = t.sprt.beta;
    info.model            = t.sprt.model;
    info.penta            = true;
    return info;
}

fastchess::EngineConfiguration participantConfig(const EngineEntry& entry, const tour::TournamentSpec& spec) {
    fastchess::EngineConfiguration config;
    config.name     = entry.name;
    config.cmd      = entry.cmd;
    config.args     = entry.args;
    config.dir      = engineWorkingDir(entry);
    config.restart  = entry.restart;
    config.options  = entry.options;
    config.protocol = entry.protocol;
    if (spec.participants.empty()) return config;

    const auto& model = spec.participants.front().engine;
    config.limit      = model.limit;
    config.variant    = model.variant;
    for (const auto& [k, v] : model.options) {
        if (k != "Hash" && k != "Threads") continue;
        if (!entry.uci_options.empty() && !hasOption(entry, k)) continue;
        bool set = false;
        for (auto& [ck, cv] : config.options) {
            if (ck == k) cv = v, set = true;
        }
        if (!set) config.options.emplace_back(k, v);
    }
    return config;
}

std::optional<tour::Summary> unfinishedMatch(const fs::path& runs_root) {
    const auto dirs = tour::tournamentDirs(runs_root);
    if (dirs.empty()) return std::nullopt;

    auto summary = tour::summarize(dirs.front());
    if (!summary || summary->finished) return std::nullopt;
    if (summary->total && summary->played >= *summary->total) return std::nullopt;
    return summary;
}

std::string chess960Fen(int n) {
    std::string rank(8, ' ');
    const auto place = [&](char piece, int nth_empty) {
        for (auto& c : rank) {
            if (c != ' ') continue;
            if (nth_empty-- == 0) {
                c = piece;
                return;
            }
        }
    };
    n                   = std::clamp(n, 0, 959);
    rank[n % 4 * 2 + 1] = 'B';  // light squares: b, d, f, h
    n /= 4;
    rank[n % 4 * 2] = 'B';  // dark squares: a, c, e, g
    n /= 4;
    place('Q', n % 6);
    n /= 6;
    static constexpr int KNIGHTS[10][2] = {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {1, 2},
                                           {1, 3}, {1, 4}, {2, 3}, {2, 4}, {3, 4}};
    place('N', KNIGHTS[n][1]);  // the second one first: the first one's index still counts the same squares
    place('N', KNIGHTS[n][0]);
    place('R', 0);
    place('K', 0);
    place('R', 0);

    std::string black = rank;
    for (auto& c : black) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return black + "/pppppppp/8/8/8/8/PPPPPPPP/" + rank + " w KQkq - 0 1";
}

void writeChess960Book(const fs::path& path) {
    if (fs::is_regular_file(path)) return;
    fs::create_directories(path.parent_path());
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        for (int n = 0; n < 960; ++n) {
            const auto fen = chess960Fen(n);
            out << fen.substr(0, fen.size() - 4) << "\n";  // EPD: no move counters
        }
    }
    fs::rename(tmp, path);
}

std::string cleanName(std::string s) {
    for (auto& c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '.' || c == '_';
        if (!ok) c = '_';
    }
    return s.substr(0, 60);
}

std::string fileNameOf(std::string name) {
    for (auto& c : name) {
        if (static_cast<unsigned char>(c) < 32 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
            c = '_';
    }
    for (auto i = name.size(); i > 0 && (name[i - 1] == '.' || name[i - 1] == ' '); --i) name[i - 1] = '_';

    // CON, PRN, AUX, NUL, COM1 to COM9 and LPT1 to LPT9, in any case and with any extension, are devices.
    std::string stem = name.substr(0, name.find('.'));
    for (auto& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const bool device = stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
                        (stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) &&
                         stem[3] >= '1' && stem[3] <= '9');
    if (device) name.insert(0, "_");
    return name;
}

std::string makeRunDirName(const std::string& a, const std::string& b) {
    return fmt::format("{}_{}_vs_{}", timestamp(), cleanName(a).substr(0, 40), cleanName(b).substr(0, 40));
}

std::string makeTournamentDirName(const std::string& name) {
    return fmt::format("{}_{}", timestamp(), cleanName(name));
}

std::string defaultTournamentName(const MatchSetup& setup) {
    const auto n = setup.participants.size();
    switch (setup.format) {
        case TournamentFormat::Match:
            return fmt::format("{} vs {}", setup.engine_a, setup.engine_b);
        case TournamentFormat::RoundRobin:
            return trf("Round robin, {} engines", n);
        case TournamentFormat::Gauntlet:
            return setup.participants.empty() ? std::string(TR("Gauntlet"))
                                              : trf("Gauntlet {} vs {} engines", setup.participants[0].name,
                                                    n > 0 ? n - std::min<std::size_t>(n, std::max(1, setup.seeds)) : 0);
        case TournamentFormat::Swiss:
            return trf("Swiss, {} engines", n);
        case TournamentFormat::Knockout:
            return trf("Knockout, {} engines", n);
    }
    return {};
}

BuiltRun buildRun(const MatchSetup& setup, const std::vector<EngineEntry>& engines, const CpuTopology& topology,
                  const fs::path& run_dir) {
    // The participants, in order, with their own settings.
    std::vector<std::pair<const EngineEntry*, ParticipantSetup>> people;
    if (setup.format == TournamentFormat::Match) {
        const auto* a = findEngine(engines, setup.engine_a);
        const auto* b = findEngine(engines, setup.engine_b);
        if (!a || !b) throw fastchess_exception(TR("Choose two engines (A and B) from the engine list."));
        people = {{a, {}}, {b, {}}};
    } else {
        for (const auto& p : setup.participants) {
            const auto* e = findEngine(engines, p.name);
            if (!e) throw fastchess_exception(trf("Unknown engine: {}", p.name));
            for (const auto& [other, _] : people) {
                if (other == e) throw fastchess_exception(trf("{} is twice in the list of participants.", p.name));
            }
            people.emplace_back(e, p);
        }
        if (people.size() < 2) throw fastchess_exception(TR("Choose at least two engines."));
        if (setup.format == TournamentFormat::Gauntlet && setup.seeds >= static_cast<int>(people.size())) {
            throw fastchess_exception(TR("A gauntlet needs at least one engine besides the tested ones."));
        }
    }

    if (setup.limit == LimitKind::Time && setup.tc_base_s <= 0 && setup.tc_inc_s <= 0)
        throw fastchess_exception(TR("The time control needs a base time or an increment."));
    if (setup.limit == LimitKind::Nodes && setup.nodes == 0)
        throw fastchess_exception(TR("The number of nodes must be positive."));
    if (setup.limit == LimitKind::MoveTime && setup.move_s <= 0)
        throw fastchess_exception(TR("The time per move must be positive."));
    if (setup.limit == LimitKind::Depth && setup.depth <= 0)
        throw fastchess_exception(TR("The depth must be positive."));

    BuiltRun run;
    auto& t = run.tournament;

    t.games          = 2;
    t.report_penta   = true;
    t.output         = fastchess::OutputType::FASTCHESS;
    t.recover        = setup.recover;
    t.check_mate_pvs = setup.check_mate_pvs;
    t.seed           = setup.seed != 0 ? setup.seed : fastchess::random::random_uint64();
    t.startup_time   = ENGINE_STARTUP_TIME;
    // The tournament layer keeps the results (tournament.json, journal.jsonl): no fastchess config.json.
    t.config_name = "";

    // The game count of a round robin or a gauntlet comes from its schedule; the match settings do not apply.
    const bool match = setup.format == TournamentFormat::Match;
    switch (match ? setup.mode : MatchMode::Fixed) {
        case MatchMode::Fixed:
            t.rounds = static_cast<std::size_t>(std::max(1, (setup.fixed_games + 1) / 2));
            break;
        case MatchMode::Infinite:
            t.rounds = UNBOUNDED_ROUNDS;
            break;
        case MatchMode::Sprt:
            t.rounds =
                setup.sprt_max_games > 0 ? static_cast<std::size_t>((setup.sprt_max_games + 1) / 2) : UNBOUNDED_ROUNDS;
            t.sprt.enabled = true;
            t.sprt.elo0    = setup.sprt_elo0;
            t.sprt.elo1    = setup.sprt_elo1;
            t.sprt.alpha   = setup.sprt_alpha;
            t.sprt.beta    = setup.sprt_beta;
            t.sprt.model   = setup.sprt_model;
            break;
    }

    t.concurrency = setup.concurrency_auto ? topology.defaultConcurrency(clockFree(setup.limit)) : setup.concurrency;
    t.concurrency = std::clamp(t.concurrency, 1, topology.logical);

    run.engines.clear();
    for (const auto& [entry, participant] : people) {
        if (!fs::is_regular_file(entry->cmd)) {
            throw fastchess_exception(trf("The program of {} is not found: {}", entry->name, entry->cmd));
        }
        // An engine without UCI_Chess960 (a WinBoard one: without the fischerandom variant) would play other
        // games than the GUI. Not known before the engine was read.
        if (setup.chess960 && !entry->uci_options.empty() && !hasOption(*entry, "UCI_Chess960")) {
            throw fastchess_exception(trf("{} does not play Chess960.", entry->name));
        }
        run.engines.push_back(toConfig(*entry, setup, participant));
    }
    t.variant = setup.chess960 ? fastchess::VariantType::FRC : fastchess::VariantType::STANDARD;

    // fastchess pins each game, both engines included, to one CPU, which only makes sense for single threaded
    // engines.
    t.affinity = setup.affinity && std::all_of(run.engines.begin(), run.engines.end(),
                                               [](const auto& c) { return configThreads(c) == 1; });

    // Chess960 without a book: every starting position, in random order.
    auto book        = setup.book;
    auto book_random = setup.book_random;
    if (setup.chess960 && book.empty()) {
        book = (run_dir.parent_path() / "chess960.epd").string();
        writeChess960Book(book);
        book_random = true;
    }

    // A Polyglot book becomes the positions its lines lead to, kept in the tournament folder.
    auto book_plies = setup.book_plies;
    if (!book.empty() && tour::isPolyglot(book)) {
        if (!fs::is_regular_file(book)) throw fastchess_exception(trf("Opening book not found: {}", book));
        tour::PolyglotWalk walk;
        walk.plies = book_plies > 0 ? book_plies : POLYGLOT_PLIES;
        std::error_code ec;
        fs::create_directories(run_dir, ec);
        const auto epd = run_dir / fmt::format("{}-{}plies.epd", fs::path(book).stem().string(), walk.plies);
        try {
            (void)tour::polyglotToEpd(book, epd, walk);
        } catch (const std::exception& e) {
            throw fastchess_exception(e.what());
        }
        book       = epd.string();
        book_plies = -1;
    }

    if (!book.empty()) {
        t.opening.file = book;
        auto ext       = fs::path(book).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".pgn")
            t.opening.format = fastchess::FormatType::PGN;
        else if (ext == ".epd")
            t.opening.format = fastchess::FormatType::EPD;
        else
            throw fastchess_exception(TR("The opening book must be an .epd, .pgn or .bin (Polyglot) file."));

        if (!fs::is_regular_file(book)) throw fastchess_exception(trf("Opening book not found: {}", book));

        t.opening.order = book_random ? fastchess::OrderType::RANDOM : fastchess::OrderType::SEQUENTIAL;
        t.opening.plies = book_plies;
        t.opening.start = std::max(1, setup.book_start);
    }

    t.draw.enabled     = setup.draw_enabled;
    t.draw.move_number = static_cast<uint32_t>(std::max(0, setup.draw_movenumber));
    t.draw.move_count  = std::max(1, setup.draw_movecount);
    t.draw.score       = std::max(0, setup.draw_score);

    t.resign.enabled    = setup.resign_enabled;
    t.resign.move_count = std::max(1, setup.resign_movecount);
    t.resign.score      = std::max(0, setup.resign_score);
    t.resign.twosided   = setup.resign_twosided;

    t.maxmoves.enabled    = setup.maxmoves_enabled;
    t.maxmoves.move_count = std::max(1, setup.maxmoves);

    if (setup.tb_enabled) {
        if (setup.tb_paths.empty()) throw fastchess_exception(TR("Give the Syzygy tablebase directory."));
        t.tb_adjudication.enabled             = true;
        t.tb_adjudication.syzygy_dirs         = setup.tb_paths;
        t.tb_adjudication.max_pieces          = std::max(0, setup.tb_pieces);
        t.tb_adjudication.ignore_50_move_rule = setup.tb_ignore50;
        t.tb_adjudication.result_type =
            static_cast<fastchess::config::TbAdjudication::ResultType>(std::clamp(setup.tb_result, 1, 3));
    }

    t.pgn.event_name     = setup.pgn_event.empty() ? "fastchess-gui" : setup.pgn_event;
    t.pgn.site           = "?";
    t.pgn.track_nodes    = setup.pgn_nodes;
    t.pgn.track_seldepth = setup.pgn_seldepth;
    t.pgn.track_nps      = setup.pgn_nps;
    t.pgn.track_wdl      = true;  // only written for the engines that report it
    t.pgn.track_pv       = setup.pgn_pv;
    t.pgn.track_tbhits   = setup.pgn_tbhits;
    t.pgn.track_decision = setup.pgn_decision;

    // Without a book, fastchess warns on stdout (and in the log) at every start: the setup already says it, in the
    // language of the interface. Its only checks of the opening are those warnings, so it is shown a stand-in.
    const auto opening = t.opening;
    if (t.opening.file.empty() && t.variant != fastchess::VariantType::FRC) {
        t.opening.file   = "-";
        t.opening.format = fastchess::FormatType::EPD;
    }
    fastchess::cli::sanitize(t);
    t.opening = opening;
    fastchess::cli::sanitize(run.engines);

    if (!match) t.sprt.enabled = false;

    auto& spec  = run.spec;
    spec.name   = setup.name.empty() ? defaultTournamentName(setup) : setup.name;
    spec.format = static_cast<tour::Format>(setup.format);
    spec.cycles = setup.format == TournamentFormat::Swiss ? 1 : std::max(1, setup.cycles);
    spec.rounds = setup.format == TournamentFormat::Swiss ? std::max(1, setup.cycles) : 0;
    spec.seeds  = std::max(1, setup.seeds);
    if (match) spec.max_games = t.rounds >= UNBOUNDED_ROUNDS ? 0 : t.rounds * t.games;
    spec.log_every_game    = setup.log_every_game;
    spec.opening_per_round = setup.opening_per_round && !match && setup.format != TournamentFormat::Knockout;
    spec.pgn_out           = setup.pgn_out;
    spec.pgn_out_chessbase = setup.export_style == PgnStyle::ChessBase;
    spec.conditions        = t;
    for (std::size_t i = 0; i < run.engines.size(); ++i) {
        tour::Participant p;
        p.id     = static_cast<int>(i);
        p.engine = run.engines[i];
        p.elo    = people[i].first->elo;
        spec.participants.push_back(p);
    }

    run.info = runInfo(spec, run_dir);

    return run;
}

RunController::~RunController() {
    if (thread_.joinable()) {
        fastchess::atomic::stop = true;
        fastchess::observer::PauseGate::instance().setPaused(false);
        thread_.join();
    }
}

int configThreads(const fastchess::EngineConfiguration& config) {
    for (const auto& [k, v] : config.options) {
        if (k == "Threads") {
            try {
                return std::max(1, std::stoi(v));
            } catch (...) {
            }
        }
    }
    return 1;
}

int tablebasePieces(const fastchess::config::TbAdjudication& tb) {
    if (!tb.enabled) return 0;
    if (tb.max_pieces > 0) return tb.max_pieces;

    // Syzygy files are named after their pieces, KQvKR.rtbw: as many pieces as letters besides the "v".
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    int largest       = 0;
    std::size_t begin = 0;
    while (begin <= tb.syzygy_dirs.size()) {
        auto end = tb.syzygy_dirs.find(separator, begin);
        if (end == std::string::npos) end = tb.syzygy_dirs.size();
        std::error_code ec;
        for (fs::directory_iterator it(tb.syzygy_dirs.substr(begin, end - begin), ec), last; !ec && it != last;
             it.increment(ec)) {
            if (it->path().extension() != ".rtbw") continue;
            const auto stem = it->path().stem().string();
            largest         = std::max(largest, static_cast<int>(stem.size()) - 1);
        }
        begin = end + 1;
    }
    return largest;
}

MatchSetup quickTestSetup(const EngineEntry& engine, const std::string& opponent, const fs::path& book,
                          std::vector<EngineEntry>& engines) {
    // Five openings, one with an en passant capture available: every game sets a position with moves.
    static constexpr const char* POSITIONS[] = {
        "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq -",
        "rnbqkb1r/pppp1ppp/5n2/4p3/2P5/2N5/PP1PPPPP/R1BQKBNR w KQkq -",
        "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq -",
        "rnbqkb1r/ppp1pppp/5n2/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq -",
        "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6",
    };
    {
        std::error_code ec;
        fs::create_directories(book.parent_path(), ec);
        std::ofstream out(book, std::ios::binary | std::ios::trunc);
        for (const auto* p : POSITIONS) out << p << '\n';
    }

    MatchSetup s;
    s.format   = TournamentFormat::Match;
    s.name     = trf("Quick test: {}", engine.name);
    s.engine_a = engine.name;
    if (opponent.empty() || opponent == engine.name) {
        auto copy = engine;
        copy.name = engine.name + " (2)";
        engines.push_back(copy);
        s.engine_b = copy.name;
    } else {
        s.engine_b = opponent;
    }
    s.mode             = MatchMode::Fixed;
    s.fixed_games      = static_cast<int>(2 * std::size(POSITIONS));
    s.limit            = LimitKind::Time;
    s.tc_base_s        = 10;
    s.tc_inc_s         = 0.1;
    s.concurrency_auto = false;
    s.concurrency      = 2;
    s.book             = book.string();
    s.book_random      = false;
    s.recover          = true;  // a crash is reported, the test goes on
    s.check_mate_pvs   = true;
    s.maxmoves_enabled = true;  // games that would go on forever end, as draws
    s.maxmoves         = 150;
    s.notify           = false;
    s.pgn_event        = s.name;
    return s;
}

QuickTestReport quickTestReport(const tour::TournamentState& tournament, int id, uint64_t time_used_ms,
                                uint64_t moves) {
    int wins = 0, draws = 0, losses = 0, timeouts = 0, crashes = 0, illegal = 0, games = 0;
    for (const auto& r : tournament.results()) {
        if (r.voided || (r.white != id && r.black != id)) continue;
        games++;
        const double score = r.scoreOf(id);
        (score == 1 ? wins : score == 0 ? losses : draws)++;
        if (score != 0) continue;
        using T = fastchess::MatchTermination;
        if (r.termination == T::TIMEOUT) timeouts++;
        if (r.termination == T::DISCONNECT || r.termination == T::STALL) crashes++;
        if (r.termination == T::ILLEGAL_MOVE) illegal++;
    }

    // The clock it had: the base time of every game, and the increment of every move it made.
    const auto* p      = tournament.spec().participant(id);
    const auto tc      = p ? p->engine.limit.tc : fastchess::TimeControl::Limits{};
    const double clock = static_cast<double>(games) * static_cast<double>(tc.time) +
                         static_cast<double>(moves) * static_cast<double>(tc.increment);
    const double share = clock > 0 ? 100.0 * static_cast<double>(time_used_ms) / clock : 0;

    QuickTestReport report;
    report.ok = games > 0 && timeouts == 0 && crashes == 0 && illegal == 0;
    report.message =
        trf("{} games +{} ={} -{} · {} lost on time, {} crashes, {} illegal moves · {:.0f} % of its clock "
            "used",
            games, wins, draws, losses, timeouts, crashes, illegal, share);
    return report;
}

std::string RunController::start(const MatchSetup& setup, const std::vector<EngineEntry>& engines,
                                 const CpuTopology& topology, const fs::path& runs_root) {
    if (state_ == State::Running) return TR("A match is already running.");
    if (g_probes_in_flight > 0) return TR("An engine is being read, try again in a moment.");

    const auto dir =
        setup.format == TournamentFormat::Match
            ? runs_root / makeRunDirName(setup.engine_a.empty() ? "A" : setup.engine_a,
                                         setup.engine_b.empty() ? "B" : setup.engine_b)
            : runs_root / makeTournamentDirName(setup.name.empty() ? defaultTournamentName(setup) : setup.name);

    std::unique_ptr<tour::TournamentState> tournament;
    try {
        const auto run = buildRun(setup, engines, topology, dir);
        tournament     = tour::TournamentState::create(dir, run.spec);
    } catch (const std::exception& e) {
        return e.what();
    }

    return launch(std::move(tournament));
}

std::string RunController::resume(const fs::path& dir) {
    if (state_ == State::Running) return TR("A match is already running.");
    if (g_probes_in_flight > 0) return TR("An engine is being read, try again in a moment.");

    std::unique_ptr<tour::TournamentState> tournament;
    try {
        tournament = tour::TournamentState::open(dir);
    } catch (const std::exception& e) {
        return trf("Cannot resume the match: {}", e.what());
    }
    if (tournament->finished()) return TR("This match is already finished.");

    for (const auto& warning : tournament->warnings()) Console::instance().add(warning, true);
    return launch(std::move(tournament));
}

std::string RunController::view(const fs::path& dir) {
    if (state_ == State::Running) return TR("A match is already running.");
    if (thread_.joinable()) thread_.join();

    std::unique_ptr<tour::TournamentState> tournament;
    try {
        try {
            tournament = tour::TournamentState::open(dir);
        } catch (const tour::FolderBusy&) {
            // Played by another program (a --run in a terminal...): shown as it stands, without touching it.
            tournament = tour::TournamentState::open(dir, true);
            Console::instance().add(
                trf("{} is open in another program: it is only shown here, as it was when opened.", dir.string()),
                true);
        }
    } catch (const std::exception& e) {
        return trf("Cannot open the tournament: {}", e.what());
    }

    info_         = runInfo(tournament->spec(), tournament->dir());
    info_.started = info_.finished = Clock::now();
    info_.total_games              = tournament->totalGames().value_or(0);
    observer_       = std::make_unique<GuiObserver>(tournament->spec().conditions.concurrency, *tournament);
    tournament_     = std::move(tournament);
    stop_requested_ = false;
    stop_when_idle_ = false;
    finished_event_ = false;
    state_          = State::Finished;
    {
        std::lock_guard<std::mutex> lock(outcome_mutex_);
        error_.clear();
    }
    return {};
}

std::string RunController::applyEdit(const tour::Edit& edit) {
    if (!tournament_) return TR("No tournament is open.");
    try {
        tournament_->applyEdit(edit);
    } catch (const std::exception& e) {
        return e.what();
    }
    afterEdit();
    return {};
}

std::string RunController::undoEdit() {
    if (!tournament_) return TR("No tournament is open.");
    try {
        tournament_->undoEdit();
    } catch (const std::exception& e) {
        return e.what();
    }
    afterEdit();
    return {};
}

void RunController::afterEdit() {
    // The views know the participants by index: new ones get a name and counters.
    if (observer_) observer_->updateParticipants(tournament_->spec());
    info_.total_games  = tournament_->totalGames().value_or(0);
    info_.participants = static_cast<int>(tournament_->spec().participants.size());
}

std::string RunController::launch(std::unique_ptr<tour::TournamentState> tournament) {
    if (thread_.joinable()) thread_.join();

    const auto& spec  = tournament->spec();
    info_             = runInfo(spec, tournament->dir());
    info_.total_games = tournament->totalGames().value_or(0);  // a Swiss or a knockout plans more than it paired
    info_.started     = Clock::now();

    // The observer queues the games a resumed match already has.
    observer_   = std::make_unique<GuiObserver>(spec.conditions.concurrency, *tournament);
    tournament_ = std::move(tournament);

    {
        std::lock_guard<std::mutex> lock(outcome_mutex_);
        error_.clear();
    }

    stop_requested_ = false;
    stop_when_idle_ = false;
    finished_event_ = false;
    thread_done_    = false;
    state_          = State::Running;

    // The globals are replaced here, on the GUI thread, while nothing else reads them.
    configureFastchess(spec);

    thread_ = std::thread(&RunController::threadMain, this);
    return {};
}

void configureFastchess(const tour::TournamentSpec& spec) {
    // fastchess plays the games the runner gives it: no opening book, PGN or config file of its own.
    auto config         = spec.conditions;
    config.games        = 2;
    config.opening.file = "";
    config.pgn.file     = "";
    config.config_name  = "";
    config.output       = fastchess::OutputType::FASTCHESS;
    // fastchess does not store the timeouts in JSON: tournament.json brought back its defaults.
    config.startup_time = ENGINE_STARTUP_TIME;

    std::vector<fastchess::EngineConfiguration> engines;
    for (const auto& p : spec.participants) engines.push_back(p.engine);

    fastchess::atomic::stop                 = false;
    fastchess::atomic::abnormal_termination = false;
    fastchess::config::TournamentConfig     = std::make_unique<fastchess::config::Tournament>(config);
    fastchess::config::EngineConfigs        = std::make_unique<std::vector<fastchess::EngineConfiguration>>(engines);
    fastchess::observer::PauseGate::instance().reset();
}

std::string playTournament(tour::TournamentState& tournament, fastchess::observer::GameObserver* observer,
                           tour::RecordListener* listener) {
    std::string error;
    try {
        const auto& tb = fastchess::config::TournamentConfig->tb_adjudication;
        if (tb.enabled && fastchess::initSyzygy(tb.syzygy_dirs) == 0) {
            throw fastchess_exception(TR("Cannot load the Syzygy tablebases from ") + tb.syzygy_dirs);
        }

        // The runner is fastchess' observer while it plays, and forwards everything to ours.
        tour::Runner runner(tournament, observer, listener);
        runner.start();
        error = runner.error();
    } catch (const std::exception& e) {
        error = e.what();
    }

    // The tournament has joined its workers: no callback can arrive any more.
    fastchess::tearDownSyzygy();
    fastchess::stopProcesses();

    // The runner sets fastchess' stop flag to interrupt the games still running when it ends (SPRT decided, or
    // stopped); left set, it would make the engine probes that follow give up at once (fastchess' Windows process
    // reader checks it).
    fastchess::atomic::stop = false;
    return error;
}

void RunController::threadMain() {
    const auto error = playTournament(*tournament_, observer_.get(), observer_.get());

    {
        std::lock_guard<std::mutex> lock(outcome_mutex_);
        error_ = error;
    }

    thread_done_ = true;
}

void RunController::setPaused(bool paused) {
    if (state_ != State::Running) return;
    fastchess::observer::PauseGate::instance().setPaused(paused);
}

bool RunController::paused() const { return fastchess::observer::PauseGate::instance().paused(); }

void RunController::stopNow() {
    if (state_ != State::Running) return;

    stop_requested_         = true;
    fastchess::atomic::stop = true;
    fastchess::observer::PauseGate::instance().wakeAll();
}

void RunController::stopWhenIdle() {
    if (state_ != State::Running) return;

    stop_when_idle_ = true;
    setPaused(true);
}

int RunController::runningGames() const {
    return state_ == State::Running ? fastchess::observer::PauseGate::instance().running() : 0;
}

void RunController::update() {
    if (state_ != State::Running) return;

    // Idle means no game running and no pair waiting for its second game, so no result is thrown away.
    if (stop_when_idle_ && !stop_requested_ && runningGames() == 0 &&
        fastchess::observer::PauseGate::instance().openPairs() == 0) {
        stopNow();
    }

    if (thread_done_) {
        thread_.join();
        state_          = State::Finished;
        info_.finished  = Clock::now();
        finished_event_ = true;
        fastchess::observer::PauseGate::instance().setPaused(false);
    }
}

std::string RunController::error() const {
    std::lock_guard<std::mutex> lock(outcome_mutex_);
    return error_;
}

bool RunController::takeFinishedEvent() {
    const bool event = finished_event_;
    finished_event_  = false;
    return event;
}

}  // namespace fcg
