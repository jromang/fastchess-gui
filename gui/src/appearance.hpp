#pragma once

// How the boards look and sound: the colours of their squares for each theme, the pieces (a set built into the
// program or the user's folder of SVG files), the move sounds. Kept in settings.json (Storage::appearance()). Every
// board drawBoard() draws takes its colours from boardColours(), and the program's one piece atlas is made from
// piecesFor(): a board added anywhere follows both without more.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <imgui.h>

#include <json.hpp>

#include "ui.hpp"

namespace fcg::appearance {

struct BoardColours {
    ImU32 light     = IM_COL32(222, 227, 230, 255);
    ImU32 dark      = IM_COL32(140, 162, 173, 255);
    ImU32 highlight = IM_COL32(155, 199, 0, 105);  // over the two squares of the last move, with its transparency

    bool operator==(const BoardColours&) const = default;
};

struct BoardPreset {
    const char* id;    // as settings.json keeps it
    const char* name;  // N_(): translated where shown
    BoardColours colours;
};
// The presets, the default (the program's colours before there were others) first.
[[nodiscard]] std::span<const BoardPreset> boardPresets();
[[nodiscard]] const BoardPreset* boardPreset(std::string_view id);

inline constexpr const char* CUSTOM = "custom";

// The board of one theme: a preset, or CUSTOM and its own colours.
struct Board {
    std::string preset = "slate";
    BoardColours custom;  // used when preset is CUSTOM; the colours the user started from otherwise

    bool operator==(const Board&) const = default;
};
// Its colours: the preset's (the default one for an unknown preset), or its own.
[[nodiscard]] BoardColours colours(const Board& board);

struct PieceSetInfo {
    const char* id;  // its folder in gui/assets/pieces
    const char* name;
    const char* author;
    const char* license;
};
// The sets built in, the default first, in the order of assets::pieceSets().
[[nodiscard]] std::span<const PieceSetInfo> pieceSets();
[[nodiscard]] const PieceSetInfo* pieceSet(std::string_view id);

inline constexpr const char* FOLDER = "folder";  // the pieces of the user's folder

// The files a folder of pieces holds, in the atlas' order: white K Q R B N P, then black K Q R B N P, each
// "<name>.svg".
inline constexpr std::array<const char*, 12> PIECE_FILES = {"wK", "wQ", "wR", "wB", "wN", "wP",
                                                            "bK", "bQ", "bR", "bB", "bN", "bP"};

// The board of the Board page drawn in 3D (board_3d.hpp), how it is lit, its finish, how long its image refines.
struct Board3d {
    bool on              = false;
    std::string lighting = "studio";  // a board3d::studios() id
    float exposure       = 0.0f;      // stops over or under the lighting's own, -2 to 2
    float varnish        = 1.0f;      // 0: waxed, matt; 1: a full coat
    float gloss          = 0.58f;     // 0: satin; 1: a mirror
    int samples          = 512;       // the image's samples before it rests, 32 to 4096

    bool operator==(const Board3d&) const = default;
};

struct Settings {
    Board light_board;                // with the light theme
    Board dark_board;                 // with the dark theme
    std::string pieces = "cburnett";  // a built-in set's id, or FOLDER
    std::string piece_folder;         // with FOLDER: 12 SVG files named wK.svg ... bP.svg
    bool sounds = false;              // a sound for each move of the game followed
    int volume  = 70;                 // percent
    bool figurines = true;               // the pieces of the set in the notation, instead of letters
    bool engine_logos = false;              // each engine's logo beside its name in Live and on the big screen
    Board3d board3d;

    [[nodiscard]] const Board& board(ui::Theme theme) const {
        return theme == ui::Theme::Dark ? dark_board : light_board;
    }
    [[nodiscard]] Board& board(ui::Theme theme) { return theme == ui::Theme::Dark ? dark_board : light_board; }

    bool operator==(const Settings&) const = default;
};

// settings.json's "appearance". A value missing or of another type keeps its default; piece_folder is as written
// (Storage makes it absolute again in the portable mode).
void to_json(nlohmann::json& j, const Settings& s);
void from_json(const nlohmann::json& j, Settings& s);

// "#rrggbbaa", and back; "#rrggbb" reads as opaque. nullopt when it is not one.
[[nodiscard]] std::string hexColour(ImU32 colour);
[[nodiscard]] std::optional<ImU32> parseHexColour(std::string_view text);

// The settings in force, which the boards and the Settings page read.
[[nodiscard]] const Settings& current();
void set(const Settings& settings);

// The colours the boards are drawn in: those of the current theme (ui::currentTheme()), or of `theme`.
[[nodiscard]] BoardColours boardColours();
[[nodiscard]] BoardColours boardColours(ui::Theme theme);

// The twelve pieces' SVG text, in the order of PIECE_FILES.
using PieceSvgs = std::array<std::string, 12>;

// The pieces of a built-in set; the default set for an id that is none.
[[nodiscard]] PieceSvgs builtInPieces(std::string_view id);

// Whether SVG text is a picture nanosvg can draw: it parses, has a size and something to draw.
[[nodiscard]] bool drawableSvg(std::string svg);

// The largest piece file read from a folder: a piece is a few kilobytes.
inline constexpr std::uintmax_t MAX_PIECE_FILE = 1024 * 1024;

// The pieces of a folder: its 12 files read and checked. When one is missing, too large or not a picture, `error`
// says which and why, and the default set is returned.
[[nodiscard]] PieceSvgs folderPieces(const std::filesystem::path& folder, std::string& error);

// The pieces `settings` asks for, as folderPieces() for a folder; `error` empty when they are those asked for.
[[nodiscard]] PieceSvgs piecesFor(const Settings& settings, std::string& error);

}  // namespace fcg::appearance
