// The TLCS server's reading of a datagram, as anyone on the network can send one to its UDP port.

#include <cstddef>
#include <cstdint>
#include <string>

#include "broadcast.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    (void)fcg::readTlcsMessage(std::string(reinterpret_cast<const char*>(data), size));
    return 0;
}
