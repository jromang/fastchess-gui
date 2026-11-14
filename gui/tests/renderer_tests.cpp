// What draws the window without OpenGL: SDL's software renderer, its textures (the pieces), the screenshots, and the
// setting that asks for it.

#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include <imgui.h>

#define STBI_NO_STDIO
#include <stb_image.h>  // its PNG reader, compiled in zip_tests.cpp

#include "board_view.hpp"
#include "renderer.hpp"
#include "storage.hpp"

using namespace fcg;
namespace fs = std::filesystem;

TEST_CASE("renderer: OpenGL version strings, desktop OpenGL 3.2 or later") {
    CHECK(render::glVersionAtLeast("4.6 (Core Profile) Mesa 24.2.8", 3, 2));
    CHECK(render::glVersionAtLeast("3.2.0 NVIDIA 550.54", 3, 2));
    CHECK(render::glVersionAtLeast("3.3.0 - Build 31.0.101.2111", 3, 2));
    CHECK(render::glVersionAtLeast("4.1 Metal - 88", 3, 2));
    CHECK_FALSE(render::glVersionAtLeast("3.1 Mesa 21.0", 3, 2));
    CHECK_FALSE(render::glVersionAtLeast("2.1 Mesa 20.3.5", 3, 2));
    CHECK_FALSE(render::glVersionAtLeast("1.1.0", 3, 2));  // Windows' own, on a remote desktop
    CHECK_FALSE(render::glVersionAtLeast("OpenGL ES 3.2 Mesa 24.2", 3, 2));
    CHECK_FALSE(render::glVersionAtLeast("", 3, 2));
    CHECK_FALSE(render::glVersionAtLeast(nullptr, 3, 2));
}

TEST_CASE("renderer: software rendering is kept in settings.json, off by default") {
    const auto root =
        fs::temp_directory_path() /
        ("fcg-tests-software-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        Storage storage(root);
        CHECK_FALSE(storage.softwareRendering());
        storage.setSoftwareRendering(true);
        CHECK(storage.softwareRendering());
    }
    CHECK(Storage(root).softwareRendering());
    Storage(root).setSoftwareRendering(false);
    CHECK_FALSE(Storage(root).softwareRendering());
    std::error_code ec;
    fs::remove_all(root, ec);
}

namespace {

struct Rgb {
    int r = 0, g = 0, b = 0;
};

}  // namespace

TEST_CASE("renderer: SDL's software driver draws a board, its pieces and lines, and saves it as a screenshot") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window* window = render::open("renderer test", 400, 400, SDL_WINDOW_HIDDEN, true);
    REQUIRE(window != nullptr);
    CHECK(render::path() == render::Path::Sdl);
    CHECK(render::software());
    CHECK_FALSE(render::viewports());
    CHECK_FALSE(render::mipmapped());
    CHECK(render::fallbackReason().empty());
    CHECK(render::description().find("software") != std::string::npos);

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    render::initBackends();

    const auto file =
        fs::temp_directory_path() /
        ("fcg-tests-software-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    {
        PieceAtlas atlas;
        REQUIRE(atlas.create(160));
        // Smaller images for small boards, there being no mipmaps.
        CHECK(atlas.texture(20.0f) != atlas.texture(160.0f));
        CHECK(atlas.texture(300.0f) == atlas.texture(160.0f));

        // The starting position at the window's corner, 320 pixels wide, under a line; then frames like the first,
        // which the driver does not draw again, the screenshot taken from what it kept.
        const chess::Board board;
        BoardDecor decor;
        decor.eval_white = -1.0f;  // no evaluation bar
        for (int i = 0; i < 8; ++i) {
            render::newFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(400, 400));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin("board", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
            drawBoard(atlas, board, 320.0f, decor);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(0, 360), ImVec2(400, 360), IM_COL32(255, 0, 0, 255), 1.0f);
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::Render();
            render::draw(400, 400);
            render::present();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(render::screenshot(file.string(), 400, 400));
    }
    render::shutdown();
    ImGui::DestroyContext();
    SDL_Quit();
    SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);

    std::ifstream in(file, std::ios::binary);
    const std::vector<unsigned char> png((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::error_code ec;
    fs::remove(file, ec);
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels =
        stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &width, &height, &channels, 3);
    REQUIRE(pixels != nullptr);
    CHECK(width == 400);
    CHECK(height == 400);
    const auto at = [&](int x, int y) {
        const unsigned char* p = pixels + (static_cast<std::size_t>(y) * width + x) * 3;
        return Rgb{p[0], p[1], p[2]};
    };
    // The pixels of a square (40 x 40, a8 at the corner) that pass a test.
    const auto count = [&](int file_index, int rank, auto&& test) {
        int n = 0;
        for (int y = (7 - rank) * 40; y < (8 - rank) * 40; ++y)
            for (int x = file_index * 40; x < (file_index + 1) * 40; ++x) n += test(at(x, y)) ? 1 : 0;
        return n;
    };

    // e4, empty: the light squares' colour (222, 227, 230), all of it.
    const auto e4 = at(4 * 40 + 20, 4 * 40 + 20);
    CHECK(e4.r == 222);
    CHECK(e4.g == 227);
    CHECK(e4.b == 230);
    // The black king on e8 (a light square), the white one on e1 (a dark square, 140, 162, 173).
    CHECK(count(4, 7, [](Rgb c) { return c.r < 60 && c.g < 60 && c.b < 60; }) > 100);
    CHECK(count(4, 0, [](Rgb c) { return c.r > 240 && c.g > 240 && c.b > 240; }) > 100);
    // The thin red line under the board, across the window. The driver places points to the half pixel: the line
    // is spread over two rows at about half its strength.
    int red = 0;
    for (int x = 0; x < 400; ++x) {
        for (int y = 357; y <= 362; ++y) {
            const auto c = at(x, y);
            red += c.r > 100 && c.r > c.g + 60 && c.r > c.b + 60 ? 1 : 0;
        }
    }
    CHECK(red >= 400);
    stbi_image_free(pixels);
}
