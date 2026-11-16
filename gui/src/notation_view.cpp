#include "notation_view.hpp"

#include <algorithm>

#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

}  // namespace

std::optional<Figurine> figurineOf(const std::string& san) {
    if (san.empty()) return std::nullopt;
    switch (san.front()) {
        case 'K':
            return Figurine{chess::PieceType::KING, san.substr(1)};
        case 'Q':
            return Figurine{chess::PieceType::QUEEN, san.substr(1)};
        case 'R':
            return Figurine{chess::PieceType::ROOK, san.substr(1)};
        case 'B':
            return Figurine{chess::PieceType::BISHOP, san.substr(1)};
        case 'N':
            return Figurine{chess::PieceType::KNIGHT, san.substr(1)};
        default:
            return std::nullopt;
    }
}

std::vector<NotationItem> lineItems(chess::Board board, const std::vector<std::string>& pv, std::size_t max_moves) {
    std::vector<NotationItem> out;
    for (std::size_t i = 0; i < pv.size() && i < max_moves; ++i) {
        const auto move = chess::uci::uciToMove(board, pv[i]);
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        if (move == chess::Move::NO_MOVE || std::find(legal.begin(), legal.end(), move) == legal.end()) break;
        if (board.sideToMove() == chess::Color::WHITE || i == 0) {
            NotationItem number;
            number.kind = NotationItem::Kind::Number;
            number.text =
                std::to_string(board.fullMoveNumber()) + (board.sideToMove() == chess::Color::WHITE ? "." : "…");
            out.push_back(std::move(number));
        }
        NotationItem item;
        item.text = chess::uci::moveToSan(board, move);
        item.id   = static_cast<int>(i);
        out.push_back(std::move(item));
        board.makeMove<true>(move);
    }
    return out;
}

NotationEvents drawNotation(const char* id, const std::vector<NotationItem>& items, float width,
                            const PieceAtlas* figurines, int max_lines) {
    NotationEvents ev;
    ImGui::PushID(id);
    auto* dl                  = ImGui::GetWindowDrawList();
    auto* font                = ImGui::GetFont();
    const float fs            = ImGui::GetFontSize();
    const ImVec2 o            = ImGui::GetCursorScreenPos();
    const float room          = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    const float lh            = ImGui::GetTextLineHeight() + px(7);
    const float pad           = px(3);
    const float gap           = px(4);
    const bool usable         = figurines && figurines->ready();
    const ImVec2 mouse        = ImGui::GetMousePos();
    const bool window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_None);
    const ImU32 text_col      = ImGui::GetColorU32(ImGuiCol_Text);

    // Each item's width: a move's figurine and text, its mark, the room around it.
    const auto measure = [&](const NotationItem& it, std::optional<Figurine>& fig) {
        fig.reset();
        float w = 0;
        if (it.kind == NotationItem::Kind::Move) {
            if (usable) fig = figurineOf(it.text);
            w = fig ? fs * 1.05f + ImGui::CalcTextSize(fig->rest.c_str()).x : ImGui::CalcTextSize(it.text.c_str()).x;
            if (!it.mark.empty()) w += ImGui::CalcTextSize(it.mark.c_str()).x + px(1);
            w += pad * 2;
        } else {
            w = ImGui::CalcTextSize(it.text.c_str()).x + (it.kind == NotationItem::Kind::Number ? px(1) : 0.0f);
        }
        return w;
    };

    float x = o.x, y = o.y;
    int line          = 1;
    bool line_started = false;
    bool cut          = false;
    for (std::size_t i = 0; i < items.size() && !cut; ++i) {
        const auto& it = items[i];
        if (it.kind == NotationItem::Kind::Break) {
            if (max_lines > 0 && line >= max_lines) {
                cut = true;
                break;
            }
            x = o.x, y += lh, ++line, line_started = false;
            continue;
        }
        std::optional<Figurine> fig;
        const float w = measure(it, fig);
        // A move number stays with its move: wrapped together.
        float keep = w;
        if (it.kind == NotationItem::Kind::Number && i + 1 < items.size()) {
            std::optional<Figurine> next_fig;
            keep += measure(items[i + 1], next_fig);
        }
        if (line_started && x + keep > o.x + room) {
            if (max_lines > 0 && line >= max_lines) {
                cut = true;
                break;
            }
            x = o.x + it.indent, y += lh, ++line;
        } else if (!line_started) {
            x = o.x + it.indent;
        }
        line_started = true;
        const ImVec2 p0(x, y), p1(x + w, y + lh - px(2));
        if (it.kind == NotationItem::Kind::Move) {
            const bool hovered =
                window_hovered && mouse.x >= p0.x && mouse.x < p1.x && mouse.y >= p0.y && mouse.y < p1.y;
            if (it.current) {
                dl->AddRectFilled(p0, p1, ui::SELECT, px(4));
                dl->AddRect(p0, p1, ui::ACCENT, px(4));
                ev.current_min = p0, ev.current_max = p1;
            } else if (hovered) {
                dl->AddRectFilled(p0, p1, ui::SELECT, px(4));
            }
            if (hovered && it.id >= 0) {
                ev.hovered = it.id;
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ev.clicked = it.id;
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ev.right_clicked = it.id;
            }
            const ImU32 col = it.colour ? it.colour : text_col;
            float tx        = x + pad;
            const float ty  = y + (lh - px(2) - ImGui::GetTextLineHeight()) / 2;
            const auto put  = [&](const std::string& text, ImU32 c) {
                dl->AddText(font, fs, ImVec2(tx, ty), c, text.c_str());
                if (it.bold) dl->AddText(font, fs, ImVec2(tx + 0.6f, ty), c, text.c_str());
                tx += ImGui::CalcTextSize(text.c_str()).x;
            };
            if (fig) {
                ImVec2 uv0, uv1;
                figurines->uv(chess::Piece(fig->type, chess::Color::WHITE), uv0, uv1);
                const float s = fs * 1.05f;
                const ImVec2 a(tx - px(1), ty + (ImGui::GetTextLineHeight() - s) / 2 - px(1));
                const ImU32 tint = it.colour ? IM_COL32(255, 255, 255, 150) : IM_COL32_WHITE;
                dl->AddImage(figurines->texture(s * ImGui::GetIO().DisplayFramebufferScale.x), a,
                             ImVec2(a.x + s, a.y + s), uv0, uv1, tint);
                tx += s;
                put(fig->rest, col);
            } else {
                put(it.text, col);
            }
            if (!it.mark.empty()) {
                tx += px(1);
                put(it.mark, it.mark_colour ? it.mark_colour : col);
            }
            x += w + gap * 0.5f;
        } else {
            // No room inside the brackets of a variation.
            if (it.text == ")" && x > o.x + gap) x -= gap * 0.5f + pad;
            const ImU32 col = it.colour ? it.colour : ui::MUTED;
            dl->AddText(font, fs, ImVec2(x, y + (lh - px(2) - ImGui::GetTextLineHeight()) / 2), col, it.text.c_str());
            x += w + (it.kind == NotationItem::Kind::Number ? px(1) : it.text == "(" ? 0.0f : gap);
        }
    }
    if (cut) dl->AddText(font, fs, ImVec2(std::min(x, o.x + room - ImGui::CalcTextSize("…").x), y), ui::MUTED, "…");
    ImGui::Dummy(ImVec2(room, y + lh - o.y));
    ImGui::PopID();
    return ev;
}

}  // namespace fcg
