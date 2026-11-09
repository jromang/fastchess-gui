#include "runner.hpp"

#include <chrono>
#include <thread>

#include <core/globals/globals.hpp>

namespace fcg::tour {

namespace {

// The game this thread is playing, and whether it was recorded. playGame() tells the observer about the end of
// the game on the thread that played it, right before it would score it.
thread_local const GameTask* t_task = nullptr;
thread_local bool t_recorded        = false;

}  // namespace

Runner::Runner(TournamentState& state, fastchess::observer::GameObserver* downstream, RecordListener* listener)
    : BaseTournament(fastchess::stats_map{}), state_(state), downstream_(downstream), listener_(listener) {}

Runner::~Runner() {
    // Workers may still be inside a callback of ours: join them before our members go away.
    pool_.kill();
    if (fastchess::observer::get() == this) fastchess::observer::set(previous_);
}

void Runner::start() {
    previous_ = fastchess::observer::get();
    fastchess::observer::set(this);

    startNext();

    using namespace std::chrono_literals;
    while (!fastchess::atomic::stop) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (in_flight_ == 0 && !state_.hasWork()) break;
        }
        std::this_thread::sleep_for(20ms);
    }

    // Interrupted games end quickly once stop is set; nothing is called back after this.
    pool_.kill();
    fastchess::observer::set(previous_);
}

void Runner::startNext() {
    std::lock_guard<std::mutex> lock(mutex_);

    while (!fastchess::atomic::stop && in_flight_ < static_cast<int>(pool_.getNumThreads())) {
        std::optional<GameTask> task;
        try {
            task = state_.claimNext();
        } catch (const std::exception& e) {
            fail(e.what());
            return;
        }
        if (!task) return;

        ++in_flight_;
        pool_.enqueue([this, task = std::move(*task)]() { play(task); });
    }
}

void Runner::play(const GameTask& task) {
    t_task     = &task;
    t_recorded = false;

    try {
        // A paused tournament lets running games (and the other game of their pair) finish but starts no new pair.
        const auto ticket = fastchess::observer::PauseGate::instance().enter(task.encounter.index, 2);

        const fastchess::GamePair<fastchess::EngineConfiguration, fastchess::EngineConfiguration> configs{task.white,
                                                                                                          task.black};
        const auto start  = []() {};
        const auto finish = [](const fastchess::Stats&, const std::string&, const fastchess::engines&) {};

        playGame(configs, start, finish, task.opening, task.encounter.index, task.number() + 1);
    } catch (const std::exception& e) {
        fail(e.what());
    }

    if (!t_recorded) state_.release(task);
    t_task = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        --in_flight_;
    }
    startNext();
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

void Runner::onGameStart(const fastchess::observer::GameStart& info) {
    if (downstream_) downstream_->onGameStart(info);
}

void Runner::onThinking(int slot, chess::Color stm, int64_t time_left_ms) {
    if (downstream_) downstream_->onThinking(slot, stm, time_left_ms);
}

void Runner::onEngineLine(int slot, chess::Color side, std::string_view line) {
    if (downstream_) downstream_->onEngineLine(slot, side, line);
}

void Runner::onMove(int slot, chess::Color mover, const fastchess::MoveData& move, const std::string& san,
                    int64_t white_time_left, int64_t black_time_left) {
    if (downstream_) downstream_->onMove(slot, mover, move, san, white_time_left, black_time_left);
}

void Runner::onGameEnd(int slot, std::size_t game_id, std::size_t round_id, const fastchess::MatchData& data,
                       bool counted) {
    if (downstream_) downstream_->onGameEnd(slot, game_id, round_id, data, counted);
    if (!counted || !t_task) return;

    try {
        const auto recorded = state_.complete(*t_task, data);
        t_recorded          = true;
        if (listener_) listener_->onRecorded(recorded.result, data, recorded.pair);
    } catch (const std::exception& e) {
        fail(e.what());
        return;
    }

    // The SPRT decided, or the last game was played: games still running are not needed.
    if (state_.finished()) fastchess::atomic::stop = true;
}

}  // namespace fcg::tour
