#include "run.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

#include <cli/sanitize.hpp>
#include <core/config/config.hpp>
#include <core/globals/globals.hpp>
#include <core/observer.hpp>
#include <core/rand.hpp>
#include <matchmaking/syzygy.hpp>
#include <types/exception.hpp>

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
    config.name    = entry.name;
    config.cmd     = entry.cmd;
    config.args    = entry.args;
    config.dir     = engineWorkingDir(entry);
    config.restart = entry.restart;
    config.options = entry.options;

    overrideOption(config, entry, "Hash", participant.hash, setup.common_hash);
    overrideOption(config, entry, "Threads", participant.threads, setup.common_threads);

    if (setup.limit == LimitKind::Time) {
        const double share         = std::max(1, participant.time_percent) / 100.0;
        config.limit.tc.time       = static_cast<int64_t>(std::llround(setup.tc_base_s * 1000.0 * share));
        config.limit.tc.increment  = static_cast<int64_t>(std::llround(setup.tc_inc_s * 1000.0 * share));
        config.limit.tc.moves      = setup.tc_moves;
        config.limit.tc.timemargin = setup.tc_margin_ms;
    } else {
        config.limit.nodes = participant.nodes > 0 ? participant.nodes : setup.nodes;
    }

    return config;
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

std::string cleanName(std::string s) {
    for (auto& c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '.' || c == '_';
        if (!ok) c = '_';
    }
    return s.substr(0, 60);
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
    const auto& tc = limit.tc;
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
    info.limit        = spec.participants[0].engine.limit.nodes > 0 ? LimitKind::Nodes : LimitKind::Time;
    info.limit_text   = limitText(spec.participants[0].engine.limit);
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
    config.name    = entry.name;
    config.cmd     = entry.cmd;
    config.args    = entry.args;
    config.dir     = engineWorkingDir(entry);
    config.restart = entry.restart;
    config.options = entry.options;
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

    BuiltRun run;
    auto& t = run.tournament;

    t.games        = 2;
    t.report_penta = true;
    t.output       = fastchess::OutputType::FASTCHESS;
    t.recover      = setup.recover;
    t.seed         = setup.seed != 0 ? setup.seed : fastchess::random::random_uint64();
    t.startup_time = ENGINE_STARTUP_TIME;
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

    const bool fixed_nodes = setup.limit == LimitKind::Nodes;
    t.concurrency          = setup.concurrency_auto ? topology.defaultConcurrency(fixed_nodes) : setup.concurrency;
    t.concurrency          = std::clamp(t.concurrency, 1, topology.logical);

    run.engines.clear();
    for (const auto& [entry, participant] : people) run.engines.push_back(toConfig(*entry, setup, participant));

    // fastchess pins each game, both engines included, to one CPU, which only makes sense for single threaded
    // engines.
    t.affinity = setup.affinity && std::all_of(run.engines.begin(), run.engines.end(),
                                               [](const auto& c) { return configThreads(c) == 1; });

    if (!setup.book.empty()) {
        t.opening.file = setup.book;
        auto ext       = fs::path(setup.book).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".pgn")
            t.opening.format = fastchess::FormatType::PGN;
        else if (ext == ".epd")
            t.opening.format = fastchess::FormatType::EPD;
        else
            throw fastchess_exception(TR("The opening book must be an .epd or .pgn file."));

        if (!fs::is_regular_file(setup.book)) throw fastchess_exception(trf("Opening book not found: {}", setup.book));

        t.opening.order = setup.book_random ? fastchess::OrderType::RANDOM : fastchess::OrderType::SEQUENTIAL;
        t.opening.plies = setup.book_plies;
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
    t.pgn.track_pv       = setup.pgn_pv;

    fastchess::cli::sanitize(t);
    fastchess::cli::sanitize(run.engines);

    if (!match) t.sprt.enabled = false;

    auto& spec  = run.spec;
    spec.name   = setup.name.empty() ? defaultTournamentName(setup) : setup.name;
    spec.format = static_cast<tour::Format>(setup.format);
    spec.cycles = setup.format == TournamentFormat::Swiss ? 1 : std::max(1, setup.cycles);
    spec.rounds = setup.format == TournamentFormat::Swiss ? std::max(1, setup.cycles) : 0;
    spec.seeds  = std::max(1, setup.seeds);
    if (match) spec.max_games = t.rounds >= UNBOUNDED_ROUNDS ? 0 : t.rounds * t.games;
    spec.conditions = t;
    for (std::size_t i = 0; i < run.engines.size(); ++i) {
        tour::Participant p;
        p.id     = static_cast<int>(i);
        p.engine = run.engines[i];
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
        tournament = tour::TournamentState::open(dir);
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
