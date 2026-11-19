// How the boards look and sound: board colours, piece sets, move sounds; and the engines' logos, in the window and on
// the broadcast web page.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "appearance.hpp"
#include "board_view.hpp"
#include "broadcast.hpp"
#include "engine_logos.hpp"
#include "free_ports.hpp"
#include "net.hpp"
#include "piece_sets.hpp"
#include "portable.hpp"
#include "sound.hpp"
#include "storage.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() /
                     (name + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    return dir;
}

void write(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

// A small picture: a red square on a transparent background, `w` x `h`.
logos::Image picture(int w, int h) {
    logos::Image image;
    image.width  = w;
    image.height = h;
    image.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
    for (int y = h / 4; y < h * 3 / 4; ++y) {
        for (int x = w / 4; x < w * 3 / 4; ++x) {
            auto* p = &image.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
            p[0]    = 220;
            p[3]    = 255;
        }
    }
    return image;
}

constexpr const char* SVG_SQUARE =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 40 20"><rect x="0" y="0" width="40" height="20" fill="#3060c0"/></svg>)";

}  // namespace

TEST_CASE("appearance: board colours, presets and custom, for each theme") {
    appearance::Board board;
    CHECK(appearance::colours(board) == appearance::boardPresets().front().colours);
    // The default is the program's board before there were others.
    CHECK(appearance::boardPresets().front().colours == appearance::BoardColours{});
    CHECK(appearance::boardPresets().size() >= 5);

    board.preset = "brown";
    CHECK(appearance::colours(board).light == IM_COL32(240, 217, 181, 255));
    board.preset = "no such preset";
    CHECK(appearance::colours(board) == appearance::boardPresets().front().colours);
    board.preset       = appearance::CUSTOM;
    board.custom.light = IM_COL32(1, 2, 3, 255);
    CHECK(appearance::colours(board).light == IM_COL32(1, 2, 3, 255));

    // The theme in use picks its board.
    appearance::Settings s;
    s.dark_board.preset = "green";
    const auto before   = appearance::current();
    appearance::set(s);
    CHECK(appearance::boardColours(ui::Theme::Dark) == appearance::boardPreset("green")->colours);
    CHECK(appearance::boardColours(ui::Theme::Light) == appearance::boardPresets().front().colours);
    appearance::set(before);
}

TEST_CASE("appearance: colours as #rrggbbaa, and the settings in settings.json") {
    CHECK(appearance::hexColour(IM_COL32(0x12, 0xab, 0xff, 0x40)) == "#12abff40");
    CHECK(appearance::parseHexColour("#12abff40") == IM_COL32(0x12, 0xab, 0xff, 0x40));
    CHECK(appearance::parseHexColour("#12ABFF") == IM_COL32(0x12, 0xab, 0xff, 0xff));
    CHECK_FALSE(appearance::parseHexColour("12abff").has_value());
    CHECK_FALSE(appearance::parseHexColour("#12abfg").has_value());
    CHECK_FALSE(appearance::parseHexColour("#12ab").has_value());

    appearance::Settings s;
    s.light_board.preset           = appearance::CUSTOM;
    s.light_board.custom.highlight = IM_COL32(10, 20, 30, 40);
    s.dark_board.preset            = "grey";
    s.pieces                       = appearance::FOLDER;
    s.piece_folder                 = "/some/pieces";
    s.sounds                       = true;
    s.volume                       = 35;
    s.engine_logos                 = true;
    s.board3d.on                   = true;
    s.board3d.lighting             = "golden_hour";
    s.board3d.exposure             = -0.5f;
    s.board3d.varnish              = 0.25f;
    s.board3d.gloss                = 0.75f;
    s.board3d.samples              = 128;
    nlohmann::json j;
    appearance::to_json(j, s);
    appearance::Settings back;
    appearance::from_json(j, back);
    CHECK(back == s);

    // What is missing or of another type keeps its default; the volume stays in its range.
    appearance::Settings odd;
    appearance::from_json(nlohmann::json::parse(R"({"pieces": 3, "volume": 900, "board_dark": {"light": "red"},
                                                    "board3d": {"on": "yes", "lighting": 2}})"),
                          odd);
    CHECK(odd.pieces == "cburnett");
    CHECK(odd.board3d == appearance::Board3d{});
    CHECK(odd.volume == 100);
    CHECK(odd.dark_board == appearance::Board{});
    // The engines' logos are off unless asked for, a profile of before included.
    CHECK_FALSE(appearance::Settings{}.engine_logos);
    CHECK_FALSE(odd.engine_logos);
    appearance::Settings logos;
    appearance::from_json(nlohmann::json::parse(R"({"engine_logos": true})"), logos);
    CHECK(logos.engine_logos);
    // The 3D board's values held in their ranges.
    appearance::Settings held;
    appearance::from_json(
        nlohmann::json::parse(R"({"board3d": {"exposure": 9, "varnish": -1, "gloss": 3, "samples": 100000}})"), held);
    CHECK(held.board3d.exposure == 2.0f);
    CHECK(held.board3d.varnish == 0.0f);
    CHECK(held.board3d.gloss == 1.0f);
    CHECK(held.board3d.samples == 4096);
    appearance::from_json(nlohmann::json::array(), odd);
    CHECK(odd == appearance::Settings{});

    // Kept in the profile, the folder relative to the program's in the portable mode.
    const auto root = freshDir("fcg-appearance-profile");
    {
        Storage storage(root);
        CHECK(storage.appearance() == appearance::Settings{});
        storage.setAppearance(s);
        CHECK(Storage(root).appearance() == s);

        portable::setBase(root);
        auto inside         = s;
        inside.piece_folder = (root / "pieces").string();
        storage.setAppearance(inside);
        std::ifstream in(root / "settings.json");
        const auto saved = nlohmann::json::parse(in);
        CHECK(saved["appearance"]["piece_folder"] == "pieces");
        CHECK(fs::path(storage.appearance().piece_folder) == (root / "pieces").lexically_normal());
        portable::setBase({});
    }
    fs::remove_all(root);
}

TEST_CASE("appearance: the piece sets built in, each drawable, in the order of their attribution") {
    const auto sets  = appearance::pieceSets();
    const auto files = assets::pieceSets();
    REQUIRE(sets.size() == files.size());
    CHECK(sets.size() >= 2);
    CHECK(std::string_view(sets.front().id) == "cburnett");
    for (std::size_t i = 0; i < sets.size(); ++i) {
        CAPTURE(sets[i].id);
        CHECK(files[i].id == sets[i].id);
        CHECK(appearance::pieceSet(sets[i].id) == &sets[i]);
        const auto svgs = appearance::builtInPieces(sets[i].id);
        for (const auto& svg : svgs) CHECK(appearance::drawableSvg(svg));
        CHECK_FALSE(rasterizePieces(24, svgs).empty());
    }
    // An unknown set is the default one.
    CHECK(appearance::builtInPieces("nothing") == appearance::builtInPieces("cburnett"));
    CHECK(appearance::builtInPieces("chessnut") != appearance::builtInPieces("cburnett"));
    CHECK_FALSE(appearance::drawableSvg("not an svg at all"));
    CHECK_FALSE(appearance::drawableSvg("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>"));
}

TEST_CASE("appearance: a folder of pieces, and what is wrong with it said by file") {
    const auto dir = freshDir("fcg-appearance-pieces");
    std::string error;

    // Missing: the default pieces, and why.
    auto svgs = appearance::folderPieces(dir / "nowhere", error);
    CHECK(error.find("nowhere") != std::string::npos);
    CHECK(svgs == appearance::builtInPieces("cburnett"));
    (void)appearance::folderPieces({}, error);
    CHECK_FALSE(error.empty());

    // Complete: its own pieces (another built-in set's, written out).
    const auto celtic = appearance::builtInPieces("celtic");
    for (std::size_t i = 0; i < 12; ++i) write(dir / (std::string(appearance::PIECE_FILES[i]) + ".svg"), celtic[i]);
    svgs = appearance::folderPieces(dir, error);
    CHECK(error.empty());
    CHECK(svgs == celtic);
    appearance::Settings s;
    s.pieces       = appearance::FOLDER;
    s.piece_folder = dir.string();
    CHECK(appearance::piecesFor(s, error) == celtic);
    CHECK(error.empty());

    // A file that is no picture, then one missing: named.
    write(dir / "bN.svg", "hello");
    svgs = appearance::folderPieces(dir, error);
    CHECK(error.find("bN.svg") != std::string::npos);
    CHECK(svgs == appearance::builtInPieces("cburnett"));
    fs::remove(dir / "wQ.svg");
    (void)appearance::folderPieces(dir, error);
    CHECK(error.find("wQ.svg") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("sound: the move and capture sounds, made the same each time, without clicks") {
    const auto move    = sound::synthesize(sound::Kind::Move);
    const auto capture = sound::synthesize(sound::Kind::Capture);
    CHECK(move.size() == static_cast<std::size_t>(sound::RATE / 10));
    CHECK(capture.size() > move.size());
    CHECK(move == sound::synthesize(sound::Kind::Move));
    for (const auto* samples : {&move, &capture}) {
        float peak = 0.0f;
        for (const float v : *samples) peak = std::max(peak, std::abs(v));
        CHECK(peak == doctest::Approx(0.9f).epsilon(0.001));
        CHECK(std::abs(samples->front()) < 0.01f);
        CHECK(std::abs(samples->back()) < 0.01f);
    }
    // Short: under a fifth of a second, as a move sound should be.
    CHECK(capture.size() < static_cast<std::size_t>(sound::RATE / 5));
    // Off, nothing is opened and nothing plays.
    sound::configure(false, 50);
    CHECK_FALSE(sound::ready());
    sound::play(sound::Kind::Move);
}

TEST_CASE("logos: pictures decoded and shrunk, their proportions kept; others refused with a reason") {
    std::string error;
    const auto png = logos::png(picture(400, 200));
    REQUIRE_FALSE(png.empty());
    auto image = logos::decode(png, false, 96, error);
    REQUIRE(image);
    CHECK(error.empty());
    CHECK(image->width == 96);
    CHECK(image->height == 48);
    // The middle red and opaque, a corner transparent.
    const auto at = [&](int x, int y) { return &image->rgba[(static_cast<std::size_t>(y) * image->width + x) * 4]; };
    CHECK(at(48, 24)[0] == 220);
    CHECK(at(48, 24)[3] == 255);
    CHECK(at(1, 1)[3] == 0);
    // Smaller than asked: as it is.
    image = logos::decode(logos::png(picture(20, 10)), false, 96, error);
    REQUIRE(image);
    CHECK(image->width == 20);

    image = logos::decode(SVG_SQUARE, true, 64, error);
    REQUIRE(image);
    CHECK(image->width == 64);
    CHECK(image->height == 32);
    CHECK(image->rgba[(16 * 64 + 32) * 4 + 3] == 255);

    CHECK_FALSE(logos::decode("not a picture", false, 64, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(logos::decode("<svg></svg>", true, 64, error));
    CHECK_FALSE(error.empty());
    // Too many pixels to be a logo, refused before it is decoded.
    CHECK_FALSE(logos::decode(logos::png(picture(logos::MAX_PIXELS + 1, 2)), false, 64, error));
    CHECK(error.find(std::to_string(logos::MAX_PIXELS)) != std::string::npos);
}

TEST_CASE("logos: a copy in the profile under a safe name, read back for the window and the web page") {
    CHECK(logos::supported("a/b/Logo.PNG"));
    CHECK(logos::supported("x.jpeg"));
    CHECK(logos::supported("x.svg"));
    CHECK_FALSE(logos::supported("x.gif"));
    CHECK_FALSE(logos::supported("x"));

    CHECK(logos::plainName("Stockfish-0011aabb.png"));
    CHECK_FALSE(logos::plainName(""));
    CHECK_FALSE(logos::plainName(".."));
    CHECK_FALSE(logos::plainName("../settings.json"));
    CHECK_FALSE(logos::plainName("a/b.png"));
    CHECK_FALSE(logos::plainName("a\\b.png"));
    CHECK_FALSE(logos::plainName("C:x.png"));

    const auto name = logos::fileName("Stock fish 17/x64 «dev»", "abc", "pic.JPEG");
    CHECK(name.rfind("Stock_fish_17_x64_dev-", 0) == 0);
    CHECK(name.size() == std::string("Stock_fish_17_x64_dev-").size() + 8 + 4);
    CHECK(name.ends_with(".jpg"));
    CHECK(logos::plainName(name));
    CHECK(logos::fileName("Stock fish 17/x64 «dev»", "abd", "pic.JPEG") != name);  // another picture, another name
    CHECK(logos::fileName("«»", "abc", "p.png").rfind("engine-", 0) == 0);

    const auto dir     = freshDir("fcg-logos");
    const auto folder  = dir / "profile" / "logos";
    const auto png     = dir / "mine.png";
    const auto svg     = dir / "mine.svg";
    const auto bad     = dir / "broken.png";
    const auto unknown = dir / "mine.gif";
    write(png, logos::png(picture(300, 300)));
    write(svg, SVG_SQUARE);
    write(bad, "not a png");
    write(unknown, "GIF89a");

    std::string error;
    const auto copied = logos::import(png, folder, "Alpha", error);
    CHECK(error.empty());
    REQUIRE_FALSE(copied.empty());
    CHECK(fs::exists(folder / copied));
    // The original can go: the copy stays.
    fs::remove(png);
    auto image = logos::load(folder / copied, logos::TEXTURE_SIDE, error);
    REQUIRE(image);
    CHECK(image->width == logos::TEXTURE_SIDE);

    CHECK_FALSE(logos::import(svg, folder, "Beta", error).empty());
    CHECK(logos::import(bad, folder, "Gamma", error).empty());
    CHECK(error.find("broken.png") != std::string::npos);
    CHECK(logos::import(unknown, folder, "Gamma", error).empty());
    CHECK(error.find("mine.gif") != std::string::npos);
    CHECK(logos::import(dir / "gone.png", folder, "Gamma", error).empty());
    CHECK_FALSE(error.empty());
    // A file too large is refused without being read.
    write(dir / "huge.png", std::string(logos::MAX_FILE + 1, '\0'));
    CHECK(logos::import(dir / "huge.png", folder, "Gamma", error).empty());
    CHECK(error.find("2 MB") != std::string::npos);

    // For the web page: PNG files at an address of their own; a name that is not a plain file of the folder, or a
    // file gone, is left out.
    std::vector<EngineEntry> engines(4);
    engines[0].name = "Alpha";
    engines[0].logo = copied;
    engines[1].name = "Beta";
    engines[1].logo = "../../etc/passwd";
    engines[2].name = "Gamma";
    engines[2].logo = "gone.png";
    engines[3].name = "Delta";
    const auto web  = logos::webLogos(engines, folder);
    REQUIRE(web.size() == 1);
    CHECK(web[0].engine == "Alpha");
    CHECK(webLogoPath(web[0].path));
    image = logos::decode(web[0].png, false, 1000, error);
    REQUIRE(image);
    CHECK(image->width == logos::WEB_SIDE);
    fs::remove_all(dir);
}

TEST_CASE("logos: an engine keeps its logo's name in engines.json, only when it has one") {
    EngineEntry e;
    e.name           = "Alpha";
    nlohmann::json j = e;
    CHECK_FALSE(j.contains("logo"));
    e.logo = "Alpha-00112233.png";
    j      = e;
    CHECK(j["logo"] == "Alpha-00112233.png");
    CHECK(j.get<EngineEntry>().logo == "Alpha-00112233.png");
}

TEST_CASE("logos: the web page asks for them at their own addresses, nothing else") {
    CHECK(webLogoPath("logo/0123abcd.png"));
    CHECK(webLogoPath("logo/" + std::string(64, 'f') + ".png"));
    CHECK_FALSE(webLogoPath("logo/0123abc.png"));  // too short
    CHECK_FALSE(webLogoPath("logo/" + std::string(65, 'f') + ".png"));
    CHECK_FALSE(webLogoPath("logo/0123ABCD.png"));
    CHECK_FALSE(webLogoPath("logo/../../0123abcd.png"));
    CHECK_FALSE(webLogoPath("logo/0123abcd.svg"));
    CHECK_FALSE(webLogoPath("logos/0123abcd.png"));

    const auto route = [](const std::string& path) {
        const std::string request = "GET " + path + " HTTP/1.1\r\nHost: x\r\n\r\n";
        return readWebRequest(request).route;
    };
    CHECK(route("/logo/0123abcd.png") == WebRequest::Route::Logo);
    CHECK(route("/logo/0123abcd.png?x=1") == WebRequest::Route::Logo);
    CHECK(route("/logo/../settings.json") == WebRequest::Route::NotFound);
    CHECK(route("/logo/%2e%2e/0123abcd.png") == WebRequest::Route::NotFound);
    CHECK(route("/logo/") == WebRequest::Route::NotFound);
    const std::string request = "GET /logo/00aa11bb.png HTTP/1.1\r\n\r\n";
    CHECK(readWebRequest(request).logo == "logo/00aa11bb.png");
}

TEST_CASE("logos: state.json has those of the tournament's engines, and the server serves them from memory") {
    BroadcastSnapshot s;
    s.name = "Logos";
    fcg::tour::TournamentSpec spec;
    for (int i = 0; i < 2; ++i) {
        fcg::tour::Participant p;
        p.id          = i;
        p.engine.name = i == 0 ? "Alpha" : "Beta";
        spec.participants.push_back(p);
    }
    s.table          = fcg::tour::computeTable(spec, {});
    s.logos["Alpha"] = "logo/0123abcd.png";
    s.logos["Other"] = "logo/89abcdef.png";  // in the library, not in the tournament
    s.logos["Beta"]  = "../secret.png";      // not an address of the server's
    const auto state = nlohmann::json::parse(snapshotJson(s));
    CHECK(state["logos"] == nlohmann::json({{"Alpha", "logo/0123abcd.png"}}));
    CHECK(webPage().find("function logo(") != std::string::npos);
    CHECK(webPage().size() < 80000);

    WebServer server;
    const uint16_t port = fcg::test::freePort();
    REQUIRE(server.start(port).empty());
    server.publish(s);
    const auto png = logos::png(picture(8, 8));
    server.setLogos({{"Alpha", "logo/0123abcd.png", png}, {"Beta", "../x.png", png}});
    const auto get = [&](const std::string& path) {
        const auto fd = net::connectTo(0x7F000001, port);
        if (!fd) return std::string();
        net::TcpConnection c(*fd);
        c.sendAll("GET " + path + " HTTP/1.0\r\nHost: localhost\r\n\r\n");
        return c.readUntil("\n\n\n\n", 1 << 20, 5000);
    };
    const auto answer = get("/logo/0123abcd.png");
    CHECK(answer.rfind("HTTP/1.0 200", 0) == 0);
    CHECK(answer.find("Content-Type: image/png\r\n") != std::string::npos);
    CHECK(answer.find("Content-Security-Policy: default-src 'none'") != std::string::npos);
    CHECK(answer.size() > png.size());
    CHECK(answer.compare(answer.size() - png.size(), png.size(), png) == 0);
    CHECK(get("/logo/89abcdef.png").rfind("HTTP/1.0 404", 0) == 0);
    server.setLogos({});
    CHECK(get("/logo/0123abcd.png").rfind("HTTP/1.0 404", 0) == 0);
    server.stop();
}
