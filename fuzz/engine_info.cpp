// An engine's info line, read by fastchess' parsers, as the live view, the broadcasts and the tournament read it:
// the engines are other people's programs. Its crashes are fastchess' (app/), fixed there.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <engine/uci_engine.hpp>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    const std::string_view line(reinterpret_cast<const char*>(data), size);
    using fastchess::engine::UciEngine;
    (void)UciEngine::parseInfo(line);
    (void)UciEngine::getScore(line);
    (void)UciEngine::getPv(line);
    return 0;
}
