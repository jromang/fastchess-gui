#pragma once

// Runs an EPD test suite: every position, for every chosen engine, several at a time, through fastchess' engine
// class. A search stops early once a solution has held for a few plies of depth.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <json.hpp>
#include <types/engine_config.hpp>

namespace fastchess::engine {
class UciEngine;
}

#include <tour/epd_suite.hpp>

#include "model.hpp"

namespace fcg {

// What the Suites page was last set to.
struct SuiteSetup {
    std::string file;
    std::vector<std::string> engines;             // engine list entries
    LimitKind limit       = LimitKind::MoveTime;  // MoveTime, Nodes or Depth
    double move_s         = 1.0;
    uint64_t nodes        = 1'000'000;
    int depth             = 15;
    int hash              = 0;  // MB, 0 = each engine's own
    int threads           = 0;
    int hold              = 3;  // plies a solution must hold before the search stops; 0 = never stops early
    bool concurrency_auto = true;
    int concurrency       = 1;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SuiteSetup, file, engines, limit, move_s, nodes, depth, hash, threads,
                                                hold, concurrency_auto, concurrency)

// One engine's answers to a suite: a column of the grid.
struct SuiteColumn {
    std::string engine;
    std::string when;   // "2026-11-10 20:15" for a saved run, empty for the one being played
    std::string limit;  // "1 s/move"
    std::vector<std::optional<tour::SuiteAnswer>> answers;  // by position, nullopt until answered

    struct Totals {
        int answered    = 0;
        int solved      = 0;
        int64_t time_ms = 0;  // to solution, summed over the solved positions
        int points      = 0;
    };
    [[nodiscard]] Totals totals() const;
};

// The text of a limit, "1 s/move", "1M nodes", "depth 15".
[[nodiscard]] std::string suiteLimitText(const SuiteSetup& setup);

// The grid as CSV: one row per position (id, solution, then each column's move and time), the totals last.
[[nodiscard]] std::string suiteCsv(const tour::Suite& suite, const std::vector<SuiteColumn>& columns);

// A finished run, kept in `dir` to compare later versions with: <suite>-<date>.json. Returns the file.
std::filesystem::path saveSuiteRun(const std::filesystem::path& dir, const std::filesystem::path& suite_file,
                                   std::size_t positions, const std::vector<SuiteColumn>& columns);
// The runs of `suite_file` kept in `dir` (same name and number of positions), newest first: (file, its label).
[[nodiscard]] std::vector<std::pair<std::filesystem::path, std::string>> savedSuiteRuns(
    const std::filesystem::path& dir, const std::filesystem::path& suite_file, std::size_t positions);
[[nodiscard]] std::vector<SuiteColumn> loadSuiteRun(const std::filesystem::path& file);

class SuiteRunner {
   public:
    SuiteRunner() = default;
    ~SuiteRunner();
    SuiteRunner(const SuiteRunner&)            = delete;
    SuiteRunner& operator=(const SuiteRunner&) = delete;

    // Starts on `suite` with `engines` (their Hash and Threads already set), `concurrency` searches at a time.
    void start(const tour::Suite& suite, std::vector<fastchess::EngineConfiguration> engines, const SuiteSetup& setup,
               int concurrency);
    // Asks the searches to stop; the positions not answered stay so.
    void stop();
    [[nodiscard]] bool running() const;
    [[nodiscard]] bool stopped() const noexcept { return stop_; }
    [[nodiscard]] std::size_t answered() const noexcept { return answered_; }
    [[nodiscard]] std::size_t total() const noexcept { return total_; }
    [[nodiscard]] std::vector<SuiteColumn> columns() const;

   private:
    void work();
    tour::SuiteAnswer think(fastchess::engine::UciEngine& engine, const tour::SuitePosition& position, bool& failed);
    void join();

    tour::Suite suite_;
    std::vector<fastchess::EngineConfiguration> engines_;
    SuiteSetup setup_;
    std::vector<std::thread> workers_;
    std::atomic<std::size_t> next_{0}, answered_{0}, total_{0};
    std::atomic<int> active_{0};
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    std::vector<SuiteColumn> columns_;
};

}  // namespace fcg
