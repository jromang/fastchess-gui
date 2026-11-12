#include "memory.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <fstream>
#    include <sstream>
#endif

#include <fmt/include/fmt/format.h>
#include <matchmaking/tournament/base/tournament.hpp>

#include "i18n.hpp"

namespace fcg {

namespace {

uint64_t number(const std::string& text) {
    try {
        return static_cast<uint64_t>(std::max(0LL, std::stoll(text)));
    } catch (...) {
        return 0;
    }
}

const UciOptionInfo* uciOption(const EngineEntry& entry, const std::string& name) {
    for (const auto& o : entry.uci_options) {
        if (o.name == name) return &o;
    }
    return nullptr;
}

// The Hash an engine plays with when the tournament does not force one: its setting, else its default.
uint64_t ownHash(const EngineEntry& entry) {
    if (const auto* v = entry.option("Hash")) return number(*v);
    if (const auto* o = uciOption(entry, "Hash")) return number(o->default_value);
    return 0;
}

const EngineEntry* findEngine(const std::vector<EngineEntry>& engines, const std::string& name) {
    for (const auto& e : engines) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

}  // namespace

namespace {

std::optional<SystemMemory> readSystemMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) return std::nullopt;
    return SystemMemory{status.ullTotalPhys >> 20, status.ullAvailPhys >> 20};
#else
    std::ifstream in("/proc/meminfo");
    SystemMemory memory;
    bool total = false, available = false;
    for (std::string line; std::getline(in, line);) {
        std::istringstream fields(line);
        std::string key;
        uint64_t kb = 0;
        if (!(fields >> key >> kb)) continue;
        if (key == "MemTotal:") memory.total_mb = kb >> 10, total = true;
        if (key == "MemAvailable:") memory.available_mb = kb >> 10, available = true;
    }
    if (!total || !available) return std::nullopt;
    return memory;
#endif
}

}  // namespace

std::optional<SystemMemory> systemMemory() {
    // Asked at every frame by the setup summary: the system is asked once a second.
    static std::mutex mutex;
    static std::optional<SystemMemory> last;
    static std::chrono::steady_clock::time_point when{};
    std::lock_guard<std::mutex> lock(mutex);
    if (const auto now = std::chrono::steady_clock::now(); now - when >= std::chrono::seconds(1)) {
        last = readSystemMemory();
        when = now;
    }
    return last;
}

std::string formatMemory(uint64_t mb) {
    if (mb < 1024) return trf("{} MB", mb);
    return trf("{:.1f} GB", static_cast<double>(mb) / 1024.0);
}

std::string MemoryNeed::text() const {
    const auto need = trn("Hash tables: up to {} in {} engine process", "Hash tables: up to {} in {} engine processes",
                          processes, formatMemory(hash_mb), processes);
    if (aboveInstalled()) {
        return trf("{}, more than the {} of memory installed: the tournament cannot start.", need,
                   formatMemory(system->total_mb));
    }
    if (aboveAvailable()) {
        return trf("{}, more than the {} of memory free: the system will swap.", need,
                   formatMemory(system->available_mb));
    }
    if (system) return trf("{}, of {} free", need, formatMemory(system->available_mb));
    return need;
}

MemoryNeed hashMemory(const std::vector<uint64_t>& hashes, int concurrency, bool affinity) {
    MemoryNeed need;
    if (hashes.empty()) return need;

    const auto games   = static_cast<std::size_t>(std::max(1, concurrency));
    const auto engines = hashes.size();
    // One cache per game with affinity, otherwise one for all.
    const std::size_t kept = affinity ? games * fastchess::BaseTournament::engineCacheSize(engines, 1, games)
                                      : fastchess::BaseTournament::engineCacheSize(engines, games, 1);

    // Up to one process per game of each engine, the largest hashes first, until the processes kept are filled.
    auto sorted = hashes;
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    std::size_t left = kept;
    for (const auto hash : sorted) {
        const auto n = std::min(left, games);
        need.hash_mb += hash * n;
        need.processes += static_cast<int>(n);
        left -= n;
        if (left == 0) break;
    }
    return need;
}

std::vector<uint64_t> setupHashes(const MatchSetup& setup, const std::vector<EngineEntry>& engines) {
    std::vector<ParticipantSetup> people;
    if (setup.format == TournamentFormat::Match) {
        people = {{setup.engine_a}, {setup.engine_b}};
    } else {
        people = setup.participants;
    }

    std::vector<uint64_t> hashes;
    for (const auto& p : people) {
        const auto* entry = findEngine(engines, p.name);
        if (!entry) continue;
        // As buildRun() forces it: on the engines that have the option, or whose options are not known yet.
        const int forced = p.hash > 0 ? p.hash : setup.common_hash;
        const bool has   = entry->uci_options.empty() || uciOption(*entry, "Hash");
        hashes.push_back(forced > 0 && has ? static_cast<uint64_t>(forced) : ownHash(*entry));
    }
    return hashes;
}

std::vector<uint64_t> specHashes(const tour::TournamentSpec& spec, const std::vector<EngineEntry>& engines) {
    std::vector<uint64_t> hashes;
    for (const auto& p : spec.participants) {
        if (!p.active) continue;  // its processes are ended
        uint64_t hash = 0;
        bool given    = false;
        for (const auto& [k, v] : p.engine.options) {
            if (k == "Hash") hash = number(v), given = true;
        }
        if (!given) {
            if (const auto* entry = findEngine(engines, p.engine.name)) hash = ownHash(*entry);
        }
        hashes.push_back(hash);
    }
    return hashes;
}

MemoryNeed setupMemory(const MatchSetup& setup, const std::vector<EngineEntry>& engines, const CpuTopology& topology) {
    const int concurrency =
        std::clamp(setup.concurrency_auto ? topology.defaultConcurrency(clockFree(setup.limit)) : setup.concurrency, 1,
                   std::max(1, topology.logical));
    auto need   = hashMemory(setupHashes(setup, engines), concurrency, setup.affinity);
    need.system = systemMemory();
    return need;
}

MemoryNeed specMemory(const tour::TournamentSpec& spec, const std::vector<EngineEntry>& engines) {
    auto need   = hashMemory(specHashes(spec, engines), spec.conditions.concurrency, spec.conditions.affinity);
    need.system = systemMemory();
    return need;
}

}  // namespace fcg
