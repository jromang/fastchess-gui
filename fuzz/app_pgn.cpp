// A PGN opening book from someone else, read by fastchess' own reader as a tournament's openings are, through the
// book inspection that also checks each position. Its crashes are fastchess' (app/), fixed there.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>

#include <tour/book_scan.hpp>

#include "fuzz_file.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    const auto file = fcg::fuzz::inputFile(data, size, ".pgn");
    try {
        const fcg::tour::BookScan scan(file, -1);
        for (std::size_t i = 0; i < std::min<std::size_t>(scan.size(), 64); ++i) (void)scan.item(i);
    } catch (const std::exception&) {
    }
    return 0;
}
