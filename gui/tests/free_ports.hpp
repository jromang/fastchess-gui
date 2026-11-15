#pragma once

// Ports for the tests' servers and clients, free when they are asked for: each test case may run in a process of its
// own, at the same time as the others, and another run of the tests may be going on. A block of consecutive ports
// drawn at random below the systems' ephemeral ranges (Linux's starts at 32768, macOS' and Windows' at 49152), so that
// no outgoing connection takes one meanwhile, each bound for TCP and UDP to check it, then let go. A port handed out
// once in a process is not handed out again in it.

#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>

#include "net.hpp"

namespace fcg::test {

inline uint16_t freePorts(int count) {
    static std::mutex mutex;
    static std::set<uint16_t> given;
    // random_device mixed with the clock: processes started together must not draw the same ports.
    static std::mt19937 random{
        std::random_device{}() ^
        static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count())};
    const std::lock_guard lock(mutex);
    std::uniform_int_distribution<int> first_port(20000, 32000 - count);
    for (int attempt = 0; attempt < 1000; ++attempt) {
        const int first = first_port(random);
        bool free       = true;
        for (int port = first; port < first + count && free; ++port) {
            net::TcpListener tcp;
            net::UdpSocket udp;
            const auto p = static_cast<uint16_t>(port);
            free         = given.count(p) == 0 && tcp.listen(p).empty() && udp.bind(p).empty();
        }
        if (!free) continue;
        for (int port = first; port < first + count; ++port) given.insert(static_cast<uint16_t>(port));
        return static_cast<uint16_t>(first);
    }
    throw std::runtime_error("no free port for the tests");
}

inline uint16_t freePort() { return freePorts(1); }

}  // namespace fcg::test
