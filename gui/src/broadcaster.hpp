#pragma once

// Broadcasting a running tournament, for the window and for headless mode alike: snapshots of it, a few times per
// second, handed to the outputs that are switched on (live files, the TLCS server, the web page, lichess).

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <tour/standings.hpp>
#include <tour/state.hpp>

#include "broadcast.hpp"
#include "lichess.hpp"
#include "live.hpp"
#include "model.hpp"
#include "ssh_tunnel.hpp"

namespace fcg {

struct BroadcastSettings {
    bool files = false;
    std::filesystem::path dir;  // empty = "live" in the tournament folder
    bool tlcs       = false;
    int tlcs_port   = 16001;
    bool tlcs_lanes = false;  // a port per concurrent game, from tlcs_port on
    bool web        = false;
    int web_port    = 8080;

    bool lichess = false;
    std::string lichess_token;
    std::string lichess_round;  // a round's URL or id; empty: a new broadcast
    bool lichess_listed = false;

    // The web page on the internet through an SSH tunnel, while it is served; to the web server's port.
    bool tunnel = false;
    TunnelSettings tunnel_to;
};

// The tunnel `setup` asks for: localhost.run, or another SSH server whose host key is trusted on first use and kept
// in `known_hosts`.
[[nodiscard]] TunnelSettings tunnelSettings(const MatchSetup& setup, const std::filesystem::path& known_hosts);

// A game of the schedule to be played, as broadcast.
struct BroadcastNextGame {
    tour::Encounter encounter;
    int game = 0;  // 0: `first` has White
    std::optional<fastchess::book::Opening> opening;
};

// The games of `rows` (the schedule) to be played next, in the order they are started: the second game of each
// encounter started, then the encounters not started yet, both games each. At most `count`, as (row, game).
[[nodiscard]] std::vector<std::pair<std::size_t, int>> nextGames(const std::vector<tour::ScheduleRow>& rows,
                                                                 std::size_t count);

// What a snapshot is made from: copies of the tournament's settings and results and of the games in progress,
// taken quickly on the thread that runs the tournament's window, so that the rest can be done on another one.
struct BroadcastInput {
    const void* tournament = nullptr;  // which tournament, only compared, never read through
    std::filesystem::path dir;         // its folder
    tour::TournamentSpec spec;
    std::vector<tour::GameResult> results;
    std::map<int, int> byes;
    uint64_t total = 0;
    bool finished  = false;
    std::optional<tour::Anchor> anchor;
    std::vector<SlotState> slots;               // by slot
    std::vector<std::vector<SlotState>> ended;  // by slot
    std::vector<std::pair<std::string, EngineTotals>> totals;  // the observer's, by participant index
    std::vector<BroadcastNextGame> next;                       // the next few games of the schedule
    std::chrono::steady_clock::time_point taken;
};

// Safe to call while the tournament runs.
[[nodiscard]] BroadcastInput broadcastInput(const tour::TournamentState& state, const GuiObserver& observer,
                                            const std::optional<tour::Anchor>& anchor);

// The tournament as broadcast: the games in progress (from the observer), the last game ended in each slot, the
// standings and the latest results. Any thread.
[[nodiscard]] BroadcastSnapshot makeSnapshot(const BroadcastInput& input);
[[nodiscard]] inline BroadcastSnapshot makeSnapshot(const tour::TournamentState& state, const GuiObserver& observer,
                                                    const std::optional<tour::Anchor>& anchor) {
    return makeSnapshot(broadcastInput(state, observer, anchor));
}

class Broadcaster {
   public:
    Broadcaster() = default;
    ~Broadcaster();
    Broadcaster(const Broadcaster&)            = delete;
    Broadcaster& operator=(const Broadcaster&) = delete;

    // Starts and stops the servers to match `settings`. Returns the errors of servers that could not start (a port
    // in use); a server that failed is not tried again until it is switched off and on.
    std::vector<std::string> configure(const BroadcastSettings& settings);

    // Publishes the tournament: at most twice a second, the files once a second. Only the copies of
    // broadcastInput() are made here; the snapshot, the files and lichess are done on a thread of the broadcaster.
    // `featured`: the slot whose game TLCS follows, -1 to let it choose.
    void update(const tour::TournamentState& state, const GuiObserver& observer,
                const std::optional<tour::Anchor>& anchor, int featured);

    // Stops everything, once the last update is published and the last push to lichess made (headless mode).
    void stop();

    [[nodiscard]] const TlcsServer& tlcs() const noexcept { return tlcs_; }
    [[nodiscard]] const WebServer& web() const noexcept { return web_; }
    [[nodiscard]] const std::string& tlcsError() const noexcept { return tlcs_error_; }
    [[nodiscard]] const std::string& webError() const noexcept { return web_error_; }
    [[nodiscard]] const BroadcastSettings& settings() const noexcept { return settings_; }
    // Where and when the files were last written, or why they could not be.
    [[nodiscard]] std::string filesStatus() const;
    [[nodiscard]] LichessRelay::Status lichess() const;
    [[nodiscard]] TunnelStatus tunnel() const { return tunnel_.status(); }
    // Trusts the host key the tunnel's server shows now, after it changed, and connects again.
    void trustTunnelKey();

   private:
    // The thread's side.
    struct Job {
        BroadcastInput input;
        int featured = -1;
    };
    void work();
    void publish(const Job& job, const BroadcastSettings& settings);
    void updateLichess(const BroadcastInput& input, const BroadcastSnapshot& snapshot,
                       const BroadcastSettings& settings);

    BroadcastSettings settings_;
    TlcsServer tlcs_;
    WebServer web_;
    SshTunnel tunnel_;
    std::string tlcs_error_;
    std::string web_error_;
    std::chrono::steady_clock::time_point last_snapshot_{};

    std::thread thread_;
    mutable std::mutex mutex_;  // what follows, up to the thread's own
    std::condition_variable wake_;
    bool quit_ = false;
    std::optional<Job> job_;  // the latest update not published yet
    BroadcastSettings next_settings_;
    std::string files_error_, files_when_, files_dir_;  // the last writing of the files
    // Started and stopped by the thread, mutex_ held.
    LichessRelay lichess_;

    // The thread's own.
    std::chrono::steady_clock::time_point last_files_{};
    // lichess: started with the first update of a tournament, from its results at that moment on.
    BroadcastSettings lichess_settings_;  // those it was started with
    const void* lichess_for_     = nullptr;
    std::size_t lichess_results_ = 0;
    std::chrono::steady_clock::time_point last_lichess_{};
};

}  // namespace fcg
