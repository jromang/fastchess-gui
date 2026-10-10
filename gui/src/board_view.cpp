#include "board_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

#include "gl.hpp"

namespace fcg::assets {
extern const std::array<std::string_view, 12> piece_svgs;
}

namespace fcg {

namespace {

constexpr ImU32 LIGHT     = IM_COL32(222, 227, 230, 255);
constexpr ImU32 DARK      = IM_COL32(140, 162, 173, 255);
constexpr ImU32 LAST_MOVE = IM_COL32(155, 199, 0, 105);
constexpr ImU32 CHECK     = IM_COL32(220, 40, 40, 150);
constexpr ImU32 BAR_WHITE = IM_COL32(236, 236, 236, 255);
constexpr ImU32 BAR_BLACK = IM_COL32(48, 48, 48, 255);

// Atlas column of a piece: white K Q R B N P, then black K Q R B N P (the order of piece_svgs).
int atlasIndex(chess::Piece piece) {
    // PieceType order is P N B R Q K; the atlas is K Q R B N P.
    static constexpr int by_type[6] = {5, 4, 3, 2, 1, 0};

    const int type   = std::clamp(static_cast<int>(piece.type()), 0, 5);
    const int offset = piece.color() == chess::Color::WHITE ? 0 : 6;
    return offset + by_type[type];
}

// The square a move visibly lands on: castling is stored as king takes rook.
chess::Square displayedTarget(const chess::Move& move) {
    if (move.typeOf() != chess::Move::CASTLING) return move.to();

    const bool king_side = move.to().index() > move.from().index();
    return chess::Square(chess::File(king_side ? 6 : 2), move.from().rank());
}

}  // namespace

PieceAtlas::~PieceAtlas() {
    if (texture_ != 0 && gl::fn().DeleteTextures) gl::fn().DeleteTextures(1, &texture_);
}

bool PieceAtlas::create(int px) {
    const int width = px * 12;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * px * 4, 0);

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) return false;

    std::vector<unsigned char> tile(static_cast<std::size_t>(px) * px * 4);
    bool ok = true;

    for (int i = 0; i < 12; ++i) {
        // nsvgParse modifies its input.
        std::string svg(assets::piece_svgs[static_cast<std::size_t>(i)]);
        NSVGimage* image = nsvgParse(svg.data(), "px", 96.0f);
        if (!image) {
            ok = false;
            continue;
        }

        std::fill(tile.begin(), tile.end(), 0);
        const float scale = static_cast<float>(px) / std::max(image->width, image->height);
        nsvgRasterize(rast, image, 0, 0, scale, tile.data(), px, px, px * 4);
        nsvgDelete(image);

        for (int y = 0; y < px; ++y) {
            std::memcpy(&pixels[(static_cast<std::size_t>(y) * width + static_cast<std::size_t>(i) * px) * 4],
                        &tile[static_cast<std::size_t>(y) * px * 4], static_cast<std::size_t>(px) * 4);
        }
    }

    nsvgDeleteRasterizer(rast);
    if (!ok) return false;

    const auto& f = gl::fn();
    f.GenTextures(1, &texture_);
    f.BindTexture(gl::TEXTURE_2D, texture_);
    f.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR_MIPMAP_LINEAR);
    f.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    f.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    f.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    f.PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    f.TexImage2D(gl::TEXTURE_2D, 0, static_cast<gl::GLint>(gl::RGBA), width, px, 0, gl::RGBA, gl::UNSIGNED_BYTE,
                 pixels.data());
    f.GenerateMipmap(gl::TEXTURE_2D);
    f.BindTexture(gl::TEXTURE_2D, 0);

    return true;
}

void PieceAtlas::uv(chess::Piece piece, ImVec2& uv0, ImVec2& uv1) const {
    const int i = atlasIndex(piece);
    uv0         = ImVec2(i / 12.0f, 0.0f);
    uv1         = ImVec2((i + 1) / 12.0f, 1.0f);
}

float evalBarFraction(int cp_white, bool mate, int mate_white) {
    if (mate) return mate_white > 0 ? 1.0f : 0.0f;
    // Logistic win expectancy, about 76% for a one pawn advantage.
    return 1.0f / (1.0f + std::pow(10.0f, -static_cast<float>(cp_white) / 400.0f));
}

void drawBoard(const PieceAtlas& atlas, const chess::Board& board, float size, const BoardDecor& decor) {
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    const ImVec2 top = ImGui::GetCursorScreenPos();

    const bool bar      = decor.eval_white >= 0.0f;
    const float bar_w   = bar ? std::max(6.0f, size * 0.03f) : 0.0f;
    const float gap     = bar ? 4.0f : 0.0f;
    const ImVec2 origin = ImVec2(top.x + bar_w + gap, top.y);
    const float sq      = size / 8.0f;

    if (bar) {
        // White fills from the bottom when White is at the bottom.
        const float frac  = std::clamp(decor.eval_white, 0.0f, 1.0f);
        const float split = decor.flipped ? top.y + size * frac : top.y + size * (1.0f - frac);
        dl->AddRectFilled(top, ImVec2(top.x + bar_w, top.y + size), decor.flipped ? BAR_WHITE : BAR_BLACK);
        dl->AddRectFilled(decor.flipped ? top : ImVec2(top.x, split),
                          decor.flipped ? ImVec2(top.x + bar_w, split) : ImVec2(top.x + bar_w, top.y + size),
                          decor.flipped ? BAR_BLACK : BAR_WHITE);
        dl->AddRectFilled(ImVec2(top.x, split), ImVec2(top.x + bar_w, split), BAR_WHITE);
    }

    std::optional<chess::Square> from, to, check;
    if (decor.last_move) {
        from = decor.last_move->from();
        to   = displayedTarget(*decor.last_move);
    }
    if (board.inCheck()) check = board.kingSq(board.sideToMove());

    const float font_size = std::max(9.0f, sq * 0.2f);
    ImFont* font          = ImGui::GetFont();

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const int col = decor.flipped ? 7 - file : file;
            const int row = decor.flipped ? rank : 7 - rank;

            const ImVec2 p0(origin.x + col * sq, origin.y + row * sq);
            const ImVec2 p1(p0.x + sq, p0.y + sq);

            const chess::Square square{chess::File(file), chess::Rank(rank)};
            const bool light = (file + rank) % 2 == 1;

            dl->AddRectFilled(p0, p1, light ? LIGHT : DARK);
            if ((from && *from == square) || (to && *to == square)) dl->AddRectFilled(p0, p1, LAST_MOVE);
            if (check && *check == square) {
                dl->AddCircleFilled(ImVec2(p0.x + sq / 2, p0.y + sq / 2), sq * 0.48f, CHECK, 24);
            }

            // Coordinates along the bottom and left edges, in the opposite square colour.
            const ImU32 coord = light ? DARK : LIGHT;
            if (row == 7) {
                const char label[2] = {static_cast<char>('a' + file), 0};
                dl->AddText(font, font_size, ImVec2(p1.x - font_size * 0.6f, p1.y - font_size * 1.05f), coord, label);
            }
            if (col == 0) {
                const char label[2] = {static_cast<char>('1' + rank), 0};
                dl->AddText(font, font_size, ImVec2(p0.x + 2.0f, p0.y + 1.0f), coord, label);
            }

            const auto piece = board.at(square);
            if (piece != chess::Piece::NONE && atlas.ready()) {
                ImVec2 uv0, uv1;
                atlas.uv(piece, uv0, uv1);
                dl->AddImage(atlas.texture(), p0, p1, uv0, uv1);
            }
        }
    }

    ImGui::Dummy(ImVec2(size + bar_w + gap, size));
}

}  // namespace fcg
