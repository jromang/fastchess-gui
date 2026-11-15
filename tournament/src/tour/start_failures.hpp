#pragma once

// The participants whose engine cannot start, while a tournament runs.
//
// A night left alone must not stop on one engine: an engine that fails to start is tried again a few times, a pause
// apart, then marked failed. Its games are set aside and the others go on; it is tried again now and then, or at once
// when asked, and its games come back once it starts. The runner (runner.hpp) reports the tries and the starts here,
// TournamentState::claimNext() leaves the games set aside out, and the window shows what is going on.
//
// Kept in memory only, nothing is journaled: a resume tries every engine anew.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fcg::tour {

// What fastchess' log says when an engine of a game fails to start: `Warning; <engine> engine startup failure:
// "<error>", trying again in 10 s`, or `Fatal; ...` for the last try. nullopt for any other line.
struct StartFailureLine {
    std::string engine;
    std::string error;
};
[[nodiscard]] std::optional<StartFailureLine> parseStartFailure(std::string_view line);

class StartFailures {
   public:
    using Clock = std::chrono::steady_clock;

    // The tries before an engine is marked failed (the first, then fastchess' retries) and the pause between them, as
    // fastchess has them; then how often a failed engine is tried again.
    static constexpr int TRIES = 4;
    static constexpr std::chrono::seconds RETRY_PAUSE{10};
    static constexpr std::chrono::minutes RETRY_EVERY{5};

    struct Engine {
        int participant = -1;
        std::string name;
        std::string error;  // why its last try failed
        int tries = 0;      // failed tries since it last started
        // Its tries are used up: its games are set aside until it starts, tried again every RETRY_EVERY.
        bool failed          = false;
        bool trying          = false;                 // a game of it was claimed, to try it
        uint64_t trying_game = 0;                     // that game (GameTask::number())
        Clock::time_point next_try;                   // its games are set aside until then
        std::chrono::system_clock::time_point since;  // its first failed try
        std::filesystem::path log;  // the engine log of its last failed try, empty if none was written
        bool last_chance = false;   // tried once more because nothing else was left to play (lastChance())
    };

    // What a failed try changed.
    enum class Outcome {
        Same,          // a game claimed before its previous failure: the same try, not counted
        Retry,         // tried again in RETRY_PAUSE
        Failed,        // its tries are used up: its games are set aside
        StillFailing,  // a try of a failed engine
    };

    // A try of `participant`'s engine (`name`) failed with `error`, in game `game`; `log` is the engine log written for
    // it. The first failure of an engine, or the failure of the game claimed to try it, is a try.
    Outcome failed(int participant, uint64_t game, const std::string& name, const std::string& error,
                   Clock::time_point now, const std::filesystem::path& log = {});

    // The engine of `participant` started: its games come back. True when it was failing.
    bool started(int participant);
    // `participant`'s engine (`name`) is set aside now as if its tries were used up, with `error`: its machine keeps
    // breaking off. Tried again every RETRY_EVERY, or at once when asked, as a failed engine is.
    void setAside(int participant, const std::string& name, const std::string& error, Clock::time_point now);

    // Game `game` of `participant` was claimed: when its engine waits for a try, this is it.
    void claimed(int participant, uint64_t game);

    // Game `game` ended without telling how the start of its engines went (given up, interrupted): the engines it was
    // to try wait for another game.
    void released(uint64_t game);

    // The participants whose games are set aside now: waiting for their next try, or being tried.
    [[nodiscard]] std::set<int> setAside(Clock::time_point now) const;

    // The next try of a participant's engine now, as soon as a game of it can be claimed. False when it is not
    // failing, or a try is under way.
    bool tryNow(int participant, Clock::time_point now);

    // A pause holds the tries: the time it lasted does not count.
    void postpone(Clock::duration paused);

    // Nothing else is left to play: each failed engine is tried once more now, once. True when one is.
    bool lastChance(Clock::time_point now);

    // The participant plays no more (withdrawn, replaced).
    void forget(int participant);

    // By participant id.
    [[nodiscard]] std::vector<Engine> engines() const;
    [[nodiscard]] std::optional<Engine> engine(int participant) const;
    [[nodiscard]] bool empty() const;
    // An engine is in its first tries, or being tried: whether it starts is not known yet.
    [[nodiscard]] bool deciding() const;

    // The tournament stopped since no game could be played without the failed engines.
    void setStalled(bool stalled);
    [[nodiscard]] bool stalled() const noexcept { return stalled_.load(); }

    // Changes at every change.
    [[nodiscard]] uint64_t version() const noexcept { return version_.load(); }

    // The engines as another program has them (the tournament process tells the window): a read-only copy's.
    void assign(std::vector<Engine> engines, bool stalled);

    // Other waits than RETRY_PAUSE and RETRY_EVERY, for the tests.
    void setWaits(Clock::duration retry_pause, Clock::duration retry_every);

   private:
    mutable std::mutex mutex_;
    std::map<int, Engine> engines_;
    Clock::duration retry_pause_ = RETRY_PAUSE;
    Clock::duration retry_every_ = RETRY_EVERY;
    std::atomic<uint64_t> version_{0};
    std::atomic_bool stalled_{false};
};

}  // namespace fcg::tour
