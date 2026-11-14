// A PGN book from someone else, turned into a Polyglot book for an engine's own book: fastchess-gui's own reading of
// its games (fastchess' reader, for the openings a tournament plays, is app_pgn's).

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>

#include <tour/pgn_book.hpp>

#include "fuzz_file.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    const auto file = fcg::fuzz::inputFile(data, size, ".pgn");
    try {
        const auto bin = std::filesystem::path(file).replace_extension(".bin");
        (void)fcg::tour::pgnToPolyglot(file, bin, 16, false);
    } catch (const std::exception&) {
    }
    return 0;
}
