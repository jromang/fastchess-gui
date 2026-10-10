#pragma once

#include <utility>
#include <vector>

namespace fcg {

// Processor counts of the machine the GUI runs on, measured at startup.
struct CpuTopology {
    int logical  = 1;  // hardware threads the process may use
    int physical = 1;  // distinct cores among them

    // Default concurrency: every hardware thread at fixed nodes, since SMT does not change a node count;
    // one game per physical core minus one with a clock, since SMT siblings slow each other down.
    [[nodiscard]] int defaultConcurrency(bool fixed_nodes) const noexcept {
        if (fixed_nodes) return logical;
        return physical > 1 ? physical - 1 : 1;
    }
};

// Builds a topology from (package id, core id) pairs, one per logical processor.
[[nodiscard]] CpuTopology topologyFromCores(const std::vector<std::pair<int, int>>& package_core);

// Queries the operating system.
[[nodiscard]] CpuTopology detectTopology();

}  // namespace fcg
