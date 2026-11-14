// An EPD file from someone else, read as the program reads it: an opening book by fastchess' own reader (through the
// book inspection that also checks each position), and a test suite.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>

#include <tour/book_scan.hpp>
#include <tour/epd_suite.hpp>

#include "fuzz_file.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    const auto file = fcg::fuzz::inputFile(data, size, ".epd");
    try {
        const fcg::tour::BookScan scan(file, -1);
        for (std::size_t i = 0; i < std::min<std::size_t>(scan.size(), 64); ++i) (void)scan.item(i);
    } catch (const std::exception&) {
    }
    try {
        (void)fcg::tour::readSuite(file);
    } catch (const std::exception&) {
    }
    return 0;
}
