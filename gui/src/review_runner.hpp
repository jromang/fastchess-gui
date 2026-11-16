#pragma once

// The continuous analysis of a game (Board page): every position searched by an engine of the library at each pass,
// passTime(pass) each, four times longer at each pass, on a thread of its own with its own engine process, so that
// its pace does not depend on the window's frames. The positions go from the last to the first (the engine's hash
// then holds what comes after a position), the board's position first. Its threads count in the core budget while it
// searches.
//
// tools/analysis_bench.py measured the order and the replay of a position's previous principal variation before it
// (from its end back to it, for a quarter of its time), against searches of 2 M nodes per position, for the same
// total work (8 games each, about 1,000 positions): backward rather than forward cut the error of the winning chances
// by about 16 % and found the reference's best move 5 points more often, with Stockfish 19 as with Gaia 4.4.3; the
// replayed variation then lost a little (error +3 % and +9 %). Both stay as switches.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "analysis.hpp"
#include "engine_client.hpp"
#include "game_analysis.hpp"
#include "model.hpp"
#include "pgn_store.hpp"

namespace fcg {

inline constexpr bool REVIEW_BACKWARD  = true;
inline constexpr bool REVIEW_PV_REPLAY = false;

// The passes a ceiling allows: those whose time per position is at most `ceiling_ms`; 0, until stopped (as many as
// there are times).
[[nodiscard]] int reviewPasses(int64_t ceiling_ms);

// The positions a principal variation of `position` goes through, its last move left out (nothing comes after it):
// at most `max` of them, each legal, none after the game would end. From the first move to the last.
[[nodiscard]] std::vector<AnalysisPosition> pvPositions(const AnalysisPosition& position, const std::string& pv,
                                                        std::size_t max);

class ReviewRunner {
   public:
    struct Setup {
        EngineEntry engine;
        AnalysisSettings settings;  // its threads and hash (one line is searched)
        ReplayGame game;
        bool chess960      = false;
        int64_t ceiling_ms = 0;  // the time per position of the last pass; 0: until stopped
        GameReview resume;       // the passes done before, kept; passes 0: from the start
    };
    struct State {
        uint64_t version = 0;
        int pass         = 0;    // the pass being run
        GameReview live;         // each position's latest value, of this pass or the one before
        std::vector<int> level;  // the pass of each live value, -1 for none
        std::vector<bool> done;  // the positions searched at this pass
        GameReview settled;      // the last complete pass: what is kept and judged
        bool running = false;    // ends when the last pass is complete, or on an error
        std::string error;
    };

    ReviewRunner() = default;
    ~ReviewRunner() { stop(); }
    ReviewRunner(const ReviewRunner&)            = delete;
    ReviewRunner& operator=(const ReviewRunner&) = delete;

    // Starts the analysis on its thread.
    void start(Setup setup);
    // Ends it: the search under way stops, the engine is told to quit.
    void stop();
    // While the board's analysis (A) has the machine: no search, the one under way stopped and done again after.
    void setPaused(bool paused) { paused_ = paused; }
    // No pass beyond this one is started (several analyses of a game going pass by pass together).
    void setPassLimit(int pass) { pass_limit_ = pass; }
    void setCeiling(int64_t ceiling_ms) { ceiling_ = ceiling_ms; }
    // The board's position, searched first at each pass; -1 when the board shows something else.
    void setFirst(int ply) { first_ = ply; }
    // A position's value from a longer search (the board's analysis): taken when it went further than its pass.
    void offer(int ply, PositionEval value);

    // The state, copied when it changed since `version`; false when it did not.
    bool state(State& out, uint64_t& version) const;
    [[nodiscard]] bool running() const;
    // The threads its engine uses now, counted in the core budget.
    [[nodiscard]] int threads() const noexcept { return claim_.threads(); }

   private:
    void run(Setup setup, std::vector<AnalysisPosition> positions);
    template <typename F>
    void update(F f);

    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int> pass_limit_{1 << 20};
    std::atomic<int> first_{-1};
    std::atomic<int64_t> ceiling_{0};
    mutable std::mutex mutex_;
    State state_;
    std::vector<std::pair<int, PositionEval>> offers_;
    CoreClaim claim_;
};

}  // namespace fcg
