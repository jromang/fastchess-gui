#pragma once

// Broadcasting a running tournament, for the window and for headless mode alike: snapshots of it, a few times per
// second, handed to the outputs that are switched on (live files, the TLCS server, the web page).

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <tour/standings.hpp>
#include <tour/state.hpp>

#include "broadcast.hpp"
#include "live.hpp"

namespace fcg {

struct BroadcastSettings {
    bool files = false;
    std::filesystem::path dir;  // empty = "live" in the tournament folder
    bool tlcs     = false;
    int tlcs_port = 16001;
    bool web      = false;
    int web_port  = 8080;
};

// The tournament as broadcast: the games in progress (from the observer), the last game ended in each slot, the
// standings and the latest results. Safe to call while the tournament runs.
[[nodiscard]] BroadcastSnapshot makeSnapshot(const tour::TournamentState& state, const GuiObserver& observer,
                                             const std::optional<tour::Anchor>& anchor);

class Broadcaster {
   public:
    // Starts and stops the servers to match `settings`. Returns the errors of servers that could not start (a port
    // in use); a server that failed is not tried again until it is switched off and on.
    std::vector<std::string> configure(const BroadcastSettings& settings);

    // Publishes the tournament: at most twice a second, the files once a second. `featured`: the slot whose game
    // TLCS follows, -1 to let it choose.
    void update(const tour::TournamentState& state, const GuiObserver& observer,
                const std::optional<tour::Anchor>& anchor, int featured);

    void stop();

    [[nodiscard]] const TlcsServer& tlcs() const noexcept { return tlcs_; }
    [[nodiscard]] const WebServer& web() const noexcept { return web_; }
    [[nodiscard]] const std::string& tlcsError() const noexcept { return tlcs_error_; }
    [[nodiscard]] const std::string& webError() const noexcept { return web_error_; }
    // Where and when the files were last written, or why they could not be.
    [[nodiscard]] const std::string& filesStatus() const noexcept { return files_status_; }

   private:
    BroadcastSettings settings_;
    TlcsServer tlcs_;
    WebServer web_;
    std::string tlcs_error_;
    std::string web_error_;
    std::string files_status_;
    std::chrono::steady_clock::time_point last_snapshot_{};
    std::chrono::steady_clock::time_point last_files_{};
};

}  // namespace fcg
