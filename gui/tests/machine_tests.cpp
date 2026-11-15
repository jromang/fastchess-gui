// What a setup's games take of the machine, and what the machine's budget (Settings) lets it play at once.

#include <doctest/doctest.hpp>

#include <filesystem>

#include "setup_checks.hpp"
#include "storage.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

EngineEntry engine(const std::string& name, int hash, bool gpu = false) {
    EngineEntry e;
    e.name = name;
    e.cmd  = "/bin/" + name;
    e.uci_options.push_back({"Hash", "spin", std::to_string(hash), "1", "65536", {}});
    e.uci_options.push_back({"Threads", "spin", "1", "1", "512", {}});
    e.gpu = gpu;
    return e;
}

MatchSetup roundRobin(const std::vector<std::string>& names) {
    MatchSetup s;
    s.format = TournamentFormat::RoundRobin;
    for (const auto& n : names) {
        ParticipantSetup p;
        p.name = n;
        s.participants.push_back(p);
    }
    return s;
}

}  // namespace

TEST_CASE("machine: a setup's heaviest game, and what the memory and the GPUs of the budget let it play at once") {
    const std::vector<EngineEntry> engines = {engine("A", 256), engine("B", 1024), engine("C", 64, true),
                                              engine("D", 64, true)};
    auto setup                             = roundRobin({"A", "B", "C"});
    setup.common_threads                   = 2;
    const auto cost                        = setupGameCost(setup, engines);
    CHECK(cost.threads == 2);
    // The two largest Hashes, with what each process takes besides.
    CHECK(cost.memory_mb == 1024 + 256 + 2 * tour::PROCESS_MB + 2 * 2 * tour::THREAD_MB);
    CHECK(cost.gpus == 1);

    tour::Budget budget;
    budget.threads   = 16;
    budget.gpus      = 8;
    budget.memory_mb = 64 * 1024;
    // Plenty: nothing to say.
    auto limit = budgetLimit(setup, engines, budget, 8);
    CHECK(limit.games == 8);
    CHECK(limit.why.empty());

    // Two GPUs: two games with the GPU engine at a time, the others beside them.
    budget.gpus = 2;
    limit       = budgetLimit(setup, engines, budget, 8);
    CHECK(limit.games == 8);
    CHECK(limit.gpu_games == 2);
    CHECK_FALSE(limit.why.empty());
    CHECK_FALSE(limit.warn);

    // Room for two games in memory.
    budget.memory_mb = 2 * cost.memory_mb + 100;
    limit            = budgetLimit(setup, engines, budget, 8);
    CHECK(limit.games == 2);
    CHECK_FALSE(limit.why.empty());
    CHECK_FALSE(limit.warn);

    // An engine that uses a GPU, and no GPU counted: games may share one.
    budget.memory_mb = 0;
    budget.gpus      = 0;
    limit            = budgetLimit(setup, engines, budget, 8);
    CHECK(limit.warn);

    // Two GPU engines meet, one GPU: such a game plays alone.
    const auto two = roundRobin({"C", "D"});
    budget.gpus    = 1;
    limit          = budgetLimit(two, engines, budget, 4);
    CHECK(setupGameCost(two, engines).gpus == 2);
    CHECK(limit.warn);
    CHECK(limit.gpu_games == 1);
}

TEST_CASE("machine: the budget is kept in the profile, its defaults as zeros") {
    const auto dir = fs::temp_directory_path() / "fcg-tests-machine-budget";
    fs::remove_all(dir);
    const Storage storage(dir);
    CHECK(storage.machineBudget() == MachineBudget{});
    MachineBudget b;
    b.cores           = 12;
    b.memory_mb       = 32000;
    b.gpus            = 2;
    b.queue_when_free = true;
    storage.setMachineBudget(b);
    CHECK(Storage(dir).machineBudget() == b);

    // An engine that uses a GPU says so in the library, only when it does.
    auto e = engine("Lc0", 0, true);
    CHECK(nlohmann::json(e).value("gpu", false));
    CHECK(nlohmann::json(e).get<EngineEntry>().gpu);
    e.gpu = false;
    CHECK_FALSE(nlohmann::json(e).contains("gpu"));
    fs::remove_all(dir);
}
