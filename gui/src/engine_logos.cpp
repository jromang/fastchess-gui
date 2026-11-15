#include "engine_logos.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

#include <nanosvg.h>  // compiled in board_view.cpp
#include <nanosvgrast.h>
#include <stb_image_write.h>

// PNG and JPEG only, from memory only: the pictures of the logos (and the tests' reading back of PNG and zlib data).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 16384
#include <stb_image.h>

#include <core/crc32.hpp>

#include "i18n.hpp"

namespace fcg::logos {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

uint32_t checksum(std::string_view bytes) {
    return fastchess::crc::finalize_crc32(
        fastchess::crc::incremental_crc32(fastchess::crc::initial_crc32(), std::string(bytes)));
}

// Each pixel of the smaller image the average of those it covers, weighted by their opacity, so that the colour of
// transparent pixels does not bleed into the edges.
Image shrink(Image in, int side) {
    if (in.width <= side && in.height <= side) return in;
    const double scale = static_cast<double>(side) / std::max(in.width, in.height);
    Image out;
    out.width  = std::max(1, static_cast<int>(std::lround(in.width * scale)));
    out.height = std::max(1, static_cast<int>(std::lround(in.height * scale)));
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    for (int y = 0; y < out.height; ++y) {
        const int y0 = y * in.height / out.height, y1 = std::max(y0 + 1, (y + 1) * in.height / out.height);
        for (int x = 0; x < out.width; ++x) {
            const int x0 = x * in.width / out.width, x1 = std::max(x0 + 1, (x + 1) * in.width / out.width);
            double r = 0, g = 0, b = 0, a = 0;
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    const unsigned char* p = &in.rgba[(static_cast<std::size_t>(sy) * in.width + sx) * 4];
                    r += p[0] * static_cast<double>(p[3]);
                    g += p[1] * static_cast<double>(p[3]);
                    b += p[2] * static_cast<double>(p[3]);
                    a += p[3];
                }
            }
            unsigned char* o = &out.rgba[(static_cast<std::size_t>(y) * out.width + x) * 4];
            if (a > 0) {
                o[0] = static_cast<unsigned char>(std::clamp(std::lround(r / a), 0L, 255L));
                o[1] = static_cast<unsigned char>(std::clamp(std::lround(g / a), 0L, 255L));
                o[2] = static_cast<unsigned char>(std::clamp(std::lround(b / a), 0L, 255L));
                o[3] = static_cast<unsigned char>(std::lround(a / ((y1 - y0) * (x1 - x0))));
            }
        }
    }
    return out;
}

std::optional<Image> decodeSvg(std::string_view bytes, int side, std::string& error) {
    std::string text(bytes);  // nsvgParse modifies its input
    NSVGimage* svg = nsvgParse(text.data(), "px", 96.0f);
    if (!svg || svg->width <= 0 || svg->height <= 0 || !svg->shapes) {
        if (svg) nsvgDelete(svg);
        error = TR("It is not an SVG picture the program can draw.");
        return std::nullopt;
    }
    const float scale = static_cast<float>(side) / std::max(svg->width, svg->height);
    Image out;
    out.width  = std::max(1, static_cast<int>(std::lround(svg->width * scale)));
    out.height = std::max(1, static_cast<int>(std::lround(svg->height * scale)));
    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (rast) {
        nsvgRasterize(rast, svg, 0, 0, scale, out.rgba.data(), out.width, out.height, out.width * 4);
        nsvgDeleteRasterizer(rast);
    }
    nsvgDelete(svg);
    if (!rast) {
        error = TR("Not enough memory to draw it.");
        return std::nullopt;
    }
    return out;
}

}  // namespace

bool supported(const fs::path& file) {
    const auto ext = lower(file.extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".svg";
}

std::optional<Image> decode(std::string_view bytes, bool svg, int side, std::string& error) {
    error.clear();
    if (svg) return decodeSvg(bytes, side, error);

    const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
    const int length = static_cast<int>(std::min<std::size_t>(bytes.size(), MAX_FILE));
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(data, length, &width, &height, &channels)) {
        error = TR("It is not a PNG or JPEG picture the program can read.");
        return std::nullopt;
    }
    if (width > MAX_PIXELS || height > MAX_PIXELS) {
        error = trf("It is {}×{} pixels: at most {} each way.", width, height, MAX_PIXELS);
        return std::nullopt;
    }
    stbi_uc* pixels = stbi_load_from_memory(data, length, &width, &height, &channels, 4);
    if (!pixels) {
        error = trf("It cannot be read: {}.", stbi_failure_reason() ? stbi_failure_reason() : "?");
        return std::nullopt;
    }
    Image image;
    image.width  = width;
    image.height = height;
    image.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return shrink(std::move(image), side);
}

std::optional<Image> load(const fs::path& file, int side, std::string& error) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) {
        error = trf("{} is not there.", file.string());
        return std::nullopt;
    }
    if (size > MAX_FILE) {
        error = trf("{} is larger than 2 MB.", file.string());
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in && !in.eof()) {
        error = trf("{} cannot be read.", file.string());
        return std::nullopt;
    }
    return decode(bytes, lower(file.extension().string()) == ".svg", side, error);
}

bool plainName(std::string_view name) {
    if (name.empty() || name == "." || name == ".." || name.size() > 200) return false;
    return name.find_first_of("/\\:") == std::string_view::npos && name.find('\0') == std::string_view::npos;
}

std::string fileName(const std::string& engine, std::string_view bytes, const fs::path& source) {
    std::string base;
    for (const char c : engine) {
        const auto u  = static_cast<unsigned char>(c);
        const bool ok = std::isalnum(u) || c == '-' || c == '_' || c == '.';
        if (ok || (!base.empty() && base.back() != '_')) base += ok ? c : '_';
        if (base.size() >= 40) break;
    }
    while (!base.empty() && (base.back() == '_' || base.back() == '.')) base.pop_back();
    if (base.empty()) base = "engine";
    auto ext = lower(source.extension().string());
    if (ext == ".jpeg") ext = ".jpg";
    return fmt::format("{}-{:08x}{}", base, checksum(bytes), ext);
}

std::string import(const fs::path& source, const fs::path& folder, const std::string& engine, std::string& error) {
    error.clear();
    if (!supported(source)) {
        error = trf("{}: a logo is a PNG, JPEG or SVG picture.", source.filename().string());
        return {};
    }
    std::error_code ec;
    const auto size = fs::file_size(source, ec);
    if (ec || size > MAX_FILE) {
        error = ec ? trf("{} is not there.", source.string()) : trf("{} is larger than 2 MB.", source.string());
        return {};
    }
    std::ifstream in(source, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in && !in.eof()) {
        error = trf("{} cannot be read.", source.string());
        return {};
    }
    std::string why;
    if (!decode(bytes, lower(source.extension().string()) == ".svg", 16, why)) {
        error = fmt::format("{}: {}", source.filename().string(), why);
        return {};
    }
    const auto name = fileName(engine, bytes, source);
    fs::create_directories(folder, ec);
    std::ofstream out(folder / name, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        error = trf("Cannot write {}.", (folder / name).string());
        return {};
    }
    return name;
}

std::string png(const Image& image) {
    std::string out;
    stbi_write_png_to_func([](void* context, void* data,
                              int n) { static_cast<std::string*>(context)->append(static_cast<char*>(data), n); },
                           &out, image.width, image.height, 4, image.rgba.data(), image.width * 4);
    return out;
}

std::vector<WebLogo> webLogos(const std::vector<EngineEntry>& engines, const fs::path& folder) {
    std::vector<WebLogo> out;
    for (const auto& e : engines) {
        if (!plainName(e.logo)) continue;
        std::string error;
        const auto image = load(folder / e.logo, WEB_SIDE, error);
        if (!image) continue;
        auto file = png(*image);
        if (file.empty()) continue;
        out.push_back({e.name, fmt::format("logo/{:08x}.png", checksum(file)), std::move(file)});
    }
    return out;
}

}  // namespace fcg::logos
