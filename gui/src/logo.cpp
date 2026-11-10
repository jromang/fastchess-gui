#include "logo.hpp"

#include <algorithm>
#include <cmath>

#include <stb_image_write.h>

namespace fcg::logo {

namespace {

// The pawn on a 24 x 24 grid; on the tile it takes 24/36 of the width, centred.
constexpr float GRID     = 24.0f;
constexpr float GLYPH    = 24.0f / 36.0f;
constexpr float ROUNDING = 10.0f / 36.0f;

struct Rect {
    float x0, y0, x1, y1, r;
};
constexpr Rect CROWN   = {11.0f, 1.2f, 13.0f, 2.9f, 0.5f};  // the stopwatch's button
constexpr Rect COLLAR  = {8.4f, 12.4f, 15.6f, 14.0f, 0.8f};
constexpr Rect BASE    = {6.0f, 19.6f, 18.0f, 22.0f, 1.0f};
constexpr float HEAD_X = 12.0f, HEAD_Y = 7.6f, HEAD_R = 4.2f;
constexpr ImVec2 BODY[4] = {{9.8f, 14.4f}, {14.2f, 14.4f}, {15.9f, 19.0f}, {8.1f, 19.0f}};
// Cut out of the head: the hand, and its pivot.
constexpr float HAND_X = 14.3f, HAND_Y = 5.6f, HAND_W = 0.75f, PIVOT_R = 0.9f;

bool inRect(const Rect& b, float x, float y) {
    if (x < b.x0 || x > b.x1 || y < b.y0 || y > b.y1) return false;
    const float cx = std::clamp(x, b.x0 + b.r, b.x1 - b.r);
    const float cy = std::clamp(y, b.y0 + b.r, b.y1 - b.r);
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= b.r * b.r;
}

bool inBody(float x, float y) {
    if (y < BODY[0].y || y > BODY[2].y) return false;
    const float t = (y - BODY[0].y) / (BODY[2].y - BODY[0].y);
    return x >= BODY[0].x + t * (BODY[3].x - BODY[0].x) && x <= BODY[1].x + t * (BODY[2].x - BODY[1].x);
}

float distanceToHand(float x, float y) {
    const float dx = HAND_X - HEAD_X, dy = HAND_Y - HEAD_Y;
    const float t = std::clamp(((x - HEAD_X) * dx + (y - HEAD_Y) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
    return std::hypot(x - (HEAD_X + t * dx), y - (HEAD_Y + t * dy));
}

// (x, y) on the grid.
bool inGlyph(float x, float y) {
    const float head = std::hypot(x - HEAD_X, y - HEAD_Y);
    if (head <= HEAD_R) return head > PIVOT_R && distanceToHand(x, y) > HAND_W;
    return inRect(CROWN, x, y) || inRect(COLLAR, x, y) || inBody(x, y) || inRect(BASE, x, y);
}

void append(std::vector<uint8_t>& out, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

}  // namespace

void draw(ImDrawList* dl, ImVec2 min, float size, ImU32 tile, ImU32 glyph) {
    dl->AddRectFilled(min, ImVec2(min.x + size, min.y + size), tile, size * ROUNDING);
    const float k = size * GLYPH / GRID;
    const ImVec2 o(min.x + size * (1 - GLYPH) / 2, min.y + size * (1 - GLYPH) / 2);
    const auto at   = [&](float x, float y) { return ImVec2(o.x + x * k, o.y + y * k); };
    const auto rect = [&](const Rect& b, ImU32 c) { dl->AddRectFilled(at(b.x0, b.y0), at(b.x1, b.y1), c, b.r * k); };

    rect(CROWN, glyph);
    dl->AddCircleFilled(at(HEAD_X, HEAD_Y), HEAD_R * k, glyph, 48);
    rect(COLLAR, glyph);
    const ImVec2 body[4] = {at(BODY[0].x, BODY[0].y), at(BODY[1].x, BODY[1].y), at(BODY[2].x, BODY[2].y),
                            at(BODY[3].x, BODY[3].y)};
    dl->AddConvexPolyFilled(body, 4, glyph);
    rect(BASE, glyph);
    // The hand and its pivot, in the tile's colour, with round ends.
    dl->AddLine(at(HEAD_X, HEAD_Y), at(HAND_X, HAND_Y), tile, 2 * HAND_W * k);
    dl->AddCircleFilled(at(HAND_X, HAND_Y), HAND_W * k, tile, 12);
    dl->AddCircleFilled(at(HEAD_X, HEAD_Y), PIVOT_R * k, tile, 12);
}

std::vector<uint8_t> icon(int size) {
    constexpr int SAMPLES   = 4;  // per side, per pixel
    constexpr float TILE[3] = {47, 111, 222};
    const float radius      = size * ROUNDING;
    const float k           = size * GLYPH / GRID;
    const float offset      = size * (1 - GLYPH) / 2;
    const Rect tile         = {0, 0, static_cast<float>(size), static_cast<float>(size), radius};

    std::vector<uint8_t> out(static_cast<std::size_t>(size) * size * 4);
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            float covered = 0, white = 0;
            for (int sy = 0; sy < SAMPLES; ++sy) {
                for (int sx = 0; sx < SAMPLES; ++sx) {
                    const float x = px + (sx + 0.5f) / SAMPLES, y = py + (sy + 0.5f) / SAMPLES;
                    if (!inRect(tile, x, y)) continue;
                    covered += 1;
                    if (inGlyph((x - offset) / k, (y - offset) / k)) white += 1;
                }
            }
            auto* p = &out[(static_cast<std::size_t>(py) * size + px) * 4];
            if (covered == 0) continue;  // transparent
            const float w = white / covered;
            for (int c = 0; c < 3; ++c) p[c] = static_cast<uint8_t>(std::lround(TILE[c] + w * (255 - TILE[c])));
            p[3] = static_cast<uint8_t>(std::lround(255 * covered / (SAMPLES * SAMPLES)));
        }
    }
    return out;
}

std::vector<uint8_t> icoFile() {
    constexpr int SIZES[] = {16, 20, 24, 32, 40, 48, 64, 256};
    constexpr int COUNT   = static_cast<int>(sizeof(SIZES) / sizeof(SIZES[0]));
    std::vector<std::vector<uint8_t>> images;
    for (const int size : SIZES) {
        const auto pixels = icon(size);
        std::vector<uint8_t> png;
        stbi_write_png_to_func(
            [](void* context, void* data, int n) {
                auto* to = static_cast<std::vector<uint8_t>*>(context);
                to->insert(to->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + n);
            },
            &png, size, size, 4, pixels.data(), size * 4);
        images.push_back(std::move(png));
    }

    // ICONDIR, one ICONDIRENTRY per image, then the images.
    std::vector<uint8_t> out;
    append(out, 0, 2);
    append(out, 1, 2);  // an icon
    append(out, COUNT, 2);
    uint32_t at = 6 + 16 * COUNT;
    for (int i = 0; i < COUNT; ++i) {
        append(out, SIZES[i] % 256, 1);  // 0 means 256
        append(out, SIZES[i] % 256, 1);
        append(out, 0, 1);   // no palette
        append(out, 0, 1);   // reserved
        append(out, 1, 2);   // colour planes
        append(out, 32, 2);  // bits per pixel
        append(out, static_cast<uint32_t>(images[i].size()), 4);
        append(out, at, 4);
        at += static_cast<uint32_t>(images[i].size());
    }
    for (const auto& image : images) out.insert(out.end(), image.begin(), image.end());
    return out;
}

}  // namespace fcg::logo
