#include "runner.hpp"

#include <chrono>
#include <fstream>
#include <thread>

#include <fmt/include/fmt/format.h>

#include <core/globals/globals.hpp>
#include <core/logger/logger.hpp>
#include <engine/process/iprocess.hpp>

namespace fcg::tour {

namespace {

// The game this thread is playing, and whether it was recorded. playGame() tells the observer about the end of
// the game on the thread that played it, right before it would score it.
thread_local const GameTask* t_task = nullptr;
thread_local bool t_recorded        = false;
// Whether both engines of that game started (fastchess told their processes).
thread_local bool t_started = false;
// Whether the operator ended that game (TournamentState::requestEnd()).
thread_local bool t_ended = false;

// What counts that game as running in the pause gate; empty while a pause holds the next start try of its engine.
using Ticket                                   = fastchess::observer::PauseGate::Ticket;
thread_local std::unique_ptr<Ticket>* t_ticket = nullptr;

// What fastchess logged about an engine of that game that failed to start, if one did.
thread_local std::optional<StartFailureLine> t_failure;

// fastchess' log on its way to the sink that was there before the runner's (the GUI's console): which engine of the
// game played on this thread failed to start, and why. fastchess logs it on that thread, right after it asked
// gameWanted() and before it asks again.
std::atomic<fastchess::Logger::PrintSink> g_next_sink{nullptr};

void watchLog(fastchess::Logger::Level level, std::string_view message) {
    if (t_task && level >= fastchess::Logger::Level::WARN) {
        if (auto failure = parseStartFailure(message);
            failure && (failure->engine == t_task->white.name || failure->engine == t_task->black.name)) {
            t_failure = std::move(failure);
        }
    }
    if (const auto next = g_next_sink.load()) next(level, message);
}

// The game as the operator ended it (TournamentState::requestEnd()): their result, an adjudication. fastchess ended
// it when the GUI stopped the engine on move (a disconnection), or on its own just before.
fastchess::MatchData endedByOperator(fastchess::MatchData data, const std::string& result) {
    using chess::GameResult;
    data.players.white.result = result == "1-0"   ? GameResult::WIN
                                : result == "0-1" ? GameResult::LOSE
                                                  : GameResult::DRAW;
    data.players.black.result = result == "1-0"   ? GameResult::LOSE
                                : result == "0-1" ? GameResult::WIN
                                                  : GameResult::DRAW;
    data.termination          = fastchess::MatchTermination::ADJUDICATION;
    data.reason               = OPERATOR_REASON;
    return data;
}

}  // namespace

Runner::Runner(TournamentState& state, fastchess::observer::GameObserver* downstream, RecordListener* listener)
    : BaseTournament(fastchess::stats_map{}),
      state_(state),
      downstream_(downstream),
      listener_(listener),
      log_every_game_(state.spec().log_every_game) {
    // What the engines write on their error output goes to their logs.
    fastchess::engine::process::capture_stderr = true;
}

Runner::~Runner() {
    // Workers may still be inside a callback of ours: join them before our members go away.
    pool_.kill();
    if (fastchess::observer::get() == this) fastchess::observer::set(previous_);
    if (fastchess::Logger::print_sink_.load() == &watchLog) fastchess::Logger::setPrintSink(g_next_sink.load());
}

void Runner::start() {
    previous_ = fastchess::observer::get();
    fastchess::observer::set(this);
    g_next_sink = fastchess::Logger::print_sink_.load();
    fastchess::Logger::setPrintSink(&watchLog);

    auto& failures = state_.startFailures();
    failures.setStalled(false);

    startNext();

    using namespace std::chrono_literals;
    auto last              = StartFailures::Clock::now();
    uint64_t failures_seen = failures.version();
    while (!fastchess::atomic::stop) {
        // A pause holds the next tries of the engines that failed to start: the time it lasts does not count.
        const auto now = StartFailures::Clock::now();
        if (fastchess::observer::PauseGate::instance().paused()) failures.postpone(now - last);
        last = now;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (in_flight_ == 0 && !state_.hasWork()) break;
        }
        // A withdrawal may have dropped the game a pair was waiting for.
        closeSettledPairs();

        // A larger allowance: the games it lets start, now.
        if (allowance_changed_.exchange(false)) startNext();

        // While engines cannot start, the games set aside come back as their engines are due for a try or start
        // again, on the threads left free meanwhile.
        if (!failures.empty() || failures.version() != failures_seen) {
            failures_seen = failures.version();
            startNext();
            if (stall()) break;
        }
        std::this_thread::sleep_for(20ms);
    }

    // Interrupted games end quickly once stop is set; nothing is called back after this.
    pool_.kill();
    fastchess::Logger::setPrintSink(g_next_sink.load());
    fastchess::observer::set(previous_);
}

bool Runner::stall() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (in_flight_ > 0) return false;
    }
    // A paused tournament waits for its resume, a tournament stopping once idle for its stop.
    auto& failures = state_.startFailures();
    if (fastchess::observer::PauseGate::instance().paused() || failures.empty() || failures.deciding() ||
        !state_.hasWork()) {
        return false;
    }
    // Each engine gets one more try before the tournament stops for it (claimed at the next round of the loop).
    if (failures.lastChance(StartFailures::Clock::now())) return false;

    failures.setStalled(true);
    report(StartFailureEvent::Kind::Stalled, failures.engines());
    return true;
}

void Runner::startNext() {
    std::lock_guard<std::mutex> lock(mutex_);

    while (!fastchess::atomic::stop && in_flight_ < static_cast<int>(pool_.getNumThreads())) {
        // The game found starts only if it fits the allowance with the games in flight (asked under the state's lock,
        // before it is claimed: a game held back is not claimed, and comes first next time).
        GameCost cost;
        Wait held = Wait::None;
        std::optional<GameTask> task;
        try {
            task = state_.claimNext([&](const Participant& first, const Participant& second) {
                cost = gameCost(first, second);
                held = allowance_.admits(in_flight_cost_, in_flight_, cost);
                return held == Wait::None;
            });
        } catch (const std::exception& e) {
            fail(e.what());
            return;
        }
        waiting_ = task ? Wait::None : held;
        if (!task) return;

        ++in_flight_;
        in_flight_cost_ += cost;
        pool_.enqueue([this, task = std::move(*task), cost]() { play(task, cost); });
    }
}

void Runner::setAllowance(const Allowance& allowance) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (allowance_ == allowance) return;
        allowance_ = allowance;
    }
    allowance_changed_ = true;
}

Allowance Runner::allowance() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allowance_;
}

GameCost Runner::inFlight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return in_flight_cost_;
}

int Runner::gamesInFlight() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return in_flight_;
}

bool Runner::gameWanted(std::size_t /*game_id*/) const {
    // A game waiting for a resume that will not come would keep "stop when idle" waiting forever.
    if (fastchess::observer::PauseGate::instance().stoppingWhenIdle()) return false;
    // fastchess said which engine failed to start: the runner tries it again itself (play()), its other games set
    // aside meanwhile, rather than let this thread wait for the tries and then stop the tournament.
    if (t_failure) return false;
    return !t_task || state_.wanted(*t_task);
}

bool Runner::gameHeld(std::size_t /*game_id*/) const {
    auto& gate      = fastchess::observer::PauseGate::instance();
    const bool held = gate.paused();

    // Asked on the game's thread while it waits for the next try of an engine. Nothing is played during that wait,
    // so a pause stops counting the game as running (the paused tournament would show it finishing until the
    // resume), and the resume counts it again before the try. The pair it entered stays as it is. "Stop when idle"
    // gives the game up instead (gameWanted()).
    if (t_ticket && t_task) {
        if (held && *t_ticket && !gate.stoppingWhenIdle()) {
            t_ticket->reset();
        } else if (!held && !*t_ticket) {
            t_ticket->reset(new Ticket(gate.enter(t_task->encounter.index, 1)));
        }
    }
    return held;
}

void Runner::play(const GameTask& task, const GameCost& cost) {
    t_task     = &task;
    t_recorded = false;
    t_started  = false;
    t_failure.reset();
    t_ended = false;

    try {
        // A paused tournament lets running games (and the other game of their pair) finish but starts no new pair.
        // The pair is closed by closeSettledPairs() when its other game was played before, or will not be.
        std::unique_ptr<Ticket> ticket(
            new Ticket(fastchess::observer::PauseGate::instance().enter(task.encounter.index, 2)));
        t_ticket = &ticket;
        struct Forget {
            ~Forget() { t_ticket = nullptr; }
        } forget;

        const fastchess::GamePair<fastchess::EngineConfiguration, fastchess::EngineConfiguration> configs{task.white,
                                                                                                          task.black};
        const auto start  = []() {};
        const auto finish = [](const fastchess::Stats&, const std::string&, const fastchess::engines&) {};

        playGame(configs, start, finish, task.opening, task.encounter.index, task.number() + 1);
    } catch (const std::exception& e) {
        fail(e.what());
    }

    const auto failure = std::move(t_failure);
    t_failure.reset();
    if (failure && !t_started) startFailed(task, *failure);

    // A game given up while an engine of it failed to start leaves that engine in fastchess' cache as if it had
    // started (fastchess makes a new process only to try again), so the next game to take it would lose at once by
    // disconnection. Its processes are ended: that game starts the engine anew. Once fastchess said which engine it
    // was, the other one, started or never tried, is kept.
    if (!t_started) {
        retireEngines([&](const std::string& name) {
            return failure ? name == failure->engine : name == task.white.name || name == task.black.name;
        });
    }

    if (!t_recorded) state_.release(task);
    t_task = nullptr;
    closeSettledPairs();
    // Nor did that game begin its pair: with no other game of it played or being played, the pair is closed, so that
    // a pause holds it back again and "stop when idle" does not wait for it while its engine is set aside.
    if (failure && !t_started) {
        fastchess::observer::PauseGate::instance().closeSettled([&](std::uint64_t encounter) {
            return encounter == task.encounter.index && !state_.begun(task.encounter.index);
        });
    }

    // A game the operator ended: the GUI stopped the process of an engine, which may have ended the game a moment
    // after fastchess did, and stay in its cache for the next game. Both engines of the game start anew. A request
    // the end of the game came too early to take is dropped.
    if (state_.takeEnd(task.number()) || t_ended) {
        retireEngines([&](const std::string& name) { return name == task.white.name || name == task.black.name; });
    }

    // The processes kept for engines with no game left (withdrawn or replaced participants, the opponents of a
    // gauntlet once they played) are not needed any more; the idle processes kept are sized on the engines left.
    const auto left = state_.enginesLeft();
    retireEngines([&](const std::string& name) { return !left.count(name); });
    setEnginesLeft(std::max<std::size_t>(1, left.size()));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        --in_flight_;
        in_flight_cost_ -= cost;
    }
    startNext();
}

void Runner::closeSettledPairs() {
    // Otherwise a pair whose other game was played before a resume, voided without replay or dropped by a
    // withdrawal would stay open, and "stop when idle" would wait for it forever.
    fastchess::observer::PauseGate::instance().closeSettled(
        [this](std::uint64_t encounter) { return state_.settled(static_cast<uint32_t>(encounter)); });
}

void Runner::startFailed(const GameTask& task, const StartFailureLine& failure) {
    const bool white   = failure.engine == task.white.name;
    const int id       = (task.game == 0) == white ? task.encounter.first : task.encounter.second;
    const auto& engine = white ? task.white : task.black;

    // What the engines of the game exchanged until then, the engine's error output first: what its author needs. A
    // log that cannot be written must not stop the tournament.
    const auto path = engineStartLogPath(state_.dir(), failure.engine);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << log(fastchess::observer::slot()).startText(engine, failure.error);
    out.close();

    auto& failures     = state_.startFailures();
    const auto outcome = failures.failed(id, task.number(), failure.engine, failure.error, StartFailures::Clock::now(),
                                         out ? path : std::filesystem::path());
    if (outcome == StartFailures::Outcome::Same) return;

    using Kind            = StartFailureEvent::Kind;
    const auto engine_now = failures.engine(id);
    if (outcome == StartFailures::Outcome::Failed && state_.spec().stop_on_start_failure) {
        report(Kind::Stopped, engine_now);
        fail(fmt::format("{} cannot start: {}", failure.engine, failure.error));
        return;
    }
    report(outcome == StartFailures::Outcome::Retry    ? Kind::Retry
           : outcome == StartFailures::Outcome::Failed ? Kind::SetAside
                                                       : Kind::StillFailing,
           engine_now);
}

void Runner::report(StartFailureEvent::Kind kind, const std::optional<StartFailures::Engine>& engine) {
    if (engine) report(kind, std::vector<StartFailures::Engine>{*engine});
}

void Runner::report(StartFailureEvent::Kind kind, std::vector<StartFailures::Engine> engines) {
    if (!listener_ || engines.empty()) return;
    StartFailureEvent event;
    event.kind    = kind;
    event.engines = std::move(engines);
    listener_->onStartFailure(event);
}

void Runner::fail(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        if (error_.empty()) error_ = message;
    }
    fastchess::atomic::abnormal_termination = true;
    fastchess::atomic::stop                 = true;
    fastchess::observer::PauseGate::instance().wakeAll();
}

std::string Runner::error() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return error_;
}

EngineLog& Runner::log(int slot) {
    std::lock_guard<std::mutex> lock(logs_mutex_);
    auto& log = logs_[slot];
    if (!log) log = std::make_unique<EngineLog>();
    return *log;
}

void Runner::writeLog(int slot, uint64_t game_number, const fastchess::MatchData& data) {
    // A log that cannot be written must not stop the tournament.
    const auto path = engineLogPath(state_.dir(), game_number);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << log(slot).text(game_number, data);
}

void Runner::onGameStart(const fastchess::observer::GameStart& info) {
    log(info.slot).start();
    if (downstream_) downstream_->onGameStart(info);
}

void Runner::onEngineProcesses(int slot, int64_t white_pid, int64_t black_pid) {
    t_started = true;
    // An engine that failed to start before is back: its games are played again.
    if (auto& failures = state_.startFailures(); t_task && !failures.empty()) {
        for (const int id : {t_task->encounter.first, t_task->encounter.second}) {
            const auto before = failures.engine(id);
            if (failures.started(id)) report(StartFailureEvent::Kind::Started, before);
        }
    }
    if (downstream_) downstream_->onEngineProcesses(slot, white_pid, black_pid);
}

void Runner::onThinking(int slot, chess::Color stm, int64_t time_left_ms) {
    if (downstream_) downstream_->onThinking(slot, stm, time_left_ms);
}

void Runner::onEngineLine(int slot, chess::Color side, std::string_view line) {
    log(slot).add(side, EngineLog::Stream::Output, line);
    if (downstream_) downstream_->onEngineLine(slot, side, line);
}

void Runner::onEngineError(int slot, chess::Color side, std::string_view line) {
    log(slot).add(side, EngineLog::Stream::Error, line);
    if (downstream_) downstream_->onEngineError(slot, side, line);
}

void Runner::onEngineInput(int slot, chess::Color side, std::string_view line) {
    log(slot).add(side, EngineLog::Stream::Input, line);
    if (downstream_) downstream_->onEngineInput(slot, side, line);
}

void Runner::onMove(int slot, chess::Color mover, const fastchess::MoveData& move, const std::string& san,
                    int64_t white_time_left, int64_t black_time_left) {
    if (downstream_) downstream_->onMove(slot, mover, move, san, white_time_left, black_time_left);
}

void Runner::onGameEnd(int slot, std::size_t game_id, std::size_t round_id, const fastchess::MatchData& played,
                       bool counted) {
    // Ended by the operator: recorded with their result, whatever ended it. Not when the tournament is stopping (the
    // game is played again): the request is dropped then.
    std::optional<fastchess::MatchData> ended;
    std::optional<ResultSet> set;
    if (counted && t_task && t_started) {
        if (const auto end = state_.takeEnd(t_task->number())) {
            t_ended = true;
            ended   = endedByOperator(played, end->first);
            set     = ResultSet{end->second, "*", fastchess::MatchTermination::INTERRUPT, ""};
        }
    }
    const auto& data = ended ? *ended : played;

    if (downstream_) downstream_->onGameEnd(slot, game_id, round_id, data, counted);
    if (!counted || !t_task) return;

    if (log_every_game_ || endsBadly(data.termination)) writeLog(slot, t_task->number() + 1, data);

    try {
        const auto recorded = state_.complete(*t_task, data, set);
        t_recorded          = true;
        if (listener_ && recorded.kept) listener_->onRecorded(recorded.result, data, recorded.pair);
    } catch (const std::exception& e) {
        fail(e.what());
        return;
    }

    // The SPRT decided, or the last game was played: games still running are not needed.
    if (state_.finished()) fastchess::atomic::stop = true;
}

}  // namespace fcg::tour
