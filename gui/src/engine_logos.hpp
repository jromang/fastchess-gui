#pragma once

// The engines' logos: a picture per engine of the library (PNG, JPEG or SVG), chosen on its card and copied into the
// profile's logos folder (Storage::logosDir), so that the original can move. Shown beside the engine's name in Live
// and on the big screen when the appearance asks for them (off by default), and offered to the broadcast web page as
// small PNG files the program writes itself.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "broadcast.hpp"
#include "model.hpp"

namespace fcg::logos {

inline constexpr std::uintmax_t MAX_FILE = 2 * 1024 * 1024;  // larger pictures are refused
inline constexpr int MAX_PIXELS          = 4096;             // and so are those wider or higher than this
inline constexpr int TEXTURE_SIDE        = 192;              // the window's: shrunk to fit this square
inline constexpr int WEB_SIDE            = 96;               // the web page's

struct Image {
    int width  = 0;
    int height = 0;
    std::vector<unsigned char> rgba;  // rows from the top
};

// Whether the file's extension is that of a picture the program reads: .png, .jpg, .jpeg or .svg, any case.
[[nodiscard]] bool supported(const std::filesystem::path& file);

// A picture's bytes decoded and shrunk to fit `side` x `side`, its proportions kept (an SVG drawn at that size).
// nullopt, with the reason in `error`, when it is no picture the program reads.
[[nodiscard]] std::optional<Image> decode(std::string_view bytes, bool svg, int side, std::string& error);
// The same from a file of at most MAX_FILE bytes.
[[nodiscard]] std::optional<Image> load(const std::filesystem::path& file, int side, std::string& error);

// Whether `name` can be a file of the logos folder as an engine keeps it: a plain file name, no folder in it. A
// hand-edited engines.json can name nothing else there.
[[nodiscard]] bool plainName(std::string_view name);

// The name the logo of `engine` is copied as: the engine's name made safe for a file name, a checksum of the
// picture (a new picture, a new name: a browser never shows the old one), the extension in lower case.
[[nodiscard]] std::string fileName(const std::string& engine, std::string_view bytes,
                                   const std::filesystem::path& source);

// Copies `source` into `folder` (made if needed) as the logo of `engine`, once it reads as a picture. Returns the
// copy's file name, or an empty string with the reason in `error`.
[[nodiscard]] std::string import(const std::filesystem::path& source, const std::filesystem::path& folder,
                                 const std::string& engine, std::string& error);

// The image as a PNG file.
[[nodiscard]] std::string png(const Image& image);

// The logos of the engines that have one, for the web page: each read from `folder`, shrunk to WEB_SIDE, as PNG.
// Those that cannot be read are left out.
[[nodiscard]] std::vector<WebLogo> webLogos(const std::vector<EngineEntry>& engines,
                                            const std::filesystem::path& folder);

}  // namespace fcg::logos
