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

#include "i18n.hpp"
#include "renderer.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

constexpr ImU32 CHECK     = BOARD_CHECK;
constexpr ImU32 SELECTED  = BOARD_SELECTED;
constexpr ImU32 TARGET    = BOARD_TARGET;
constexpr ImU32 BAR_WHITE = IM_COL32(236, 236, 236, 255);
constexpr ImU32 BAR_BLACK = IM_COL32(48, 48, 48, 255);

// Atlas column of a piece: white K Q R B N P, then black K Q R B N P (the order of appearance::PIECE_FILES).
int atlasIndex(chess::Piece piece) {
    // PieceType order is P N B R Q K; the atlas is K Q R B N P.
    static constexpr int by_type[6] = {5, 4, 3, 2, 1, 0};

    const int type   = std::clamp(static_cast<int>(piece.type()), 0, 5);
    const int offset = piece.color() == chess::Color::WHITE ? 0 : 6;
    return offset + by_type[type];
}

}  // namespace

chess::Square displayedTarget(const chess::Move& move) {
    if (move.typeOf() != chess::Move::CASTLING) return move.to();

    const bool king_side = move.to().index() > move.from().index();
    return chess::Square(chess::File(king_side ? 6 : 2), move.from().rank());
}

std::vector<unsigned char> rasterizePieces(int px, const appearance::PieceSvgs& svgs) {
    const int width = px * 12;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * px * 4, 0);

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) return {};

    std::vector<unsigned char> tile(static_cast<std::size_t>(px) * px * 4);
    bool ok = true;

    for (int i = 0; i < 12; ++i) {
        // nsvgParse modifies its input.
        std::string svg(svgs[static_cast<std::size_t>(i)]);
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
    if (!ok) return {};
    return pixels;
}

PieceAtlas::~PieceAtlas() { clear(); }

void PieceAtlas::clear() {
    for (const auto& level : levels_) render::destroyTexture(level.texture);
    levels_.clear();
}

bool PieceAtlas::create(int px, const appearance::PieceSvgs& svgs) {
    clear();
    // OpenGL makes the smaller sizes itself (mipmaps). SDL's renderer has none: they are rasterized from the SVGs,
    // sharper than shrunk.
    const int smallest = render::mipmapped() ? px : std::max(1, px / 8);
    for (int size = px; size >= smallest; size /= 2) {
        const auto pixels = rasterizePieces(size, svgs);
        if (pixels.empty()) return false;
        const auto texture = render::createTexture(size * 12, size, pixels.data());
        if (texture == ImTextureID_Invalid) return !levels_.empty();
        levels_.push_back({size, texture});
    }
    return true;
}

ImTextureID PieceAtlas::texture(float pixels) const noexcept {
    // Linear filtering without mipmaps stays smooth down to half the image's size: the smallest image at least as
    // large as drawn is between one and two times the size.
    for (auto it = levels_.rbegin(); it != levels_.rend(); ++it) {
        if (static_cast<float>(it->pixels) >= pixels) return it->texture;
    }
    return levels_.empty() ? ImTextureID_Invalid : levels_.front().texture;
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

namespace {

float evalBarWidth(float size) { return std::max(6.0f, size * 0.03f); }
constexpr float EVAL_BAR_GAP = 4.0f;

// Where the squares start, right of the eval bar when it is shown.
ImVec2 boardOrigin(ImVec2 top, float size, bool bar) {
    return ImVec2(top.x + (bar ? evalBarSpace(size) : 0.0f), top.y);
}

}  // namespace

float evalBarSpace(float size) { return evalBarWidth(size) + EVAL_BAR_GAP; }

void drawEvalBar(ImDrawList* dl, ImVec2 top, float size, float eval_white, bool flipped) {
    // White fills from the bottom when White is at the bottom.
    const float bar_w = evalBarWidth(size);
    const float frac  = std::clamp(eval_white, 0.0f, 1.0f);
    const float split = flipped ? top.y + size * frac : top.y + size * (1.0f - frac);
    dl->AddRectFilled(top, ImVec2(top.x + bar_w, top.y + size), flipped ? BAR_WHITE : BAR_BLACK);
    dl->AddRectFilled(flipped ? top : ImVec2(top.x, split),
                      flipped ? ImVec2(top.x + bar_w, split) : ImVec2(top.x + bar_w, top.y + size),
                      flipped ? BAR_BLACK : BAR_WHITE);
    dl->AddRectFilled(ImVec2(top.x, split), ImVec2(top.x + bar_w, split), BAR_WHITE);
}

std::optional<chess::Square> boardSquareAt(ImVec2 point, ImVec2 top, float size, const BoardDecor& decor) {
    const ImVec2 origin = boardOrigin(top, size, decor.eval_white >= 0.0f);
    const float sq      = size / 8.0f;
    const int col       = static_cast<int>(std::floor((point.x - origin.x) / sq));
    const int row       = static_cast<int>(std::floor((point.y - origin.y) / sq));
    if (col < 0 || col > 7 || row < 0 || row > 7) return std::nullopt;
    const int file = decor.flipped ? 7 - col : col;
    const int rank = decor.flipped ? row : 7 - row;
    return chess::Square(chess::File(file), chess::Rank(rank));
}

void drawBoard(const PieceAtlas& atlas, const chess::Board& board, float size, const BoardDecor& decor) {
    std::array<chess::Piece, 64> pieces;
    for (int i = 0; i < 64; ++i) pieces[static_cast<std::size_t>(i)] = board.at(chess::Square(i));
    std::optional<chess::Square> check;
    if (board.inCheck()) check = board.kingSq(board.sideToMove());
    drawBoard(atlas, pieces, size, decor, check);
}

void drawBoard(const PieceAtlas& atlas, const std::array<chess::Piece, 64>& pieces, float size,
               const BoardDecor& decor, std::optional<chess::Square> check) {
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    const ImVec2 top = ImGui::GetCursorScreenPos();

    const bool bar      = decor.eval_white >= 0.0f;
    const ImVec2 origin = boardOrigin(top, size, bar);
    const float sq      = size / 8.0f;

    if (bar) drawEvalBar(dl, top, size, decor.eval_white, decor.flipped);

    const auto colours = decor.colours ? *decor.colours : appearance::boardColours();
    std::optional<chess::Square> from, to;
    if (decor.last_move) {
        from = decor.last_move->from();
        to   = displayedTarget(*decor.last_move);
    }

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

            dl->AddRectFilled(p0, p1, light ? colours.light : colours.dark);
            if ((from && *from == square) || (to && *to == square)) {
                dl->AddRectFilled(p0, p1, colours.highlight);
                if (decor.last_move_frame > 0) {
                    const float inset = decor.last_move_frame / 2;
                    dl->AddRect(ImVec2(p0.x + inset, p0.y + inset), ImVec2(p1.x - inset, p1.y - inset),
                                decor.last_move_colour, 0.0f, 0, decor.last_move_frame);
                }
            }
            if (decor.selected && *decor.selected == square) dl->AddRectFilled(p0, p1, SELECTED);
            if (decor.drop && *decor.drop == square) dl->AddRectFilled(p0, p1, BOARD_DROP);
            if (check && *check == square) {
                dl->AddCircleFilled(ImVec2(p0.x + sq / 2, p0.y + sq / 2), sq * 0.48f, CHECK, 24);
            }

            // Coordinates along the bottom and left edges, in the opposite square colour.
            const ImU32 coord = light ? colours.dark : colours.light;
            if (row == 7) {
                const char label[2] = {static_cast<char>('a' + file), 0};
                dl->AddText(font, font_size, ImVec2(p1.x - font_size * 0.6f, p1.y - font_size * 1.05f), coord, label);
            }
            if (col == 0) {
                const char label[2] = {static_cast<char>('1' + rank), 0};
                dl->AddText(font, font_size, ImVec2(p0.x + 2.0f, p0.y + 1.0f), coord, label);
            }

            const auto piece = pieces[static_cast<std::size_t>(square.index())];
            if (piece != chess::Piece::NONE && atlas.ready() && !(decor.lifted && *decor.lifted == square)) {
                ImVec2 uv0, uv1;
                atlas.uv(piece, uv0, uv1);
                dl->AddImage(atlas.texture(sq * ImGui::GetIO().DisplayFramebufferScale.x), p0, p1, uv0, uv1);
            }
            // Where the piece picked up may go: a dot, a ring around a piece it would take.
            if (std::find(decor.targets.begin(), decor.targets.end(), square) != decor.targets.end()) {
                const ImVec2 c(p0.x + sq / 2, p0.y + sq / 2);
                if (piece != chess::Piece::NONE)
                    dl->AddCircle(c, sq * 0.45f, TARGET, 32, sq * 0.07f);
                else
                    dl->AddCircleFilled(c, sq * 0.15f, TARGET, 24);
            }
        }
    }

    // The arrows: a shaft from the centre of the first square, a head whose tip stops short of the second's centre.
    for (const auto& arrow : decor.arrows) {
        const ImVec2 a = squareCentre(arrow.move.from(), origin, size, decor.flipped);
        const ImVec2 b = squareCentre(displayedTarget(arrow.move), origin, size, decor.flipped);
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 1.0f) continue;
        const ImVec2 u(dx / length, dy / length);  // along the arrow
        const ImVec2 n(-u.y, u.x);                 // across it
        const float shaft = sq * 0.09f, head = sq * 0.24f, head_len = sq * 0.42f, stop = sq * 0.12f;
        const ImVec2 tip(b.x - u.x * stop, b.y - u.y * stop);
        const ImVec2 base(tip.x - u.x * head_len, tip.y - u.y * head_len);
        const auto segment = [&](ImVec2 p, ImVec2 q) {
            dl->AddQuadFilled(ImVec2(p.x + n.x * shaft, p.y + n.y * shaft),
                              ImVec2(q.x + n.x * shaft, q.y + n.y * shaft),
                              ImVec2(q.x - n.x * shaft, q.y - n.y * shaft),
                              ImVec2(p.x - n.x * shaft, p.y - n.y * shaft), arrow.colour);
        };
        if (arrow.dashed) {
            const float shaft_len = std::max(0.0f, length - stop - head_len), dash = sq * 0.22f, gap = sq * 0.14f;
            for (float t = 0; t < shaft_len; t += dash + gap) {
                const float e = std::min(shaft_len, t + dash);
                segment(ImVec2(a.x + u.x * t, a.y + u.y * t), ImVec2(a.x + u.x * e, a.y + u.y * e));
            }
        } else {
            segment(a, base);
        }
        dl->AddTriangleFilled(tip, ImVec2(base.x + n.x * head, base.y + n.y * head),
                              ImVec2(base.x - n.x * head, base.y - n.y * head), arrow.colour);
    }

    // A move's judgement on the corner of its square.
    if (decor.badge) {
        const ImVec2 c = squareCentre(decor.badge->square, origin, size, decor.flipped);
        const float r  = sq * 0.17f;
        const ImVec2 at(std::min(c.x + sq * 0.5f - r * 0.55f, origin.x + size - r),
                        std::max(c.y - sq * 0.5f + r * 0.55f, origin.y + r));
        drawBoardBadge(dl, at, r, *decor.badge);
    }
    if (decor.frame)
        dl->AddRect(ImVec2(origin.x - 2, origin.y - 2), ImVec2(origin.x + size + 2, origin.y + size + 2), *decor.frame,
                    0, 0, 3.0f);

    ImGui::Dummy(ImVec2(size + (bar ? evalBarSpace(size) : 0.0f), size));
}

void drawDraggedPiece(const PieceAtlas& atlas, chess::Piece piece, float size) {
    if (piece == chess::Piece::NONE || !atlas.ready()) return;
    const float sq  = size / 8.0f;
    const ImVec2 at = ImGui::GetIO().MousePos;
    ImVec2 uv0, uv1;
    atlas.uv(piece, uv0, uv1);
    // Over the panels around the board too, as a piece held in the hand.
    ImGui::GetForegroundDrawList()->AddImage(atlas.texture(sq * ImGui::GetIO().DisplayFramebufferScale.x),
                                             ImVec2(at.x - sq / 2, at.y - sq / 2), ImVec2(at.x + sq / 2, at.y + sq / 2),
                                             uv0, uv1);
}

void drawBoardBadge(ImDrawList* dl, ImVec2 centre, float radius, const BoardBadge& badge) {
    ImFont* font = ImGui::GetFont();
    dl->AddCircleFilled(ImVec2(centre.x + 1, centre.y + 1.5f), radius, IM_COL32(0, 0, 0, 70), 24);
    dl->AddCircleFilled(centre, radius, badge.colour, 24);
    const float fs  = radius * 1.25f;
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, badge.text.c_str());
    dl->AddText(font, fs, ImVec2(centre.x - ts.x / 2, centre.y - ts.y / 2), IM_COL32_WHITE, badge.text.c_str());
}

ImVec2 squareCentre(chess::Square square, ImVec2 origin, float size, bool flipped) {
    const int file = static_cast<int>(square.file());
    const int rank = static_cast<int>(square.rank());
    const int col  = flipped ? 7 - file : file;
    const int row  = flipped ? rank : 7 - rank;
    const float sq = size / 8.0f;
    return ImVec2(origin.x + (static_cast<float>(col) + 0.5f) * sq, origin.y + (static_cast<float>(row) + 0.5f) * sq);
}

std::vector<chess::Move> legalPrefix(chess::Board board, const std::vector<std::string>& pv, std::size_t max_moves) {
    std::vector<chess::Move> out;
    for (std::size_t i = 0; i < pv.size() && out.size() < max_moves; ++i) {
        const auto move = chess::uci::uciToMove(board, pv[i]);
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        if (move == chess::Move::NO_MOVE || std::find(legal.begin(), legal.end(), move) == legal.end()) break;
        out.push_back(move);
        board.makeMove<true>(move);
    }
    return out;
}

BoardInfo boardInfo(const chess::Board& board) {
    using chess::Color;
    using chess::PieceType;
    static constexpr PieceType types[5] = {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP, PieceType::ROOK,
                                           PieceType::QUEEN};
    static constexpr int values[5]      = {1, 3, 3, 5, 9};

    BoardInfo info;
    info.halfmoves = board.halfMoveClock();
    info.pieces    = board.occ().count();
    for (int i = 0; i < 5; ++i) {
        info.surplus[i] = board.pieces(types[i], Color::WHITE).count() - board.pieces(types[i], Color::BLACK).count();
        info.balance += info.surplus[i] * values[i];
    }
    return info;
}

void drawBoardInfo(const PieceAtlas& atlas, const chess::Board& board, float width, int tb_pieces) {
    const auto info    = boardInfo(board);
    const bool labels  = width >= ImGui::GetFontSize() * 22.0f;
    const float icon   = ImGui::GetTextLineHeight();
    const float spread = ImGui::GetFontSize() * (labels ? 1.2f : 0.6f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, ImGui::GetStyle().ItemSpacing.y));

    // 50-move counter, in the warning colour for its last 20 plies.
    const auto fifty = labels ? trf("50 moves {}/100", info.halfmoves) : fmt::format("{}/100", info.halfmoves);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(info.halfmoves >= 80 ? ui::WARN : ui::MUTED), "%s",
                       fifty.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("Plies since the last capture or pawn move; 100 is a draw."));

    // Material: the pieces one side has more of, then the balance.
    ImGui::SameLine(0, spread);
    ImGui::BeginGroup();
    static constexpr chess::PieceType types[5] = {chess::PieceType::QUEEN, chess::PieceType::ROOK,
                                                  chess::PieceType::BISHOP, chess::PieceType::KNIGHT,
                                                  chess::PieceType::PAWN};
    // Pieces of a kind overlap a little, as on a score sheet.
    const ImVec2 start = ImGui::GetCursorScreenPos();
    float x            = start.x;
    for (int i = 0; i < 5 && atlas.ready(); ++i) {
        const int n = info.surplus[4 - i];
        if (n == 0) continue;
        ImVec2 uv0, uv1;
        atlas.uv(chess::Piece(types[i], n > 0 ? chess::Color::WHITE : chess::Color::BLACK), uv0, uv1);
        const ImTextureID texture = atlas.texture(icon * ImGui::GetIO().DisplayFramebufferScale.x);
        for (int k = 0; k < std::min(std::abs(n), 8); ++k) {
            ImGui::GetWindowDrawList()->AddImage(texture, ImVec2(x, start.y), ImVec2(x + icon, start.y + icon), uv0,
                                                 uv1);
            x += icon * 0.6f;
        }
        x += icon * 0.5f;
    }
    if (x > start.x) {
        ImGui::Dummy(ImVec2(x - start.x, icon));
        ImGui::SameLine();
    }
    ImGui::TextDisabled("%s", info.balance == 0  ? "="
                              : info.balance > 0 ? fmt::format("+{}", info.balance).c_str()
                                                 : fmt::format("−{}", -info.balance).c_str());
    ImGui::EndGroup();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Material difference, in pawns, from White's side."));

    // Pieces left, and whether the tablebases can end the game.
    ImGui::SameLine(0, spread);
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", labels ? trf("{} pieces", info.pieces).c_str() : std::to_string(info.pieces).c_str());
    const bool tb = tb_pieces > 0 && info.pieces <= tb_pieces;
    if (tb) {
        ImGui::SameLine(0, ImGui::GetFontSize() * 0.3f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "TB");
    }
    ImGui::EndGroup();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tb ? TR("Few enough pieces for the tablebase adjudication.")
                                   : TR("Pieces on the board, kings included."));
    }
    ImGui::PopStyleVar();
}

}  // namespace fcg
