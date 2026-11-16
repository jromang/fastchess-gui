#pragma once

// The analysis engine of the Board page: an engine of the library searching the position shown, with several lines
// (MultiPV), over the engine client (engine_client.hpp). Its settings are the analysis's own: they never change the
// library engine nor reach a tournament.

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include <types/score.hpp>

#include "engine_client.hpp"
#include "model.hpp"

namespace fcg {

struct AnalysisSettings {
    std::string engine;  // a library engine's name
    int threads = 1;
    int hash    = 0;  // MB, 0: the engine's own (its library value, or its default)
    int multipv = 3;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AnalysisSettings, engine, threads, hash, multipv)

// One line of the analysis, as the engine's last info line for it said.
struct AnalysisLine {
    int multipv      = 1;
    int64_t depth    = 0;
    int64_t seldepth = 0;
    std::optional<fastchess::Score> score;  // the side to move's point of view
    bool lowerbound = false;
    bool upperbound = false;
    std::optional<std::array<int64_t, 3>> wdl;  // win, draw, loss in permille, the side to move's point of view
    uint64_t nodes  = 0;
    uint64_t nps    = 0;
    int64_t time_ms = 0;
    uint64_t tbhits  = 0;
    int64_t hashfull = -1;        // permille, -1 when not told
    std::vector<std::string> pv;  // UCI moves
};

// Takes an info line into `lines` (one per MultiPV rank, best first): a line with a score updates its rank, up to
// `multipv` ranks. False when the line tells nothing to keep (a currmove line, an info string, a rank beyond).
bool takeInfoLine(std::vector<AnalysisLine>& lines, const std::string& info, int multipv);

// A position to search: a start position and the moves played from it.
struct AnalysisPosition {
    std::string fen;
    std::vector<std::string> moves;  // UCI
    bool chess960 = false;

    [[nodiscard]] bool operator==(const AnalysisPosition& o) const {
        return fen == o.fen && moves == o.moves && chess960 == o.chess960;
    }
    [[nodiscard]] bool empty() const noexcept { return fen.empty(); }
};

// How long a search lasts: both 0, until it is stopped.
struct SearchLimit {
    int64_t movetime_ms = 0;
    int depth           = 0;
    [[nodiscard]] bool infinite() const noexcept { return movetime_ms <= 0 && depth <= 0; }
};

// "go infinite", "go movetime 1000", "go depth 20".
[[nodiscard]] std::string goCommand(const SearchLimit& limit);

// "position fen <fen> moves e2e4 e7e5".
[[nodiscard]] std::string positionCommand(const AnalysisPosition& position);

class Analyser {
   public:
    enum class State { Off, Starting, Ready, Searching, Stopping, Failed };

    Analyser() = default;
    ~Analyser() { stop(); }
    Analyser(const Analyser&)            = delete;
    Analyser& operator=(const Analyser&) = delete;

    // Starts the engine; once it is ready it searches the position asked for, if any. The library's option values
    // are sent, then the settings' Threads, Hash and MultiPV where the engine has them, and UCI_ShowWDL and
    // UCI_AnalyseMode on.
    void start(const EngineEntry& entry, const AnalysisSettings& settings);
    // Ends the engine.
    void stop();
    // The settings changed: Threads, Hash and MultiPV are sent between two searches (the search goes on with them).
    void update(const AnalysisSettings& settings);

    // Searches `position`, after stopping the search of another one; `new_game` sends ucinewgame first (another game:
    // the hash table is of no use).
    void analyse(const AnalysisPosition& position, SearchLimit limit = {}, bool new_game = false);
    // Stops the search; the engine stays, ready for the next one.
    void halt();

    // Reads what the engine wrote and moves on: once per frame.
    void poll();

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] bool active() const noexcept { return state_ != State::Off && state_ != State::Failed; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] const std::string& engine() const noexcept { return engine_; }
    [[nodiscard]] const std::string& idName() const noexcept { return id_name_; }
    [[nodiscard]] const AnalysisSettings& settings() const noexcept { return settings_; }
    // The position the lines are about, and whether its search ended (the engine gave its move).
    [[nodiscard]] const AnalysisPosition& position() const noexcept { return searched_; }
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] const std::string& bestmove() const noexcept { return bestmove_; }
    [[nodiscard]] const std::vector<AnalysisLine>& lines() const noexcept { return lines_; }
    // What the engine said it can do: Threads, MultiPV.
    [[nodiscard]] bool hasOption(const std::string& name) const;
    // The threads its search uses now (0 between searches), counted in the core budget.
    [[nodiscard]] int threadsInUse() const noexcept { return claim_.threads(); }
    // How long it has been searching the position.
    [[nodiscard]] double searchSeconds() const;

   private:
    void fail(std::string message);
    void handle(const std::string& line);
    void sendOptions(bool all);
    void beginSearch();
    [[nodiscard]] int effectiveThreads() const;

    EngineProcess process_;
    State state_ = State::Off;
    std::string error_;
    std::string engine_;
    std::string id_name_;
    std::vector<std::pair<std::string, std::string>> library_options_;
    std::vector<UciOptionInfo> options_;  // as the engine declared them
    AnalysisSettings settings_;
    AnalysisSettings sent_;      // what the engine was told
    bool options_due_  = false;  // settings changed: sent before the next search
    bool chess960_     = false;  // UCI_Chess960 sent
    bool ready_sent_   = false;
    bool new_game_due_ = false;
    std::chrono::milliseconds start_timeout_{60000};
    std::chrono::steady_clock::time_point deadline_{};  // of the answer awaited (uciok, readyok, bestmove)

    std::optional<AnalysisPosition> wanted_;  // to search next
    SearchLimit wanted_limit_;
    AnalysisPosition searched_;
    SearchLimit limit_;
    std::chrono::steady_clock::time_point search_start_{};
    bool finished_ = false;
    std::string bestmove_;
    std::vector<AnalysisLine> lines_;
    std::vector<std::string> last_words_;  // what it wrote last, for an error
    CoreClaim claim_;
};

}  // namespace fcg
