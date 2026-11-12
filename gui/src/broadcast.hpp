#pragma once

// Broadcasting a tournament: the app builds a snapshot of it (games in progress, standings) a few times per second
// and hands it to the outputs that are switched on: files (live PGN, standings, JSON), a TLCS server (TLCV,
// node-tlcv, Livius) and a web page.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <tour/standings.hpp>

#include "net.hpp"

namespace fcg {

// One game in progress, as broadcast.
struct LiveGame {
    int slot        = 0;
    uint64_t number = 0;  // game number in the tournament
    std::string white;
    std::string black;
    int white_elo = 0;  // reference ratings, 0 = none
    int black_elo = 0;
    std::string start_fen;
    std::vector<std::string> san;  // moves from start_fen
    std::vector<std::string> uci;
    std::string fen;  // current position
    bool white_to_move = true;
    bool clocks        = false;
    int64_t white_ms   = 0;  // clock of each side (the side to move's is running)
    int64_t black_ms   = 0;
    bool active        = false;  // false: the game just ended
    std::string result;          // when it ended

    struct Search {
        int depth       = 0;
        int score_cp    = 0;  // the searching side's point of view; mate in N moves as +-(100000 - N)
        uint64_t nodes  = 0;
        int64_t time_ms = 0;
        std::vector<std::string> pv;  // UCI
    };
    Search search[2];  // [0] White's last or current search, [1] Black's
};

struct BroadcastSnapshot {
    std::string name;
    std::string format;  // "match", "round_robin", "gauntlet"
    uint64_t played = 0;
    uint64_t total  = 0;  // 0 = unbounded
    bool finished   = false;
    std::vector<LiveGame> games;
    std::vector<LiveGame> ended;  // the last games ended in each slot, so their end is not missed (TLCS)
    tour::Table table;
    std::vector<std::string> recent;  // latest results, newest first: "White - Black 1-0"
    struct Played {
        uint64_t number = 0;  // 1-based
        std::string white, black, result;
    };
    std::vector<Played> played_games;  // the last 300 games played, newest first (TLCS game list)
};

// The snapshot as JSON, for scripts and for the web page.
[[nodiscard]] std::string snapshotJson(const BroadcastSnapshot& s);

// The games in progress as one PGN file, results "*".
[[nodiscard]] std::string livePgn(const BroadcastSnapshot& s);

// The crosstable in the text layout TLCS sends (and node-tlcv parses): a RANK/ENGINE/GAMES/POINTS header, one row
// per engine with a cell per opponent (1 = 0 per game), and "total games = N".
[[nodiscard]] std::vector<std::string> tlcsCrosstable(const BroadcastSnapshot& s, uint64_t total_games);

// Writes live.pgn, standings.html and tournament.json in `dir`, each through a temporary file so that a program
// uploading them never reads half a file. Returns an error message, empty on success.
std::string writeBroadcastFiles(const BroadcastSnapshot& s, const std::filesystem::path& dir);

// A TLCS compatible server: TLCV clients, node-tlcv (ccrl.live) and Livius log on over UDP and follow one game:
// players, moves, clocks, the engines' searches, the result, and the crosstable, sent again after every game.
class TlcsServer {
   public:
    TlcsServer();
    ~TlcsServer();

    // Returns an error message, empty on success. With `lanes`, every concurrent game has a port of its own: the game
    // of slot k on port + k, each port opened when its slot first has a game.
    std::string start(uint16_t port, bool lanes = false);
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_; }

    // The game to follow is the one of `featured` (a slot), or the first game in progress; with lanes, each port's
    // own slot.
    void publish(const BroadcastSnapshot& snapshot, int featured);

    // Viewers on every port.
    [[nodiscard]] int clients() const;
    // The ports open, the first one first; why the next one could not be opened (lanes), empty if it could.
    [[nodiscard]] std::vector<uint16_t> ports() const;
    [[nodiscard]] std::string laneError() const;

   private:
    struct Impl;  // one port
    std::string open(uint16_t port, int lane);

    mutable std::mutex mutex_;  // what follows
    std::vector<std::unique_ptr<Impl>> impls_;
    uint16_t port_ = 0;
    bool lanes_    = false;
    std::string lane_error_;
    std::atomic_bool running_{false};
};

// A small web server: a page showing every game in progress, the standings and the crosstable, refreshed every
// second from /state.json.
class WebServer {
   public:
    ~WebServer();

    std::string start(uint16_t port);
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_; }

    void publish(const BroadcastSnapshot& snapshot);

    [[nodiscard]] int viewers() const noexcept { return viewers_; }

   private:
    void serve();

    net::TcpListener listener_;
    std::thread thread_;
    std::atomic_bool running_{false};
    std::atomic_int viewers_{0};
    std::mutex mutex_;
    std::string state_json_ = "{}";
    std::vector<std::chrono::steady_clock::time_point> recent_;  // state requests, to count viewers
};

// The viewer page served at "/".
[[nodiscard]] const char* webPage();

}  // namespace fcg
