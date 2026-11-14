// The web server's reading of a request, as a client on the internet sends it.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "broadcast.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    const std::string_view request(reinterpret_cast<const char*>(data), size);
    const auto r = fcg::readWebRequest(request);
    // The tag the client kept is a view into the request: every byte of it is read, so that one outside the request
    // would be seen by AddressSanitizer.
    volatile unsigned sum = 0;
    for (const char c : r.if_none_match) sum = sum + static_cast<unsigned char>(c);
    if (!r.if_none_match.empty() && (r.if_none_match.data() < request.data() ||
                                     r.if_none_match.data() + r.if_none_match.size() > request.data() + size)) {
        std::abort();
    }
    return 0;
}
