#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <affinity/affinity_manager.hpp>
#include <core/filesystem/file_writer.hpp>
#include <core/globals/globals.hpp>
#include <core/logger/logger.hpp>
#include <core/memory/cache.hpp>
#include <core/threading/threadpool.hpp>
#include <engine/uci_engine.hpp>
#include <game/book/opening_book.hpp>
#include <matchmaking/game_pair.hpp>
#include <matchmaking/output/output.hpp>
#include <matchmaking/scoreboard.hpp>
#include <matchmaking/timeout_tracker.hpp>
#include <matchmaking/tournament/schedule/scheduler.hpp>
#include <types/tournament.hpp>

namespace fastchess {

class BaseTournament {
    using EngineCache = util::CachePool<engine::UciEngine, std::string>;

   public:
    BaseTournament(const stats_map& results);
    virtual ~BaseTournament();

    // Starts the tournament
    virtual void start();

    [[nodiscard]] stats_map getResults() noexcept { return scoreboard_.getResults(); }

    [[nodiscard]] std::uint64_t gamesPlayed() const noexcept { return match_count_; }
    [[nodiscard]] std::uint64_t totalGames() const noexcept { return final_matchcount_; }

    // An engine that fails to start is tried again this many times, after a pause, before the tournament stops.
    static constexpr int ENGINE_START_RETRIES = 3;
    static constexpr std::chrono::seconds ENGINE_START_RETRY_PAUSE{10};

    // At most this many engine processes wait for their next game, besides those of the games being played (see
    // engineCacheSize()).
    static constexpr std::size_t MAX_IDLE_ENGINES = 32;

    // The size of an engine cache serving `games` games at a time, `caches` caches sharing the tournament's
    // `engines` different engines (one cache per game thread with affinity, otherwise a single one).
    [[nodiscard]] static std::size_t engineCacheSize(std::size_t engines, std::size_t games, std::size_t caches);

   protected:
    using start_fn  = std::function<void()>;
    using finish_fn = std::function<void(const Stats& stats, const std::string& reason, const engines&)>;

    EngineCache& getEngineCache();

    // Ends the processes kept for later games of the engines `retired` names (no game left, withdrawn,
    // replaced...) that are not playing now.
    void retireEngines(const std::function<bool(const std::string& name)>& retired);

    // How many different engines still have games to play, as the front end knows it; the engine caches are sized
    // on it. Until it is told, the engines of the configuration are counted.
    void setEnginesLeft(std::size_t engines);

    // Gets called after a game has finished
    virtual void startNext() = 0;

    // Asked on the thread of game `game_id` while an engine of it fails to start: false gives the game up without
    // scoring it, rather than trying again and then stopping the tournament (a front end whose participant was
    // withdrawn meanwhile, or that is stopping once the running games are over).
    [[nodiscard]] virtual bool gameWanted(std::size_t /*game_id*/) const { return true; }

    // Asked like gameWanted(): true while the front end holds its games back (a pause). The wait before the next try
    // does not count meanwhile, so the try comes after the pause, and the pause uses up none of the tries.
    [[nodiscard]] virtual bool gameHeld(std::size_t /*game_id*/) const { return false; }

    // creates the matches
    void create();

    // Function to save the config file
    void saveJson();

    // play one game and write it to the pgn file
    void playGame(const GamePair<EngineConfiguration, EngineConfiguration>& configs, const start_fn& start,
                  const finish_fn& finish, const book::Opening& opening, std::size_t round_id, std::size_t game_id);

    // We keep engines alive after they were used to avoid the overhead of starting them again.
    ScoreBoard scoreboard_ = {};
    PlayerTracker tracker_ = {};
    util::ThreadPool pool_ = util::ThreadPool{1};

    std::unique_ptr<Scheduler> generator_;
    std::unique_ptr<IOutput> output_;
    std::unique_ptr<affinity::AffinityManager> cores_;
    std::unique_ptr<util::FileWriter> file_writer_pgn_;
    std::unique_ptr<util::FileWriter> file_writer_epd_;
    std::unique_ptr<book::OpeningBook> book_;

    std::atomic<std::uint64_t> match_count_      = 0;
    std::uint64_t initial_matchcount_            = 0;
    std::atomic<std::uint64_t> final_matchcount_ = 0;

    // Caches for non-affinity mode (shared by all threads)
    std::unique_ptr<EngineCache> global_cache_;

    // Per-Thread cache for affinity mode
    std::unordered_map<std::thread::id, std::unique_ptr<EngineCache>> thread_caches_;

    // Only need mutex for managing the cache containers, not the EngineCache itself
    std::mutex cache_management_mutex_;

    // setEnginesLeft(), 0 until it is called; cache_management_mutex_ held.
    std::size_t engines_left_ = 0;

   private:
    [[nodiscard]] std::size_t setResults(const stats_map& results);

    [[nodiscard]] int getMaxAffinity(const std::vector<EngineConfiguration>& configs) const noexcept;

    void restartEngine(std::unique_ptr<engine::UciEngine>& engine);
};

}  // namespace fastchess
