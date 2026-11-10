#pragma once

// A tournament on a lichess broadcast (https://lichess.org/api#tag/Broadcasts): the games in progress pushed every
// few seconds, and each finished game once, with its evaluations and clocks. lichess shows them with its board,
// eval bar and chat, to anyone, for free.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <json.hpp>

#include "broadcast.hpp"
#include "https.hpp"

namespace fcg {

// Where a tournament goes on lichess: lichess.json in its folder (the token is not there).
struct LichessTarget {
    std::string tour_id;                        // the broadcast tournament; empty when only a round was given
    std::string round_id;                       // the round games go to now
    std::string url;                            // its page
    int round_index = 1;                        // the rounds made here: "Part 1", "Part 2"...
    std::map<std::string, std::string> rounds;  // game key -> the round it was pushed to
    std::set<std::string> finished;             // game keys pushed with their result
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LichessTarget, tour_id, round_id, url, round_index, rounds, finished)

// A lichess round holds this many games; the next ones go to a new round of the same broadcast.
constexpr std::size_t LICHESS_ROUND_GAMES = 64;

// A game as lichess gets it. The tags stay the same while it is played and once it is finished, so that lichess
// updates the same board: the encounter as Round, the engines, no date.
struct LichessGame {
    std::string key;  // "round|white|black"
    std::string pgn;
    bool finished = false;
};
[[nodiscard]] LichessGame lichessLiveGame(const LiveGame& game, const std::string& event, int white_elo, int black_elo,
                                          bool frc);
// A finished game, from its PGN (the ChessBase style one: evaluations and clocks lichess reads), with the same tags.
[[nodiscard]] LichessGame lichessFinishedGame(std::string_view pgn, const std::string& event, uint32_t round,
                                              const std::string& white, const std::string& black, int white_elo,
                                              int black_elo, const std::string& result, bool frc);

// The round id of a lichess round URL (https://lichess.org/broadcast/<tour>/<round>/<id>) or the id itself.
[[nodiscard]] std::optional<std::string> lichessRoundId(std::string_view text);

class LichessRelay {
   public:
    struct Settings {
        std::string token;
        std::string round;    // a round's URL or id; empty: a new broadcast named after the tournament
        bool listed = false;  // listed among lichess' community broadcasts, otherwise only for those with the link
        std::string name;     // the tournament's
        std::filesystem::path dir;      // the tournament's folder, for lichess.json
        std::filesystem::path tmp_dir;  // for curl's files
        std::string base = "https://lichess.org";
    };
    struct Status {
        bool running = false;
        std::string url;
        std::string error;
        std::string last_push;  // "20:15:03"
        std::size_t games = 0;  // pushed so far
    };

    LichessRelay() = default;
    ~LichessRelay();
    LichessRelay(const LichessRelay&)            = delete;
    LichessRelay& operator=(const LichessRelay&) = delete;

    void start(Settings settings);
    // Pushes what was offered last, then stops.
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] Status status() const;
    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }

    // The games to push: those in progress and those finished since the last offer.
    void offer(std::vector<LichessGame> live, std::vector<LichessGame> finished);

   private:
    void loop();
    bool prepare();  // the broadcast and its first round, made or read; false when it cannot be
    bool newRound();
    void push();
    HttpResult post(const std::string& path, const std::string& type, const std::string& body);
    void save();

    Settings settings_;
    LichessTarget target_;
    std::thread thread_;
    std::atomic<bool> running_{false}, stopping_{false};
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<LichessGame> live_;
    std::map<std::string, LichessGame> pending_;  // finished games not pushed yet
    Status status_;
    std::chrono::steady_clock::time_point not_before_{};  // after a "too many requests"
};

}  // namespace fcg
