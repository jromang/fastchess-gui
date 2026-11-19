#include "appearance.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

#include <nanosvg.h>  // compiled in board_view.cpp

#include "i18n.hpp"
#include "piece_sets.hpp"

namespace fcg::appearance {

namespace fs = std::filesystem;

namespace {

// Lichess' blue board (the program's only one before), its brown one, a deeper blue, the green of many sites, a
// neutral grey. The last move in the green lichess uses, but on the last three, where yellow or blue shows better.
constexpr BoardPreset PRESETS[] = {
    {"slate",
     N_("Blue grey"),
     {IM_COL32(222, 227, 230, 255), IM_COL32(140, 162, 173, 255), IM_COL32(155, 199, 0, 105)}},
    {"brown", N_("Brown"), {IM_COL32(240, 217, 181, 255), IM_COL32(181, 136, 99, 255), IM_COL32(155, 199, 0, 105)}},
    {"blue", N_("Blue"), {IM_COL32(232, 238, 245, 255), IM_COL32(120, 150, 189, 255), IM_COL32(246, 212, 60, 130)}},
    {"green", N_("Green"), {IM_COL32(238, 238, 210, 255), IM_COL32(118, 150, 86, 255), IM_COL32(255, 255, 51, 120)}},
    {"grey", N_("Grey"), {IM_COL32(226, 226, 226, 255), IM_COL32(158, 158, 158, 255), IM_COL32(95, 160, 235, 110)}},
};

// In the order of the sets embedded (CMakeLists.txt, FCG_PIECE_SETS); a test checks that they agree.
constexpr PieceSetInfo SETS[] = {
    {"cburnett", "Cburnett", "Colin M.L. Burnett", "BSD 3-clause"},
    {"chessnut", "Chessnut", "Alexis Luengas", "Apache 2.0"},
    {"celtic", "Celtic", "Maurizio Monge", "MIT"},
    {"fantasy", "Fantasy", "Maurizio Monge", "MIT"},
};

Settings g_settings;

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

template <typename T>
T value(const nlohmann::json& j, const char* key, T fallback) {
    try {
        return j.value(key, fallback);
    } catch (const nlohmann::json::exception&) {
        return fallback;
    }
}

nlohmann::json boardJson(const Board& b) {
    return {{"preset", b.preset},
            {"light", hexColour(b.custom.light)},
            {"dark", hexColour(b.custom.dark)},
            {"highlight", hexColour(b.custom.highlight)}};
}

Board boardFrom(const nlohmann::json& j) {
    Board b;
    if (!j.is_object()) return b;
    b.preset          = value(j, "preset", b.preset);
    const auto colour = [&](const char* key, ImU32& to) {
        if (const auto c = parseHexColour(value(j, key, std::string{}))) to = *c;
    };
    colour("light", b.custom.light);
    colour("dark", b.custom.dark);
    colour("highlight", b.custom.highlight);
    return b;
}

}  // namespace

std::span<const BoardPreset> boardPresets() { return PRESETS; }

const BoardPreset* boardPreset(std::string_view id) {
    for (const auto& p : PRESETS) {
        if (id == p.id) return &p;
    }
    return nullptr;
}

BoardColours colours(const Board& board) {
    if (board.preset == CUSTOM) return board.custom;
    const auto* preset = boardPreset(board.preset);
    return preset ? preset->colours : PRESETS[0].colours;
}

std::span<const PieceSetInfo> pieceSets() { return SETS; }

const PieceSetInfo* pieceSet(std::string_view id) {
    for (const auto& s : SETS) {
        if (id == s.id) return &s;
    }
    return nullptr;
}

void to_json(nlohmann::json& j, const Settings& s) {
    j = {{"board_light", boardJson(s.light_board)},
         {"board_dark", boardJson(s.dark_board)},
         {"pieces", s.pieces},
         {"piece_folder", s.piece_folder},
         {"sounds", s.sounds},
         {"volume", s.volume},
         {"figurines", s.figurines},
         {"engine_logos", s.engine_logos},
         {"board3d",
          {{"on", s.board3d.on},
           {"lighting", s.board3d.lighting},
           {"exposure", s.board3d.exposure},
           {"varnish", s.board3d.varnish},
           {"gloss", s.board3d.gloss},
           {"samples", s.board3d.samples}}}};
}

void from_json(const nlohmann::json& j, Settings& s) {
    s = Settings{};
    if (!j.is_object()) return;
    if (j.contains("board_light")) s.light_board = boardFrom(j["board_light"]);
    if (j.contains("board_dark")) s.dark_board = boardFrom(j["board_dark"]);
    s.pieces       = value(j, "pieces", s.pieces);
    s.piece_folder = value(j, "piece_folder", s.piece_folder);
    s.sounds       = value(j, "sounds", s.sounds);
    s.volume       = std::clamp(value(j, "volume", s.volume), 0, 100);
    s.figurines    = value(j, "figurines", s.figurines);
    s.engine_logos = value(j, "engine_logos", s.engine_logos);
    if (j.contains("board3d") && j["board3d"].is_object()) {
        const auto& b      = j["board3d"];
        s.board3d.on       = value(b, "on", s.board3d.on);
        s.board3d.lighting = value(b, "lighting", s.board3d.lighting);
        s.board3d.exposure = std::clamp(value(b, "exposure", s.board3d.exposure), -2.0f, 2.0f);
        s.board3d.varnish  = std::clamp(value(b, "varnish", s.board3d.varnish), 0.0f, 1.0f);
        s.board3d.gloss    = std::clamp(value(b, "gloss", s.board3d.gloss), 0.0f, 1.0f);
        s.board3d.samples  = std::clamp(value(b, "samples", s.board3d.samples), 32, 4096);
    }
}

std::string hexColour(ImU32 colour) {
    const auto channel = [&](int shift) { return static_cast<unsigned>((colour >> shift) & 0xFF); };
    return fmt::format("#{:02x}{:02x}{:02x}{:02x}", channel(IM_COL32_R_SHIFT), channel(IM_COL32_G_SHIFT),
                       channel(IM_COL32_B_SHIFT), channel(IM_COL32_A_SHIFT));
}

std::optional<ImU32> parseHexColour(std::string_view text) {
    if ((text.size() != 7 && text.size() != 9) || text[0] != '#') return std::nullopt;
    int bytes[4] = {0, 0, 0, 255};
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        const int hi = hexDigit(text[i + 1]), lo = hexDigit(text[i + 2]);
        if (hi < 0 || lo < 0) return std::nullopt;
        bytes[i / 2] = hi * 16 + lo;
    }
    return IM_COL32(bytes[0], bytes[1], bytes[2], bytes[3]);
}

const Settings& current() { return g_settings; }

void set(const Settings& settings) { g_settings = settings; }

BoardColours boardColours() { return boardColours(ui::currentTheme()); }

BoardColours boardColours(ui::Theme theme) { return colours(g_settings.board(theme)); }

PieceSvgs builtInPieces(std::string_view id) {
    const auto sets = assets::pieceSets();
    const auto it   = std::find_if(sets.begin(), sets.end(), [&](const auto& s) { return s.id == id; });
    const auto& set = it != sets.end() ? *it : sets.front();
    PieceSvgs out;
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = std::string(set.svgs[i]);
    return out;
}

bool drawableSvg(std::string svg) {
    // nsvgParse modifies its input. Text that is no SVG at all parses to an image with no size and nothing in it.
    NSVGimage* image = nsvgParse(svg.data(), "px", 96.0f);
    if (!image) return false;
    const bool ok = image->width > 0 && image->height > 0 && image->shapes != nullptr;
    nsvgDelete(image);
    return ok;
}

PieceSvgs folderPieces(const fs::path& folder, std::string& error) {
    error.clear();
    PieceSvgs out;
    std::error_code ec;
    if (folder.empty()) {
        error = TR("No folder of pieces is chosen.");
    } else if (!fs::is_directory(folder, ec)) {
        error = trf("The folder {} is not there.", folder.string());
    }
    for (std::size_t i = 0; i < out.size() && error.empty(); ++i) {
        const auto file = folder / (std::string(PIECE_FILES[i]) + ".svg");
        const auto size = fs::file_size(file, ec);
        if (ec) {
            error = trf("{} is missing: the folder needs the 12 files wK.svg, wQ.svg … bN.svg, bP.svg.", file.string());
            break;
        }
        if (size > MAX_PIECE_FILE) {
            error = trf("{} is larger than 1 MB: is it a piece?", file.string());
            break;
        }
        std::ifstream in(file, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!in && !in.eof()) {
            error = trf("{} cannot be read.", file.string());
            break;
        }
        if (!drawableSvg(text)) {
            error = trf("{} is not an SVG picture the program can draw.", file.string());
            break;
        }
        out[i] = std::move(text);
    }
    if (!error.empty()) return builtInPieces(SETS[0].id);
    return out;
}

PieceSvgs piecesFor(const Settings& settings, std::string& error) {
    error.clear();
    if (settings.pieces == FOLDER) return folderPieces(fs::path(settings.piece_folder), error);
    return builtInPieces(settings.pieces);
}

}  // namespace fcg::appearance
