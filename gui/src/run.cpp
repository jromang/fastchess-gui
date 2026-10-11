#include "run.hpp"

#include <cmath>
#include <ctime>

#include <cli/sanitize.hpp>
#include <core/config/config.hpp>
#include <core/globals/globals.hpp>
#include <core/observer.hpp>
#include <core/rand.hpp>
#include <matchmaking/syzygy.hpp>
#include <matchmaking/tournament/roundrobin/roundrobin.hpp>
#include <types/exception.hpp>

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

fastchess::EngineConfiguration toConfig(const EngineEntry& entry, const MatchSetup& setup) {
    fastchess::EngineConfiguration config;
    config.name    = entry.name;
    config.cmd     = entry.cmd;
    config.args    = entry.args;
    config.dir     = engineWorkingDir(entry);
    config.restart = entry.restart;
    config.options = entry.options;

    if (setup.limit == LimitKind::Time) {
        config.limit.tc.time       = static_cast<int64_t>(std::llround(setup.tc_base_s * 1000.0));
        config.limit.tc.increment  = static_cast<int64_t>(std::llround(setup.tc_inc_s * 1000.0));
        config.limit.tc.moves      = setup.tc_moves;
        config.limit.tc.timemargin = setup.tc_margin_ms;
    } else {
        config.limit.nodes = setup.nodes;
    }

    return config;
}

}  // namespace

std::string makeRunDirName(const std::string& a, const std::string& b) {
    auto clean = [](std::string s) {
        for (auto& c : s) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                            c == '.' || c == '_';
            if (!ok) c = '_';
        }
        return s.substr(0, 40);
    };

    const auto now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);

    return fmt::format("{}_{}_vs_{}", stamp, clean(a), clean(b));
}

BuiltRun buildRun(const MatchSetup& setup, const std::vector<EngineEntry>& engines, const CpuTopology& topology,
                  const fs::path& run_dir) {
    const auto* a = findEngine(engines, setup.engine_a);
    const auto* b = findEngine(engines, setup.engine_b);
    if (!a || !b) throw fastchess_exception(TR("Choose two engines (A and B) from the engine list."));

    if (setup.limit == LimitKind::Time && setup.tc_base_s <= 0 && setup.tc_inc_s <= 0)
        throw fastchess_exception(TR("The time control needs a base time or an increment."));
    if (setup.limit == LimitKind::Nodes && setup.nodes == 0)
        throw fastchess_exception(TR("The number of nodes must be positive."));

    BuiltRun run;
    auto& t = run.tournament;

    t.games            = 2;
    t.report_penta     = true;
    t.output           = fastchess::OutputType::FASTCHESS;
    t.recover          = setup.recover;
    t.seed             = setup.seed != 0 ? setup.seed : fastchess::random::random_uint64();
    t.config_name      = (run_dir / "config.json").string();
    t.autosaveinterval = 20;

    switch (setup.mode) {
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

    // fastchess pins each game, both engines included, to one CPU, which only makes sense for single threaded
    // engines.
    t.affinity = setup.affinity && a->threads() == 1 && b->threads() == 1;

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

    run.engines = {toConfig(*a, setup), toConfig(*b, setup)};

    fastchess::cli::sanitize(t);
    fastchess::cli::sanitize(run.engines);

    auto& info      = run.info;
    info.engine_a   = run.engines[0].name;
    info.engine_b   = run.engines[1].name;
    info.dir        = run_dir;
    info.mode       = setup.mode;
    info.limit      = setup.limit;
    info.limit_text = fixed_nodes ? trf("{} nodes", setup.nodes)
                                  : fmt::format("{}{}+{}", setup.tc_moves > 0 ? fmt::format("{}/", setup.tc_moves) : "",
                                                formatSeconds(setup.tc_base_s), formatSeconds(setup.tc_inc_s));
    info.total_games = t.rounds >= UNBOUNDED_ROUNDS ? 0 : t.rounds * t.games;
    info.concurrency = t.concurrency;
    info.affinity    = t.affinity;
    info.sprt        = t.sprt.enabled;
    info.elo0        = t.sprt.elo0;
    info.elo1        = t.sprt.elo1;
    info.alpha       = t.sprt.alpha;
    info.beta        = t.sprt.beta;
    info.model       = t.sprt.model;
    info.penta       = t.report_penta;

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

    if (thread_.joinable()) thread_.join();

    const auto* a  = findEngine(engines, setup.engine_a);
    const auto* b  = findEngine(engines, setup.engine_b);
    const auto dir = runs_root / makeRunDirName(a ? a->name : "A", b ? b->name : "B");

    BuiltRun run;
    try {
        run = buildRun(setup, engines, topology, dir);
    } catch (const std::exception& e) {
        return e.what();
    }

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return trf("Cannot create the folder {}", dir.string());

    info_         = run.info;
    info_.started = Clock::now();
    observer_     = std::make_unique<GuiObserver>(run.tournament.concurrency, info_.engine_a, info_.engine_b, dir);

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
    fastchess::atomic::stop                 = false;
    fastchess::atomic::abnormal_termination = false;
    fastchess::config::TournamentConfig     = std::make_unique<fastchess::config::Tournament>(run.tournament);
    fastchess::config::EngineConfigs = std::make_unique<std::vector<fastchess::EngineConfiguration>>(run.engines);
    fastchess::observer::PauseGate::instance().reset();
    fastchess::observer::set(observer_.get());

    thread_ = std::thread(&RunController::threadMain, this, run.tournament, run.engines);
    return {};
}

void RunController::threadMain(fastchess::config::Tournament tournament, std::vector<fastchess::EngineConfiguration>) {
    std::string error;

    try {
        fastchess::random::seed(tournament.seed);

        if (tournament.tb_adjudication.enabled) {
            if (fastchess::initSyzygy(tournament.tb_adjudication.syzygy_dirs) == 0) {
                throw fastchess_exception(TR("Cannot load the Syzygy tablebases from ") +
                                          tournament.tb_adjudication.syzygy_dirs);
            }
        }

        fastchess::RoundRobin round_robin(fastchess::stats_map{});
        round_robin.start();
    } catch (const std::exception& e) {
        error = e.what();
    }

    // The tournament has joined its workers: no callback can arrive any more.
    fastchess::tearDownSyzygy();
    fastchess::observer::set(nullptr);
    fastchess::stopProcesses();

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
