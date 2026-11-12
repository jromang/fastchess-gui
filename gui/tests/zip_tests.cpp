#include <doctest/doctest.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

// stb_image's inflater, only to read back what the zip writer deflated.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include "zip.hpp"

namespace {

uint32_t read16(const std::string& s, std::size_t at) {
    return static_cast<uint8_t>(s[at]) | (static_cast<uint8_t>(s[at + 1]) << 8);
}

uint32_t read32(const std::string& s, std::size_t at) { return read16(s, at) | (read16(s, at + 2) << 16); }

// The one file of an archive zipArchive() wrote: its name and its data, inflated.
std::pair<std::string, std::string> unzip(const std::string& zip) {
    REQUIRE(zip.size() > 30 + 46 + 22);
    REQUIRE(read32(zip, 0) == 0x04034b50);
    const auto method     = read16(zip, 8);
    const auto compressed = read32(zip, 18);
    const auto size       = read32(zip, 22);
    const auto name_len   = read16(zip, 26);
    const auto name       = zip.substr(30, name_len);
    const auto body       = zip.substr(30 + name_len, compressed);
    if (method == 0) return {name, body};

    REQUIRE(method == 8);
    int length = 0;
    char* data = stbi_zlib_decode_noheader_malloc(body.data(), static_cast<int>(body.size()), &length);
    REQUIRE(data != nullptr);
    std::string out(data, static_cast<std::size_t>(length));
    std::free(data);
    CHECK(out.size() == size);
    return {name, out};
}

}  // namespace

TEST_CASE("zip: one file deflated, read back as written, with its central directory") {
    std::string pgn;
    for (int i = 0; i < 200; ++i)
        pgn += "[Event \"CCRL\"]\n[Round \"" + std::to_string(i + 1) + "\"]\n\n1. e4 e5 1-0\n\n";
    const std::string name = "[Tester 2026-11-12] Seed 64-bit - Nov 12 (hash 256MB).pgn";
    const auto zip         = fcg::zipArchive(name, pgn, 0x6000, 0x5d6c);

    CHECK(read16(zip, 8) == 8);  // deflated: a PGN shrinks
    CHECK(zip.size() < pgn.size() / 4);
    const auto [read_name, data] = unzip(zip);
    CHECK(read_name == name);
    CHECK(data == pgn);

    // The end record points to the central directory, which names the same file with the same sizes and CRC.
    const auto end = zip.size() - 22;
    REQUIRE(read32(zip, end) == 0x06054b50);
    CHECK(read16(zip, end + 10) == 1);
    const auto directory = read32(zip, end + 16);
    REQUIRE(read32(zip, directory) == 0x02014b50);
    CHECK(read32(zip, directory + 16) == read32(zip, 14));  // CRC
    CHECK(read32(zip, directory + 24) == pgn.size());
    CHECK(zip.substr(directory + 46, name.size()) == name);
    CHECK(read32(zip, end + 12) == end - directory);

    // The CRC-32 of the data, as every zip tool checks it: "123456789" is CBF43926.
    const auto check = fcg::zipArchive("a", "123456789", 0, 0);
    CHECK(read32(check, 14) == 0xCBF43926);

    // What does not shrink is stored as is.
    const auto tiny = fcg::zipArchive("a.txt", "x", 0, 0);
    CHECK(read16(tiny, 8) == 0);
    CHECK(unzip(tiny).second == "x");
    CHECK(unzip(fcg::zipArchive("empty", "", 0, 0)).second.empty());

    const auto file = std::filesystem::temp_directory_path() / "fcg-zip-test.zip";
    REQUIRE(fcg::writeZip(file, "games.pgn", pgn));
    std::ifstream in(file, std::ios::binary);
    const std::string written((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(unzip(written).second == pgn);
    in.close();
    std::filesystem::remove(file);
}
