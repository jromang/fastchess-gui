// The logo: the icon's pixels, and the Windows and macOS icon files kept as the code draws them.

#include <doctest/doctest.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "logo.hpp"

using namespace fcg;
namespace fs = std::filesystem;

TEST_CASE("logo: a blue tile with rounded corners, the pawn in white, the hand cut out") {
    const int size = 72;
    const auto px  = logo::icon(size);
    REQUIRE(px.size() == size * size * 4);
    const auto at = [&](int x, int y) { return &px[(y * size + x) * 4]; };
    CHECK(at(0, 0)[3] == 0);  // outside the rounded corner
    const auto* tile = at(size / 2, 3);
    CHECK(tile[3] == 255);
    CHECK(tile[2] == 222);  // the light theme's accent
    // The base of the pawn (grid y 19.6 to 22 of 24, on the middle 2/3 of the tile).
    const auto grid  = [&](float g) { return static_cast<int>(size / 6.0f + g * size * 2 / 3 / 24); };
    const auto* base = at(size / 2, grid(20.8f));
    CHECK(base[0] == 255);
    CHECK(base[1] == 255);
    // The pivot of the hand, in the middle of the head, is the tile's colour.
    CHECK(at(grid(12), grid(7.6f))[0] == 47);
}

TEST_CASE("logo: the Windows icon is the logo drawn (FCG_UPDATE_ICON=1 writes it)") {
    const auto ico = logo::icoFile();
    REQUIRE(ico.size() > 6);
    CHECK(ico[2] == 1);  // an icon
    CHECK(ico[4] == 8);  // 8 images

    const char* env     = std::getenv("FCG_SOURCE_DIR");
    const fs::path path = fs::path(env ? env : FCG_SOURCE_DIR) / "gui" / "win" / "fastchess-gui.ico";
    const char* update  = std::getenv("FCG_UPDATE_ICON");
    if (update && std::string(update) == "1") {
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(ico.data()), ico.size());
    }
#ifdef __linux__
    // Floating point may round a pixel differently with another compiler (or another processor's fused
    // multiply-add): compared where the file is written.
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> committed((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK_MESSAGE(committed == ico, "gui/win/fastchess-gui.ico is not the logo drawn: FCG_UPDATE_ICON=1");
#endif
}

TEST_CASE("logo: the macOS icon is the logo drawn (FCG_UPDATE_ICON=1 writes it)") {
    const auto icns = logo::icnsFile();
    REQUIRE(icns.size() > 16);
    CHECK(std::string(icns.begin(), icns.begin() + 4) == "icns");
    // The length of the file, then of each image, big-endian: the images fill the file exactly.
    const auto big = [&](std::size_t at) {
        return static_cast<std::size_t>(icns[at]) << 24 | static_cast<std::size_t>(icns[at + 1]) << 16 |
               static_cast<std::size_t>(icns[at + 2]) << 8 | icns[at + 3];
    };
    CHECK(big(4) == icns.size());
    int images     = 0;
    std::size_t at = 8;
    for (; at + 8 <= icns.size() && big(at + 4) >= 8; at += big(at + 4)) {
        CHECK(icns[at + 8] == 0x89);  // a PNG image
        CHECK(icns[at + 9] == 'P');
        ++images;
    }
    CHECK(at == icns.size());
    CHECK(images == 10);

    const char* env     = std::getenv("FCG_SOURCE_DIR");
    const fs::path path = fs::path(env ? env : FCG_SOURCE_DIR) / "gui" / "mac" / "fastchess-gui.icns";
    const char* update  = std::getenv("FCG_UPDATE_ICON");
    if (update && std::string(update) == "1") {
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(icns.data()), icns.size());
    }
#ifdef __linux__
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> committed((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK_MESSAGE(committed == icns, "gui/mac/fastchess-gui.icns is not the logo drawn: FCG_UPDATE_ICON=1");
#endif
}
