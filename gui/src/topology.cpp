#include "topology.hpp"

#include <algorithm>
#include <set>
#include <thread>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <sched.h>

#    include <fstream>
#    include <string>
#endif

namespace fcg {

CpuTopology topologyFromCores(const std::vector<std::pair<int, int>>& package_core) {
    CpuTopology topology;
    if (package_core.empty()) return topology;

    const std::set<std::pair<int, int>> cores(package_core.begin(), package_core.end());

    topology.logical  = static_cast<int>(package_core.size());
    topology.physical = static_cast<int>(cores.size());
    return topology;
}

namespace {

CpuTopology fallback() {
    CpuTopology topology;
    topology.logical  = std::max(1u, std::thread::hardware_concurrency());
    topology.physical = topology.logical;
    return topology;
}

#ifdef _WIN32

CpuTopology detect() {
    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    if (length == 0) return fallback();

    std::vector<char> buffer(length);
    auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &length)) return fallback();

    std::vector<std::pair<int, int>> package_core;
    int core = 0;

    for (DWORD offset = 0; offset < length;) {
        auto* entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);

        if (entry->Relationship == RelationProcessorCore) {
            for (WORD g = 0; g < entry->Processor.GroupCount; ++g) {
                auto mask = entry->Processor.GroupMask[g].Mask;
                while (mask) {
                    package_core.emplace_back(0, core);
                    mask &= mask - 1;
                }
            }
            ++core;
        }

        offset += entry->Size;
    }

    return topologyFromCores(package_core);
}

#else

int readInt(const std::string& path) {
    std::ifstream file(path);
    int value = -1;
    file >> value;
    return file ? value : -1;
}

CpuTopology detect() {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) != 0) return fallback();

    std::vector<std::pair<int, int>> package_core;

    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, &set)) continue;

        const auto base   = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/";
        const int package = readInt(base + "physical_package_id");
        const int core_id = readInt(base + "core_id");

        // Without topology information, count every processor as its own core.
        package_core.emplace_back(package, core_id < 0 ? 100000 + cpu : core_id);
    }

    if (package_core.empty()) return fallback();
    return topologyFromCores(package_core);
}

#endif

}  // namespace

CpuTopology detectTopology() {
    auto topology = detect();

    // fastchess rejects a concurrency above hardware_concurrency(), so never offer more.
    const int hw      = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    topology.logical  = std::clamp(topology.logical, 1, hw);
    topology.physical = std::clamp(topology.physical, 1, topology.logical);
    return topology;
}

}  // namespace fcg
