#pragma once

// Hooks for an in-process front end (fastchess-gui).
//
// The command line tool never installs an observer, so every hook below is a
// single atomic load that finds nullptr. The GUI installs one before starting a
// tournament and removes it after the tournament object has been destroyed.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <chess.hpp>

#include <core/globals/globals.hpp>
#include <core/threading/slot.hpp>
#include <matchmaking/stats.hpp>
#include <types/match_data.hpp>

namespace fastchess::observer {

struct GameStart {
    int slot             = -1;
    std::size_t game_id  = 0;
    std::size_t round_id = 0;

    std::string white;
    std::string black;

    // Position before the book moves, and the book moves in UCI notation.
    std::string start_fen;
    std::vector<std::string> book_moves;

    // Initial clocks in milliseconds, 0 when the side plays without a clock.
    int64_t white_time = 0;
    int64_t black_time = 0;
};

class GameObserver {
   public:
    virtual ~GameObserver() = default;

    // Called on the worker thread right before the engines are prepared.
    virtual void onGameStart(const GameStart&) {}

    // The side to move has been sent "go". time_left_ms is the clock that was sent (0 without a clock).
    virtual void onThinking(int /*slot*/, chess::Color /*stm*/, int64_t /*time_left_ms*/) {}

    // Every stdout line of the engine playing `side` in `slot`, as soon as it has been read.
    virtual void onEngineLine(int /*slot*/, chess::Color /*side*/, std::string_view /*line*/) {}

    // A legal move has been played. san is computed on the position before the move.
    virtual void onMove(int /*slot*/, chess::Color /*mover*/, const MoveData& /*move*/, const std::string& /*san*/,
                        int64_t /*white_time_left*/, int64_t /*black_time_left*/) {}

    // The game is over. counted is false when the game was interrupted and will not be scored.
    virtual void onGameEnd(int /*slot*/, std::size_t /*game_id*/, std::size_t /*round_id*/, const MatchData& /*data*/,
                           bool /*counted*/) {}

    // Head to head statistics after a scored game, from the point of view of `first`.
    virtual void onResult(const Stats& /*h2h*/, const std::string& /*first*/, const std::string& /*second*/) {}
};

inline std::atomic<GameObserver*> g_observer{nullptr};

[[nodiscard]] inline GameObserver* get() noexcept { return g_observer.load(std::memory_order_acquire); }

inline void set(GameObserver* observer) noexcept { g_observer.store(observer, std::memory_order_release); }

// Index of the tournament worker thread running on this thread, -1 elsewhere.
[[nodiscard]] inline int slot() noexcept { return util::t_worker_index; }

// While paused, no new game pair starts; games already running are not affected, and the second
// game of a pair whose first game has started is still let through, so that pentanomial statistics
// (which only count complete pairs) never lose a game to a pause.
class PauseGate {
   public:
    static PauseGate& instance() {
        static PauseGate gate;
        return gate;
    }

    void setPaused(bool paused) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            paused_ = paused;
        }
        cv_.notify_all();
    }

    [[nodiscard]] bool paused() const noexcept { return paused_.load(); }

    // Forgets the pairs of a previous tournament.
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        paused_ = false;
        open_pairs_.clear();
        cv_.notify_all();
    }

    // Wake up waiters, for instance after atomic::stop has been set.
    void wakeAll() { cv_.notify_all(); }

    // Keeps a task counted as running from the moment it passes the gate until it is destroyed.
    class Ticket {
       public:
        explicit Ticket(PauseGate& gate) : gate_(gate) {}
        ~Ticket() { gate_.running_--; }

        Ticket(const Ticket&)            = delete;
        Ticket& operator=(const Ticket&) = delete;

       private:
        PauseGate& gate_;
    };

    // Blocks while paused (unless another game of this pair already started), then counts the caller
    // as running. Returns early when the tournament is being stopped.
    [[nodiscard]] Ticket enter(std::uint64_t pairing_id, std::size_t games_per_pair) {
        std::unique_lock<std::mutex> lock(mutex_);

        auto it = open_pairs_.find(pairing_id);
        while (it == open_pairs_.end() && paused_ && !atomic::stop.load()) {
            cv_.wait_for(lock, std::chrono::milliseconds(100));
            it = open_pairs_.find(pairing_id);
        }

        if (games_per_pair > 1) {
            if (it == open_pairs_.end()) {
                open_pairs_.emplace(pairing_id, 1);
            } else if (++it->second >= games_per_pair) {
                open_pairs_.erase(it);
            }
        }

        running_++;
        return Ticket(*this);
    }

    // Number of tasks that went through the gate and have not finished yet.
    [[nodiscard]] int running() const noexcept { return running_.load(); }

    // Pairs whose first game started but not all of the others yet.
    [[nodiscard]] std::size_t openPairs() {
        std::lock_guard<std::mutex> lock(mutex_);
        return open_pairs_.size();
    }

   private:
    PauseGate() = default;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic_bool paused_{false};
    std::atomic_int running_{0};

    // Pairs with at least one game started, and how many of their games did.
    std::unordered_map<std::uint64_t, std::size_t> open_pairs_;
};

}  // namespace fastchess::observer
