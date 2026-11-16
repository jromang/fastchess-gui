#pragma once

// A game, or a pair of games with the colours reversed, between two engines of the library from a position of the
// Board page, with a clock for each; or between the user and an engine, the user's moves made on the board. Played by
// the GUI's own engine client on a thread of its own, outside fastchess: it runs while a tournament plays without
// touching its configuration, and its threads count in the core budget. Each game is appended to a PGN file as it ends.

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <json.hpp>

#include "engine_client.hpp"
#include "model.hpp"
#include "pgn_store.hpp"

namespace fcg {

// A clock: a base time and an increment per move.
struct GameClock {
    double base_s = 60.0;
    double inc_s  = 0.6;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GameClock, base_s, inc_s)

// "1+0.6", "40+0" (minutes and seconds, as the testers write it), "0:30+0.2" below a minute.
[[nodiscard]] std::string clockText(const GameClock& clock);

struct BoardMatchSetup {
    std::string first, second;  // library engines: the first has White in the first game
    GameClock first_clock, second_clock;
    bool pair = false;  // a second game, the colours reversed
    std::string fen;    // the start position
    bool chess960 = false;
    // The user plays one side: 0 the first (White in the first game), 1 the second; -1 two engines. Its entry is only
    // the name its games give it.
    int human = -1;
};

// The user's name in the games they play: their login name, or "Human".
[[nodiscard]] std::string humanName();

// How long a move may take beyond what the clock had before it counts as a loss on time: the time the move takes to
// come through the pipe and the GUI.
inline constexpr std::chrono::milliseconds CLOCK_MARGIN{100};

// The user's side of a game, between the window and the game's thread: whether their move is awaited, and the move
// given.
struct BoardSeat {
    std::mutex mutex;
    std::condition_variable cv;
    bool turn = false;
    std::optional<std::string> move;
};

class BoardMatch {
   public:
    struct State {
        uint64_t version = 0;
        int game         = 0;  // the one being played (or the last), from 0
        int games        = 1;
        ReplayGame current;                 // its moves so far, each with the engine's score, depth and time
        std::array<int64_t, 2> clock_ms{};  // White's and Black's, at the last move
        std::optional<chess::Color> thinking;
        // The side the user plays in the game being played. While it plays, the engine's scores and lines are left
        // out of `current` and `info`, which would tell its plan; the finished game has them.
        std::optional<chess::Color> you;
        std::chrono::steady_clock::time_point think_start{};
        std::string info;  // the last info line of the side thinking
        std::vector<ReplayGame> finished;
        bool running = false;
        std::string error;  // why it could not start or go on
        std::filesystem::path pgn;
    };

    BoardMatch() = default;
    ~BoardMatch() { stop(); }
    BoardMatch(const BoardMatch&)            = delete;
    BoardMatch& operator=(const BoardMatch&) = delete;

    // Starts the games on a thread of its own, appended to `pgn` as each ends. False, with the reason, when an
    // engine has no program.
    bool start(const EngineEntry& first, const EngineEntry& second, const BoardMatchSetup& setup,
               const std::filesystem::path& pgn, std::string& error);
    // Ends the games: the one being played ends without a result ("*"), its engines are told to quit.
    void stop();
    // The user's move (UCI) in the game they play, taken when it is their turn; ignored otherwise. The window gives
    // only legal moves.
    void play(const std::string& uci);
    // The user resigns the game being played, whoever is to move; the second game of a pair follows.
    void resign();
    // Once the games are over: their thread joined, their threads no longer counted. From the thread that started
    // them, now and then.
    void reap();

    // The state, copied when it changed since `version`; false when it did not.
    bool state(State& out, uint64_t& version) const;
    [[nodiscard]] bool running() const;
    // The threads its engines use, counted in the core budget while it plays: the larger of the two (they take
    // turns).
    [[nodiscard]] int threads() const noexcept { return claim_.threads(); }

   private:
    void run(EngineEntry first, EngineEntry second, BoardMatchSetup setup, std::filesystem::path pgn);
    template <typename F>
    void update(F f);

    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> resign_{false};
    BoardSeat seat_;
    mutable std::mutex mutex_;
    State state_;
    CoreClaim claim_;
};

}  // namespace fcg
