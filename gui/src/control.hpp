#pragma once

// The control channel between the window and a tournament process (fastchess-gui --run <folder> --control ...): JSON
// lines, one message per line, both ways.
//
// Each side first says hello: {"hello": {"protocol": 1, "version": "0.5.9", ...}}. A side that does not speak the
// protocol of the other closes the channel. Then:
//
//   - the tournament sends events, {"seq": N, "e": "<kind>", ...}, N counting from 1 in each process: its state now
//     and then (Status), each journal line as it is written ("journal", with where it starts and ends in the file, so
//     that the window's read-only copy follows it exactly, TournamentState::follow()), the games in progress (a game
//     starts, an engine thinks, its search, a move, the end of a game), each game recorded with its engines' search
//     totals, its log lines, and its end;
//   - the window sends commands, {"c": "<kind>", "id": N, ...}: pause, resume, stop (now or when idle), an edit, undo,
//     end a game, try an engine again, its allowance (budget.hpp). A command that can fail is answered by a "reply"
//     event with its id and the error, empty when it worked.
//
// A window that connects again (the tournament kept running without it) opens its copy of the tournament afresh and
// says in its hello where its journal ends: the tournament sends the journal lines from there, then the games in
// progress as they stand ("slot" events), then goes on.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include <matchmaking/stats.hpp>
#include <tour/budget.hpp>
#include <tour/edits.hpp>
#include <tour/start_failures.hpp>
#include <tour/state.hpp>

#include "live.hpp"

namespace fcg::control {

inline constexpr int PROTOCOL = 1;

struct Hello {
    int protocol = PROTOCOL;
    std::string version;  // the program's
    // The tournament's: its folder and process.
    std::string dir;
    int64_t pid = 0;
    // How long it has played in this process, in milliseconds: a window that connects later counts its time from
    // there rather than from its own start. As a duration, not a date, so that another machine's clock does not matter.
    int64_t playing_ms = 0;
    // The window's: where the journal its copy of the tournament holds ends (TournamentState::journalSize()); the
    // tournament sends the lines after it.
    uint64_t journal = 0;
};
[[nodiscard]] nlohmann::json helloJson(const Hello& hello);
// nullopt when the line is no hello.
[[nodiscard]] std::optional<Hello> parseHello(const nlohmann::json& line);

// What the tournament process tells of itself, in its "state" events.
struct Status {
    enum class Phase { Starting, Running, Finished };
    Phase phase       = Phase::Starting;
    bool paused       = false;
    bool stop_idle    = false;      // stopping once the running games are over
    bool stopping     = false;      // stopping now
    int running_games = 0;          // past the pause gate, not finished
    int open_pairs    = 0;          // encounters with one game played and the other not started
    std::vector<uint64_t> running;  // the numbers of the games being played
    bool can_undo   = false;
    bool undo_lasts = false;
    std::string pgn_out_error;
    // The engines that cannot start, and whether the tournament stopped for them.
    std::vector<tour::StartFailures::Engine> start_failures;
    bool stalled = false;
    // The machine: what the games in flight take, what the tournament may use, what holds its next game back.
    tour::GameCost in_flight;
    tour::Allowance allowance;
    tour::Wait waiting = tour::Wait::None;
    // The machines its engines run on, as its engine hub sees them: connected, playing, or why not.
    struct Machine {
        std::string id;
        std::string name;
        int connections = 0;
        int engines     = 0;
        std::string error;
        int64_t since = 0;  // unix time of the error, seconds; 0 when none
        bool operator==(const Machine&) const = default;
    };
    std::vector<Machine> machines;

    bool operator==(const Status&) const;
};
[[nodiscard]] nlohmann::json statusJson(const Status& status);
[[nodiscard]] Status statusFromJson(const nlohmann::json& j);

// How a tournament process ended, in its "end" event.
struct End {
    std::string error;             // the runner's or the start's; empty when none
    bool abnormal        = false;  // fastchess stopped it (an engine crashed without "restart")
    bool stopped_by_user = false;
    bool finished        = false;  // every game played, or the SPRT decided
    bool stalled         = false;  // no game left without the engines that cannot start
    // The engine fastchess said could not start, and why, when that stopped the tournament.
    std::string failed_engine;
    std::string failed_reason;
    int exit_code = 0;
};
[[nodiscard]] nlohmann::json endJson(const End& end);
[[nodiscard]] End endFromJson(const nlohmann::json& j);

// The parts of the games in progress, as they travel.
[[nodiscard]] nlohmann::json liveMoveJson(const LiveMove& move, bool frc = false);
[[nodiscard]] LiveMove liveMoveFromJson(const nlohmann::json& j);
// `frc`: the game is Chess960, whose castling moves read otherwise.
[[nodiscard]] nlohmann::json slotJson(const SlotState& slot, bool frc);
[[nodiscard]] SlotState slotFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json moveDataJson(const fastchess::MoveData& move);
[[nodiscard]] fastchess::MoveData moveDataFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json gameStartJson(const fastchess::observer::GameStart& start);
[[nodiscard]] fastchess::observer::GameStart gameStartFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json totalsJson(const EngineTotals& totals);
[[nodiscard]] EngineTotals totalsFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json statsJson(const fastchess::Stats& stats);
[[nodiscard]] fastchess::Stats statsFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json allowanceJson(const tour::Allowance& allowance);
[[nodiscard]] tour::Allowance allowanceFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json costJson(const tour::GameCost& cost);
[[nodiscard]] tour::GameCost costFromJson(const nlohmann::json& j);
// An engine that cannot start; its next try in seconds from now, the clocks of two processes not being the same.
[[nodiscard]] nlohmann::json startFailureJson(const tour::StartFailures::Engine& engine);
[[nodiscard]] tour::StartFailures::Engine startFailureFromJson(const nlohmann::json& j);

[[nodiscard]] const char* waitName(tour::Wait wait);
[[nodiscard]] tour::Wait waitFromName(std::string_view name);

// Commands, as the window writes them.
[[nodiscard]] nlohmann::json command(std::string_view kind, uint64_t id,
                                     nlohmann::json fields = nlohmann::json::object());

// Splits what a pipe or a socket delivers into lines.
class LineSplitter {
   public:
    // Adds `data`; complete lines are appended to `lines` (without their end). A line longer than `max` is dropped.
    void feed(std::string_view data, std::vector<std::string>& lines, std::size_t max = 64u << 20);

   private:
    std::string partial_;
    bool skipping_ = false;  // inside a line too long
};

}  // namespace fcg::control
