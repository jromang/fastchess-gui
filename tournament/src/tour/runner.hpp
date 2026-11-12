#pragma once

// Plays the games of a TournamentState with fastchess.
//
// Runner derives from fastchess' BaseTournament like RoundRobin does, and reuses its thread pool, engine cache,
// CPU affinity and playGame() (time control, adjudication, crash recovery). It asks the TournamentState which
// game to play next instead of a fastchess Scheduler.
//
// While it runs, the runner is also fastchess' game observer: it records every scored game in the state, keeps
// the engine log of the games that end badly (see engine_log.hpp), then forwards each callback to `downstream`
// (the GUI), which may be null.
//
// The caller sets fastchess' global configuration first (config::TournamentConfig with an empty opening file,
// pgn file and config name; config::EngineConfigs with the participants), as for any fastchess tournament.

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <core/observer.hpp>
#include <matchmaking/tournament/base/tournament.hpp>

#include "engine_log.hpp"
#include "state.hpp"

namespace fcg::tour {

// Told about every game the runner records, on the tournament thread that played it.
class RecordListener {
   public:
    virtual ~RecordListener() = default;

    // `pair` is set when the game completed its encounter: the statistics of the two participants from the
    // point of view of the lower id, after this encounter.
    virtual void onRecorded(const GameResult& result, const fastchess::MatchData& data,
                            const std::optional<fastchess::Stats>& pair) = 0;
};

class Runner final : public fastchess::BaseTournament, public fastchess::observer::GameObserver {
   public:
    Runner(TournamentState& state, fastchess::observer::GameObserver* downstream, RecordListener* listener);
    ~Runner() override;

    // Plays until the tournament is finished or fastchess::atomic::stop is set, and returns once every game
    // thread has ended. Errors stop the tournament and are reported by error().
    void start() override;

    [[nodiscard]] std::string error() const;

   protected:
    void startNext() override;
    // While an engine fails to start: the game is still to be played, and the tournament is not pausing (a pause,
    // or a stop once the running games are over). It is otherwise given up, to be played again later if needed.
    [[nodiscard]] bool gameWanted(std::size_t game_id) const override;

   private:
    void play(const GameTask& task);
    void fail(const std::string& message);
    void closeSettledPairs();  // forgets the pause gate's pairs with no game left to start
    EngineLog& log(int slot);
    void writeLog(int slot, uint64_t game_number, const fastchess::MatchData& data);

    // fastchess::observer::GameObserver
    void onGameStart(const fastchess::observer::GameStart& info) override;
    void onEngineProcesses(int slot, int64_t white_pid, int64_t black_pid) override;
    void onThinking(int slot, chess::Color stm, int64_t time_left_ms) override;
    void onEngineLine(int slot, chess::Color side, std::string_view line) override;
    void onEngineInput(int slot, chess::Color side, std::string_view line) override;
    void onMove(int slot, chess::Color mover, const fastchess::MoveData& move, const std::string& san,
                int64_t white_time_left, int64_t black_time_left) override;
    void onGameEnd(int slot, std::size_t game_id, std::size_t round_id, const fastchess::MatchData& data,
                   bool counted) override;

    TournamentState& state_;
    fastchess::observer::GameObserver* downstream_;
    RecordListener* listener_;
    fastchess::observer::GameObserver* previous_ = nullptr;

    std::mutex mutex_;
    int in_flight_ = 0;  // games handed to the pool and not finished

    mutable std::mutex error_mutex_;
    std::string error_;

    const bool log_every_game_;
    std::mutex logs_mutex_;
    std::map<int, std::unique_ptr<EngineLog>> logs_;  // by worker slot
};

}  // namespace fcg::tour
