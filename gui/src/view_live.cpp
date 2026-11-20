// The live view: the mosaic of the games being played, and the parts of the game followed (board, players, searches,
// moves, chart), beside it or each in a panel of its own; detached boards.

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <future>
#include <sstream>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <fmt/include/fmt/format.h>
#include <engine/uci_engine.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "layout.hpp"
#include "ui.hpp"

namespace fcg {

int pvAgreement(const std::string& previous_pv, const std::string& pv) {
    // The previous PV starts with the opponent's move: what it expected next is from its second move on.
    std::istringstream expected(previous_pv), actual(pv);
    std::string a, b;
    expected >> a;
    int n = 0;
    while (expected >> a && actual >> b && a == b) ++n;
    return n;
}

const char* pgnFieldOption(uint8_t field) {
    // As the checkboxes of the setup's PGN options name them.
    switch (field) {
        case PGN_NODES:
            return TR("nodes");
        case PGN_SELDEPTH:
            return "seldepth";
        case PGN_NPS:
            return "nps";
        case PGN_TBHITS:
            return "tbhits";
        case PGN_PV:
            return "pv";
        case PGN_DECISION:
            return TR("decision time");
        default:
            return "";
    }
}

bool pgnFieldOn(const fastchess::config::Pgn& pgn, uint8_t field) {
    switch (field) {
        case PGN_NODES:
            return pgn.track_nodes;
        case PGN_SELDEPTH:
            return pgn.track_seldepth;
        case PGN_NPS:
            return pgn.track_nps;
        case PGN_TBHITS:
            return pgn.track_tbhits;
        case PGN_PV:
            return pgn.track_pv;
        case PGN_DECISION:
            return pgn.track_decision;
        default:
            return false;
    }
}

std::string missingPgnFieldTip(uint8_t field, std::optional<bool> on) {
    if (!on) {
        return trf("Written when \"{}\" is on in the PGN options of the setup and the engines give what it needs.",
                   pgnFieldOption(field));
    }
    if (*on) {
        return trf(
            "\"{}\" was on in this tournament's PGN options: the engines did not give what it needs in this "
            "game.",
            pgnFieldOption(field));
    }
    return trf("Written when \"{}\" is on in the PGN options of the setup.", pgnFieldOption(field));
}

void wdlLine(const std::array<int64_t, 3>& wdl) {
    const double sum = std::max<int64_t>(1, wdl[0] + wdl[1] + wdl[2]);
    const auto text =
        trf("W {:.0f} · D {:.0f} · L {:.0f} %", 100 * wdl[0] / sum, 100 * wdl[1] / sum, 100 * wdl[2] / sum);
    ImGui::TextColored(ui::MUTED4, "%s", text.c_str());
    ImGui::SameLine();

    // The three shares in a thin bar, vertically centred on the text.
    const float width = ImGui::GetFontSize() * 6.0f, height = ImGui::GetFontSize() * 0.4f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float y  = p.y + (ImGui::GetTextLineHeight() - height) / 2;
    const float x1 = p.x + width * static_cast<float>(wdl[0] / sum);
    const float x2 = x1 + width * static_cast<float>(wdl[1] / sum);
    auto* dl       = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(x1, y + height), IM_COL32(110, 190, 120, 255));
    dl->AddRectFilled(ImVec2(x1, y), ImVec2(x2, y + height), IM_COL32(160, 160, 160, 255));
    dl->AddRectFilled(ImVec2(x2, y), ImVec2(p.x + width, y + height), IM_COL32(215, 95, 95, 255));
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
}

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// Width of the panel beside the followed board: players, moves and charts; two moves a row in the list, each with its
// score, depth and time.
float sidePanel() { return px(400); }

// Height of a mosaic card `card` wide: the board, two name rows, the details and the evaluation bar, inside the
// card's padding.
float cardHeight(float card, bool tile_info) {
    const float line = ImGui::GetTextLineHeightWithSpacing();
    return (card - 2 * px(6)) + 2 * line + (tile_info ? line : 0.0f) + px(4) + 2 * ImGui::GetStyle().ItemSpacing.y +
           2 * px(5);
}

struct MosaicGrid {
    int cols     = 1;
    float card   = 0;      // width of a card
    bool scrolls = false;  // too many games for the room: cards of the smallest size, under a scroll bar

    [[nodiscard]] float width() const {
        return cols * card + (cols - 1) * ImGui::GetStyle().ItemSpacing.x +
               (scrolls ? ImGui::GetStyle().ScrollbarSize : 0.0f);
    }
};

// The number of columns that gives `n` games the largest boards in `size`, like the tiles of a video call.
MosaicGrid mosaicGrid(int n, ImVec2 size, bool tile_info) {
    const ImVec2 gap  = ImGui::GetStyle().ItemSpacing;
    const float extra = cardHeight(0, tile_info);
    MosaicGrid grid;
    for (int cols = 1; cols <= n; ++cols) {
        const int rows   = (n + cols - 1) / cols;
        const float wide = (size.x - gap.x * (cols - 1)) / cols;
        const float tall = (size.y - gap.y * (rows - 1)) / rows - extra;
        const float card = std::floor(std::min(wide, tall) - 1.0f);  // a pixel spare, against a stray scroll bar
        if (card > grid.card) grid = {cols, card, false};
    }
    if (grid.card < px(120)) {
        const float width = size.x - ImGui::GetStyle().ScrollbarSize;
        grid.cols         = std::max(1, static_cast<int>((width + gap.x) / (px(120) + gap.x)));
        grid.card         = std::floor((width - gap.x * (grid.cols - 1)) / grid.cols);
        grid.scrolls      = true;
    }
    return grid;
}

// Evaluation shown by the bar: the side to move's current search, else the last move's score. With `ply` before the
// last move (the board stepped back), the score of the last move before it.
float evalBar(const SlotState& st, int ply = -1) {
    std::optional<fastchess::Score> score;
    chess::Color pov   = st.stm;
    const auto& info   = st.info[st.stm == chess::Color::WHITE ? 0 : 1];
    const bool earlier = ply >= 0 && static_cast<std::size_t>(ply) < st.moves.size();
    if (!earlier && st.thinking && !info.empty()) {
        const auto parsed = fastchess::engine::UciEngine::parseInfo(info);
        if (parsed.score) score = *parsed.score;
    }
    if (!score) {
        const auto end = earlier ? st.moves.begin() + ply : st.moves.end();
        for (auto it = std::make_reverse_iterator(end); it != st.moves.rend(); ++it) {
            if (!it->book && it->score) {
                score = it->score;
                pov   = it->mover;
                break;
            }
        }
    }
    if (!score) return 0.5f;
    const int sign = pov == chess::Color::WHITE ? 1 : -1;
    if (score->type == fastchess::ScoreType::MATE) {
        return evalBarFraction(0, true, sign * static_cast<int>(score->value >= 0 ? 1 : -1));
    }
    if (score->type == fastchess::ScoreType::CP)
        return evalBarFraction(sign * static_cast<int>(score->value), false, 0);
    return 0.5f;
}

// The colours of the engines in the live view: White's light blue, Black's orange.
ImU32 engineColour(chess::Color color, int alpha) {
    return color == chess::Color::WHITE ? IM_COL32(110, 168, 222, alpha) : IM_COL32(237, 153, 77, alpha);
}

}  // namespace

std::vector<BoardArrow> pvArrows(const SlotState& st, const chess::Board& board) {
    std::vector<BoardArrow> arrows;
    const auto& line = st.info[st.stm == chess::Color::WHITE ? 0 : 1];
    if (!st.active || !st.thinking || line.empty()) return arrows;
    const auto moves = legalPrefix(board, fastchess::engine::UciEngine::parseInfo(line).pv, 2);
    // The reply first, so that the move is drawn over it.
    if (moves.size() > 1) arrows.push_back({moves[1], engineColour(~st.stm, 150)});
    if (!moves.empty()) arrows.push_back({moves[0], engineColour(st.stm, 210)});
    return arrows;
}

namespace {

// A panel `width` wide shows the game followed at large: each engine's search figure by figure with the whole line,
// its logo as high as its name and clock. Narrower (beside the mosaic), the short line of the cards.
bool largePanel(float width) { return width >= px(440); }

// `text` wrapped `width` wide in the current font, cut after the last word that keeps it within `lines` lines, with "…"
// when cut.
std::string withinLines(const std::string& text, float width, int lines) {
    const float most = ImGui::GetFontSize() * static_cast<float>(lines) + 0.5f;
    if (ImGui::CalcTextSize(text.c_str(), nullptr, false, width).y <= most) return text;
    std::string out;
    std::istringstream words(text);
    for (std::string w; words >> w;) {
        auto next = out.empty() ? w : out + " " + w;
        if (ImGui::CalcTextSize((next + " …").c_str(), nullptr, false, width).y > most) break;
        out = std::move(next);
    }
    return out + " …";
}

// The cursor moved down to `top` + `height` when what was drawn since `top` took less: the room kept left blank.
// Heights are counted as the cursor moves, the spacing under the last item included.
void keepHeight(float top, float height) {
    if (height <= 0.0f || ImGui::GetCursorPosY() >= top + height) return;
    ImGui::SetCursorPosY(top + height - ImGui::GetStyle().ItemSpacing.y);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// Figures each under its name, in chips that wrap to the width of the panel.
void figureChips(const std::vector<std::pair<std::string, std::string>>& figures) {
    ImFont* font         = ImGui::GetFont();
    ImFont* bold         = ui::titleFont() ? ui::titleFont() : font;
    const float fs       = ImGui::GetFontSize();
    const float label_px = fs * 0.78f;
    const ImVec2 pad(px(8), px(4));
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    float end         = -1.0f;  // the right edge of the last chip, -1 before the first
    for (const auto& [name, value] : figures) {
        const ImVec2 ns = font->CalcTextSizeA(label_px, FLT_MAX, 0.0f, name.c_str());
        const ImVec2 vs = bold->CalcTextSizeA(fs, FLT_MAX, 0.0f, value.c_str());
        const float w   = std::max({ns.x, vs.x, px(48)}) + 2 * pad.x;
        const float h   = ns.y + vs.y + 2 * pad.y;
        if (end >= 0 && end + px(6) + w <= right) ImGui::SameLine(0, px(6));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        auto* dl        = ImGui::GetWindowDrawList();
        dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), px(6));
        dl->AddText(font, label_px, ImVec2(at.x + pad.x, at.y + pad.y), ui::MUTED, name.c_str());
        dl->AddText(bold, fs, ImVec2(at.x + pad.x, at.y + pad.y + ns.y), ImGui::GetColorU32(ImGuiCol_Text),
                    value.c_str());
        ImGui::Dummy(ImVec2(w, h));
        end = ImGui::GetItemRectMax().x;
    }
}

// Two choices side by side in a strip, the one chosen raised. True when the choice changed.
bool segmented(const char* id, const char* first, const char* second, bool& chose_second) {
    ImGui::PushID(id);
    const float edge      = px(3);
    const float h         = ImGui::GetFrameHeight() - 2 * edge;
    const float pad_x     = px(9);
    const char* labels[2] = {first, second};
    const float w[2]      = {ImGui::CalcTextSize(first).x + 2 * pad_x, ImGui::CalcTextSize(second).x + 2 * pad_x};
    const ImVec2 at       = ImGui::GetCursorScreenPos();
    const ImVec2 size(w[0] + w[1] + px(2) + 2 * edge, h + 2 * edge);
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg), px(7));
    bool changed = false;
    float x      = at.x + edge;
    for (int i = 0; i < 2; ++i) {
        const bool chosen = (i == 1) == chose_second;
        ImGui::SetCursorScreenPos(ImVec2(x, at.y + edge));
        if (ImGui::InvisibleButton(i == 0 ? "##first" : "##second", ImVec2(w[i], h)) && !chosen) {
            chose_second = i == 1;
            changed      = true;
        }
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        if (chosen) {
            dl->AddRectFilled(a, b, ui::CARD_BG, px(5));
            dl->AddRect(a, b, ui::CARD_BD, px(5));
        } else if (ImGui::IsItemHovered()) {
            dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), px(5));
        }
        const float ty = a.y + (h - ImGui::GetTextLineHeight()) / 2;
        dl->AddText(ImVec2(a.x + pad_x, ty), chosen ? ImGui::GetColorU32(ImGuiCol_Text) : ui::MUTED, labels[i]);
        x += w[i] + px(2);
    }
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy(size);
    ImGui::PopID();
    return changed;
}

// The width segmented() takes for these labels.
float segmentedWidth(const char* first, const char* second) {
    return ImGui::CalcTextSize(first).x + ImGui::CalcTextSize(second).x + 4 * px(9) + px(2) + 2 * px(3);
}

std::string clockText(const SlotState& st, chess::Color color, int64_t elapsed) {
    const bool thinks = st.active && st.stm == color && st.thinking;
    if (st.clocks) {
        auto ms = color == chess::Color::WHITE ? st.white_ms : st.black_ms;
        if (thinks) ms -= elapsed;
        return ui::formatClock(ms);
    }
    return thinks ? ui::formatClock(elapsed) : std::string();
}

}  // namespace

void App::refreshSlot(int slot) {
    auto* obs = run_->observer();
    if (!obs || slot < 0) return;
    if (static_cast<int>(slot_views_.size()) < obs->slotCount())
        slot_views_.resize(static_cast<std::size_t>(obs->slotCount()));
    if (slot >= static_cast<int>(slot_views_.size())) return;

    auto& v = slot_views_[static_cast<std::size_t>(slot)];
    obs->copySlot(slot, v.state);
    if (v.state.active) v.last_active = Clock::now();
    // A gauntlet's new game, from its seed's side; Flip turns it over until the next game.
    if (setup_.live_seed_side && v.state.has_game && v.state.game_id != v.turned_for) {
        v.turned_for = v.state.game_id;
        if (const auto* t = run_->tournament()) {
            if (const auto black = seedHasBlack(t->spec(), v.state.white, v.state.black)) v.flip = *black;
        }
    }
    // A new game: its board follows it, whatever position the previous one was stepped back to.
    if (v.state.has_game && v.state.game_id != v.nav_game) {
        v.nav_game = v.state.game_id;
        v.nav.follow();
        v.listed_ply = -1;
        v.pv_rows    = {};
        v.card_h     = {};
        v.search_h   = {};
    }
    if (v.state.has_game && v.state.version != v.board_version) {
        v.board = chess::Board();
        v.board.setFen(v.state.start_fen);
        v.last_capture = false;
        for (const auto& m : v.state.moves) {
            v.last_capture = v.board.isCapture(m.move);
            v.board.makeMove<true>(m.move);
        }
        v.board_version = v.state.version;
    }
    // Its opening, read again when a move comes.
    if (v.state.has_game) {
        const std::pair<std::size_t, std::size_t> at{v.state.game_id, v.state.moves.size()};
        if (at != v.opening_at) {
            const auto* t       = run_->tournament();
            const bool chess960 = t && t->spec().conditions.variant == fastchess::VariantType::FRC;
            v.opening           = liveOpening(v.state, chess960);
            v.opening_at        = at;
        }
    }
}

int App::tbPieces() {
    const auto* t = run_->tournament();
    if (t != tb_tournament_) {
        tb_tournament_ = t;
        tb_pieces_     = t ? tablebasePieces(t->spec().conditions.tb_adjudication) : 0;
    }
    return tb_pieces_;
}

void App::drawLive() {
    if (!ui::beginPanel(TR("Live###live"))) {
        ui::endPanel();
        return;
    }

    auto* obs = run_->observer();
    if (!obs) {
        ImGui::TextDisabled("%s", TR("No match yet. Choose two engines, then click Start."));
        ui::endPanel();
        return;
    }

    const int slots = obs->slotCount();
    live_slot_      = std::clamp(live_slot_, 0, slots - 1);
    for (int i = 0; i < slots; ++i) refreshSlot(i);
    // As many cards as games, not as threads: a tournament's last games, or fewer games than threads, leave most
    // slots idle. The game followed is one of those shown.
    {
        std::vector<SlotSeen> seen;
        seen.reserve(static_cast<std::size_t>(slots));
        for (int i = 0; i < slots; ++i) {
            const auto& v = slot_views_[static_cast<std::size_t>(i)];
            seen.push_back({v.state.active, v.state.has_game, v.last_active});
        }
        live_shown_ = shownSlots(seen, live_slot_, Clock::now());
        if (!live_shown_.empty() && std::find(live_shown_.begin(), live_shown_.end(), live_slot_) == live_shown_.end())
            live_slot_ = live_shown_.front();
    }
    const int shown = static_cast<int>(live_shown_.size());
    hearMoves(live_slot_);

    // The parts of the followed game that have no panel of their own open are shown here, beside the mosaic.
    const unsigned parts = livePartsHere();

    // Mosaic of every game with the chosen one beside it, or one game with tabs.
    const bool mosaic = live_mosaic_ && shown > 1;
    if (slots > 1) {
        if (ImGui::RadioButton(TR("Mosaic"), live_mosaic_)) live_mosaic_ = true;
        ImGui::SameLine();
        if (ImGui::RadioButton(TR("One game"), !live_mosaic_)) live_mosaic_ = false;
        ImGui::SameLine(0, px(24));
        if (mosaic) {
            if (ImGui::Checkbox(TR("Details under the boards"), &setup_.tile_info)) saveSetupSoon();
            ui::help(TR("The 50-move counter, the material and the pieces left, under every board of the mosaic."));
            ImGui::SameLine(0, px(24));
        }
    }

    if (const auto* t = run_->tournament(); t && t->spec().format == tour::Format::Gauntlet) {
        if (ImGui::Checkbox(TR("Seed at the bottom"), &setup_.live_seed_side)) {
            for (auto& v : slot_views_) v.turned_for = ~std::size_t{0};  // turned at once
            saveSetupSoon();
        }
        ui::help(
            TR("Each new game of the gauntlet is shown from the side of its seed, the engine that meets all the "
               "others. Flip still turns the board over."));
        ImGui::SameLine(0, px(24));
    }

    // The game followed filling the window, for a television, a club or a stream.
    if (ImGui::Button(TR("Big screen"))) startBigScreen(live_slot_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The game followed filling the window, in large type, for a television, a club or a "
                                   "stream. F11: full screen, Esc: back."));
    }
    // The side the scores of this tab are from, at the right of the line.
    {
        const char* mover = TR("Score of the side to move");
        const char* white = TR("Score of White");
        ImGui::SameLine();
        const float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - segmentedWidth(mover, white);
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x));
        if (segmented("##score_side", mover, white, setup_.live_white_score)) saveSetupSoon();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("The engines' scores in this tab: from the side to move's point of view, as "
                                       "fastchess writes them in the PGN, or from White's. The evaluation bar and the "
                                       "big screen are always from White's side."));
        }
    }
    if (mosaic) ImGui::SameLine(0, px(24));  // the line ends below; the tabs of one game go under it

    if (!mosaic) {
        if (ImGui::BeginTabBar("##slots", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton)) {
            for (const int i : live_shown_) {
                const bool active = obs->slotActive(i);
                if (!active) ImGui::PushStyleColor(ImGuiCol_Text, ui::MUTED4);
                const auto label = fmt::format("#{}###slot{}", i + 1, i);
                if (ImGui::BeginTabItem(label.c_str())) {
                    live_slot_ = i;
                    ImGui::EndTabItem();
                }
                if (!active) ImGui::PopStyleColor();
            }
            ImGui::EndTabBar();
        }
        if (parts != 0) drawLiveGame(slot_views_[static_cast<std::size_t>(live_slot_)], "##focus", parts, live_slot_);
    } else {
        ImGui::NewLine();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float gap    = ImGui::GetStyle().ItemSpacing.x;
        if (parts == 0) {
            // The followed game in panels of its own: the mosaic takes the whole room.
            const auto grid = mosaicGrid(shown, avail, setup_.tile_info);
            ImGui::BeginChild("##mosaic", ImVec2(0, 0));
            drawMosaic(grid.cols, grid.card);
            ImGui::EndChild();
        } else if (avail.x < px(700)) {
            // Narrow: the mosaic above, the followed game under it.
            const ImVec2 strip(avail.x, std::floor(avail.y * 0.4f));
            const auto grid = mosaicGrid(shown, strip, setup_.tile_info);
            ImGui::BeginChild("##mosaic", ImVec2(0, strip.y));
            drawMosaic(grid.cols, grid.card);
            ImGui::EndChild();
        } else {
            // The followed game gets the width its board needs at full height, beside its panel, within 40 to 70 % of
            // the width; the mosaic the rest, less what its cards leave unused. Without its board, the width of the
            // panel beside it.
            const bool board = parts & LiveBoard;
            const bool side  = parts & ~LiveBoard;
            const float want = (board ? avail.y - ImGui::GetTextLineHeightWithSpacing() - liveNavHeight() : 0.0f) +
                               (board && side ? gap : 0.0f) + (side ? sidePanel() : 0.0f);
            float focus = board ? std::clamp(want, avail.x * 0.4f, avail.x * 0.7f) : std::min(want, avail.x * 0.5f);
            auto grid   = mosaicGrid(shown, ImVec2(avail.x - focus - gap, avail.y), setup_.tile_info);
            // Cards that would need a scroll bar: the mosaic takes from the followed board, down to 40 % of the width
            // for it, until they fit, or as wide as it can.
            for (float f = focus - px(16); grid.scrolls && board && f >= avail.x * 0.4f - 0.5f; f -= px(16)) {
                focus = std::max(f, avail.x * 0.4f);
                grid  = mosaicGrid(shown, ImVec2(avail.x - focus - gap, avail.y), setup_.tile_info);
            }
            ImGui::BeginChild("##mosaic", ImVec2(std::min(grid.width(), avail.x - focus - gap), 0));
            drawMosaic(grid.cols, grid.card);
            ImGui::EndChild();
            ImGui::SameLine();
        }
        if (parts != 0) {
            ImGui::BeginChild("##focus_panel", ImVec2(0, 0));
            drawLiveGame(slot_views_[static_cast<std::size_t>(live_slot_)], "##focus", parts, live_slot_);
            ImGui::EndChild();
        }
    }
    // A, the mouse over no board: the game followed. The arrows, Home and End step through it.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (boardKey()) analyseLiveGame(live_slot_);
        if (live_slot_ >= 0 && live_slot_ < static_cast<int>(slot_views_.size()))
            liveNavKeys(slot_views_[static_cast<std::size_t>(live_slot_)]);
    }

    ui::endPanel();
}

void App::drawMosaic(int cols, float card) {
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const float h    = cardHeight(card, setup_.tile_info);

    const auto now = Clock::now();
    for (std::size_t k = 0; k < live_shown_.size(); ++k) {
        const int i    = live_shown_[k];
        auto& v        = slot_views_[static_cast<std::size_t>(i)];
        const auto& st = v.state;
        if (k % static_cast<std::size_t>(cols) != 0) ImGui::SameLine();

        const bool selected = i == live_slot_;
        ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImGui::ColorConvertU32ToFloat4(ui::ACCENT)
                                                        : ImGui::ColorConvertU32ToFloat4(ui::CARD_BD));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::CARD_BG));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(6), px(5)));
        ImGui::BeginChild(fmt::format("##card{}", i).c_str(), ImVec2(card, h),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const float inner   = ImGui::GetContentRegionAvail().x;
        const auto elapsed  = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.think_start).count();
        const auto name_row = [&](chess::Color color) {
            const auto& name = color == chess::Color::WHITE ? st.white : st.black;
            const auto clock = clockText(st, color, elapsed);
            const bool moves = st.active && st.stm == color;
            ImGui::PushStyleColor(ImGuiCol_Text, moves ? ui::ACCENT : ImGui::GetColorU32(ImGuiCol_Text));
            // A name too long for the card ends with "…" before the clock.
            const char* text = st.has_game ? name.c_str() : "—";
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float end  = pos.x + inner - ImGui::CalcTextSize(clock.c_str()).x - px(6);
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, ImVec2(end, pos.y + line), end, text, nullptr,
                                      nullptr);
            ImGui::Dummy(ImVec2(end - pos.x, ImGui::GetTextLineHeight()));
            ImGui::PopStyleColor();
            if (!clock.empty()) {
                ImGui::SameLine(inner - ImGui::CalcTextSize(clock.c_str()).x + ImGui::GetStyle().WindowPadding.x);
                ImGui::TextDisabled("%s", clock.c_str());
            }
        };

        const auto top    = v.flip ? chess::Color::WHITE : chess::Color::BLACK;
        const auto bottom = v.flip ? chess::Color::BLACK : chess::Color::WHITE;
        name_row(top);
        if (st.has_game) {
            BoardDecor decor;
            decor.flipped    = v.flip;
            decor.eval_white = -1.0f;
            if (!st.moves.empty()) decor.last_move = st.moves.back().move;
            drawBoard(atlas_, v.board, inner, decor);
        } else {
            ImGui::Dummy(ImVec2(inner, inner));
        }
        name_row(bottom);
        if (setup_.tile_info) {
            if (st.has_game) {
                drawBoardInfo(atlas_, v.board, inner, tbPieces());
            } else {
                ImGui::Dummy(ImVec2(inner, ImGui::GetTextLineHeight()));
            }
        }

        // A thin evaluation bar and the move number.
        {
            ImDrawList* dl  = ImGui::GetWindowDrawList();
            const auto pos  = ImGui::GetCursorScreenPos();
            const float f   = st.has_game ? evalBar(st) : 0.5f;
            const float bar = px(4);
            dl->AddRectFilled(pos, ImVec2(pos.x + inner, pos.y + bar), IM_COL32(25, 25, 25, 255), bar);
            dl->AddRectFilled(pos, ImVec2(pos.x + inner * f, pos.y + bar), IM_COL32(236, 236, 236, 255), bar);
            dl->AddRect(pos, ImVec2(pos.x + inner, pos.y + bar), IM_COL32(130, 130, 130, 255), bar);  // seen on white
            ImGui::Dummy(ImVec2(inner, bar));
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        if (ImGui::IsItemClicked()) live_slot_ = i;
        // A: this game on the Board page, analysed.
        if (ImGui::IsItemHovered() && st.has_game && boardKey()) analyseLiveGame(i);
        if (ImGui::IsItemHovered() && st.has_game) {
            ImGui::SetTooltip(
                "%s – %s\n%s%s", st.white.c_str(), st.black.c_str(),
                trf("Game {} · move {}", st.game_id, (st.moves.size() + 1) / 2).c_str(),
                st.active ? (std::string("\n") + TR("Right click: end this game with a result.")).c_str() : "");
        }
        if (st.active && ImGui::BeginPopupContextItem(fmt::format("##end_game{}", i).c_str())) {
            drawEndGameMenu(i);
            ImGui::EndPopup();
        }
    }
}

void App::drawEndGameMenu(int slot) {
    if (slot < 0 || slot >= static_cast<int>(slot_views_.size())) return;
    const auto& st = slot_views_[static_cast<std::size_t>(slot)].state;
    if (!st.active || !st.has_game) {
        ImGui::TextDisabled("%s", TR("No game is being played here."));
        return;
    }
    if (end_menu_.game != st.game_id) {
        end_menu_      = {};
        end_menu_.game = st.game_id;
    }
    ImGui::TextDisabled(
        "%s", trf("Game {} · move {}: {} – {}", st.game_id, (st.moves.size() + 2) / 2, st.white, st.black).c_str());
    ImGui::TextUnformatted(TR("End with result"));
    ui::help(
        TR("For a game the program would not end well: a hang it does not catch, a won ending the engines keep "
           "shuffling.\nThe engine on move is stopped (it starts again for its next game), and the game counts with "
           "this result: Termination \"adjudicated by the operator\" in the exports, the reason in a comment.\nIts "
           "result can be changed afterwards in the Games tab."));
    static constexpr const char* RESULTS[] = {"1-0", "1/2-1/2", "0-1"};
    for (const char* result : RESULTS) {
        if (result != RESULTS[0]) ImGui::SameLine();
        if (ImGui::RadioButton(result == RESULTS[1] ? "½-½" : result, end_menu_.result == result)) {
            end_menu_.result = result;
        }
    }
    {
        char buffer[160];
        const auto n = std::min(end_menu_.why.size(), sizeof(buffer) - 1);
        std::copy_n(end_menu_.why.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(px(300));
        if (ImGui::InputTextWithHint("##end_why", TR("Why (kept with the game)"), buffer, sizeof(buffer))) {
            end_menu_.why = buffer;
        }
    }
    if (!end_menu_.result.empty()) {
        // What it changes: the points each player gets from the game.
        const double white = end_menu_.result == "1-0" ? 1.0 : end_menu_.result == "0-1" ? 0.0 : 0.5;
        const auto points  = [](double p) { return p == 0.5 ? std::string("+½") : fmt::format("+{}", p); };
        ImGui::TextUnformatted(
            fmt::format("{} {} · {} {}", st.white, points(white), st.black, points(1.0 - white)).c_str());
        if (ImGui::Button(TR("End the game"))) {
            end_menu_.error = run_->endGame(slot, st.game_id - 1, end_menu_.result, end_menu_.why);
            if (end_menu_.error.empty()) {
                Console::instance().add(trf("Game {} was ended by the operator: {}.", st.game_id,
                                            end_menu_.result == "1/2-1/2" ? "½-½" : end_menu_.result));
                end_menu_ = {};
                ImGui::CloseCurrentPopup();
            }
        }
    }
    if (!end_menu_.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(320));
        ImGui::TextUnformatted(end_menu_.error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
}

float App::liveNavHeight() const { return setup_.nav_buttons ? boardNavHeight() : 0.0f; }

void App::liveNavKeys(SlotView& view) {
    if (!view.state.has_game) return;
    view.nav.step(navKeys(view.flip), static_cast<int>(view.state.moves.size()));
}

// The board as large as `size`, with the 50-move counter and the material under it, and the buttons through the moves.
// Stepped back, it shows that earlier position while the game goes on.
void App::drawLiveBoard(SlotView& view, float size) {
    const auto& st  = view.state;
    const int total = static_cast<int>(st.moves.size());
    const bool held = view.nav.held(total);
    const int ply   = view.nav.shown(total);
    chess::Board earlier;
    if (held) {
        earlier.setFen(st.start_fen);
        for (int i = 0; i < ply; ++i) earlier.makeMove<true>(st.moves[static_cast<std::size_t>(i)].move);
    }
    const chess::Board& board = held ? earlier : view.board;

    ImGui::BeginGroup();
    ImGui::BeginGroup();
    BoardDecor decor;
    decor.flipped    = view.flip;
    decor.eval_white = evalBar(st, ply);
    if (ply > 0) decor.last_move = st.moves[static_cast<std::size_t>(ply - 1)].move;
    if (setup_.live_arrows && !held) decor.arrows = pvArrows(st, view.board);
    drawBoard(atlas_, board, size, decor);
    // Without the buttons, the way back to the game over the board.
    if (held && !setup_.nav_buttons && drawBackOverBoard()) view.nav.follow();
    // A: this game on the Board page, analysed.
    if (ImGui::IsItemHovered() && st.has_game && boardKey()) {
        analyseLiveGame(static_cast<int>(&view - slot_views_.data()));
    }
    drawBoardInfo(atlas_, board, size, tbPieces());
    ImGui::EndGroup();
    // Right click on the board: the game ended with a result. Asked from the palette, once the panel is in front.
    const int slot = static_cast<int>(&view - slot_views_.data());
    if (open_end_menu_ == slot + 1 && !panel_request_ && !tab_request_) {
        open_end_menu_ = 0;
        ImGui::OpenPopup("##end_game");
    }
    if (st.active && ImGui::BeginPopupContextItem("##end_game")) {
        drawEndGameMenu(slot);
        ImGui::EndPopup();
    }
    if (setup_.nav_buttons) {
        const auto click = drawBoardNav("##live_nav", ply, total, view.flip, true, st.active, size);
        view.nav.step(click.step, total);
        if (click.hide) {
            setup_.nav_buttons = false;
            saveSetupSoon();
        }
    }
    ImGui::EndGroup();
}

// The board alone, as large as the room allows, in the middle of it.
void App::drawLiveBoardFitted(SlotView& view) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float info_h = ImGui::GetTextLineHeightWithSpacing() + liveNavHeight();
    const float size   = std::floor(std::max(px(120), std::min(avail.x, avail.y - info_h - 4.0f)));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - size) * 0.5f));
    drawLiveBoard(view, size);
}

// A player: the name and the clock, and with `search` what its engine is searching (else what its last move
// reported). In a large panel, a card (drawLivePlayerCard).
float App::drawLivePlayer(SlotView& view, chess::Color color, bool search, float pv_room) {
    if (const float width = ImGui::GetContentRegionAvail().x; largePanel(width))
        return drawLivePlayerCard(view, color, search, width, pv_room);
    auto* obs          = run_->observer();
    const auto& st     = view.state;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - st.think_start).count();
    const bool white   = color == chess::Color::WHITE;
    const auto& name   = white ? st.white : st.black;
    const bool to_move = st.active && st.stm == color;
    const bool thinks  = to_move && st.thinking;

    // A small disc in the side's colour.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r  = ImGui::GetTextLineHeight() * 0.32f;
    const ImVec2 c(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f);
    dl->AddCircleFilled(c, r, white ? IM_COL32(236, 236, 236, 255) : IM_COL32(25, 25, 25, 255), 16);
    dl->AddCircle(c, r, IM_COL32(130, 130, 130, 255), 16);
    ImGui::Dummy(ImVec2(r * 2, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    if (appearance::current().engine_logos && drawEngineLogo(name, ImGui::GetTextLineHeight()) > 0) ImGui::SameLine();

    if (to_move) ImGui::PushStyleColor(ImGuiCol_Text, ui::ACCENT);
    if (isMatch() && obs) {
        ImGui::Text("%s  (%s)", name.c_str(), name == obs->engineA() ? "A" : "B");
    } else {
        ImGui::TextUnformatted(name.c_str());
    }
    if (to_move) ImGui::PopStyleColor();

    // Clock on the right.
    const auto clock = clockText(st, color, elapsed);
    if (!clock.empty()) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(clock.c_str()).x);
        if (thinks) ImGui::PushStyleColor(ImGuiCol_Text, ui::ACCENT);
        ImGui::TextUnformatted(clock.c_str());
        if (thinks) ImGui::PopStyleColor();
    }
    if (!search) return 0.0f;

    // Search info: the running search for the side to move, else what its last move reported. Wrapped in a
    // narrow panel. The tablebase hits and the hash fill when the engine sends them. It keeps the most rows it has
    // taken in this game, so that what is under it does not move with each line (#193).
    const float search_top = ImGui::GetCursorPosY();
    ImGui::PushTextWrapPos(0);
    const auto& line = st.info[white ? 0 : 1];
    const auto info  = line.empty() ? fastchess::engine::UciInfo{} : fastchess::engine::UciEngine::parseInfo(line);
    const auto extra = [&info] {
        std::string out;
        if (info.tbhits) out += "  " + trf("tb {}", ui::formatCount(*info.tbhits));
        if (info.hashfull) out += "  " + trf("hash {} %", *info.hashfull / 10);
        return out;
    };
    if (thinks && !line.empty()) {
        const auto score = info.score ? std::optional<fastchess::Score>(*info.score) : std::nullopt;
        const auto figures =
            fmt::format("d{}/{}  {}  {} n  {} n/s  {}{}", info.depth.value_or(0), info.seldepth.value_or(0),
                        ui::formatScore(liveScore(score, color)), ui::formatCount(info.nodes.value_or(0)),
                        ui::formatCount(info.nps.value_or(0)), ui::formatClock(elapsed), extra());
        ImGui::TextColored(ui::MUTED4, "%s", figures.c_str());
        if (info.wdl) wdlLine(*info.wdl);
        const auto pv = pvToSan(view.board, info.pv, 12);
        ImGui::TextUnformatted(pv.empty() ? " " : pv.c_str());
    } else {
        const LiveMove* last = nullptr;
        for (auto it = st.moves.rbegin(); it != st.moves.rend(); ++it) {
            if (it->mover == color && !it->book) {
                last = &*it;
                break;
            }
        }
        if (last) {
            const auto decision =
                last->decision_ms ? trf(" (decided at {})", ui::formatClock(*last->decision_ms)) : std::string();
            const auto figures =
                fmt::format("d{}/{}  {}  {} n  {} n/s  {}{}{}", last->depth, last->seldepth,
                            ui::formatScore(liveScore(last->score, color)), ui::formatCount(last->nodes),
                            ui::formatCount(last->nps), ui::formatClock(last->time_ms), decision, extra());
            ImGui::TextColored(ui::MUTED4, "%s", figures.c_str());
            if (last->wdl) wdlLine(*last->wdl);
        } else {
            ImGui::TextDisabled("—");
        }
        ImGui::TextUnformatted(" ");
    }
    ImGui::PopTextWrapPos();
    auto& kept       = view.search_h[white ? 0 : 1];
    const float used = ImGui::GetCursorPosY() - search_top;
    keepHeight(search_top, kept);
    kept = std::max(kept, used);
    return 0.0f;
}

float App::drawLivePlayerCard(SlotView& view, chess::Color color, bool search, float width, float pv_room) {
    auto* obs          = run_->observer();
    const auto& st     = view.state;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - st.think_start).count();
    const bool white   = color == chess::Color::WHITE;
    const bool to_move = st.active && st.stm == color;
    const bool thinks  = to_move && st.thinking;
    // Its last move, and the last that its engine searched (a book move has no search).
    int last = -1, searched = -1;
    for (int i = static_cast<int>(st.moves.size()) - 1; i >= 0 && searched < 0; --i) {
        const auto& m = st.moves[static_cast<std::size_t>(i)];
        if (m.mover != color) continue;
        if (last < 0) last = i;
        if (!m.book) searched = i;
    }

    // The card, framed in the accent colour for the side to move.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::CARD_BG));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(to_move ? ui::ACCENT : ui::CARD_BD));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(10));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, to_move ? std::max(1.0f, px(2)) : 1.0f);
    ImGui::BeginChild(white ? "##card_white" : "##card_black", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // The card keeps its height through the game (#193): its line the most rows a line has needed, the rest (a row of
    // chips, the W/D/L line, what it said when it played) the most it has taken.
    const float card_top = ImGui::GetCursorPosY();

    // The header: the side's disc, the logo, the name and what the side does, the clock at the right.
    ImDrawList* dl       = ImGui::GetWindowDrawList();
    ImFont* font         = ImGui::GetFont();
    ImFont* bold         = ui::titleFont() ? ui::titleFont() : font;
    const float fs       = ImGui::GetFontSize();
    const float inner    = ImGui::GetContentRegionAvail().x;
    const float name_px  = fs * 1.15f;
    const float clock_px = fs * 1.75f;
    const float line_h   = ImGui::GetTextLineHeight();
    const float name_h   = bold->CalcTextSizeA(name_px, FLT_MAX, 0.0f, "Ag").y;
    const float header_h = std::max(name_h + line_h + px(2), bold->CalcTextSizeA(clock_px, FLT_MAX, 0.0f, "0:").y);
    const ImVec2 p       = ImGui::GetCursorScreenPos();
    float x              = p.x;
    const float r        = fs * 0.32f;
    dl->AddCircleFilled(ImVec2(x + r, p.y + header_h / 2), r,
                        white ? IM_COL32(236, 236, 236, 255) : IM_COL32(25, 25, 25, 255), 16);
    dl->AddCircle(ImVec2(x + r, p.y + header_h / 2), r, IM_COL32(130, 130, 130, 255), 16);
    x += 2 * r + px(8);
    // The logo grows with the panel, up to the height of the name and what follows it.
    const auto& name = white ? st.white : st.black;
    if (const auto* logo = appearance::current().engine_logos ? engineLogo(name) : nullptr;
        logo && logo->width > 0 && logo->height > 0) {
        const float high = std::clamp(line_h + (width - px(440)) * 0.12f, line_h, header_h);
        const float w    = std::min(high * logo->width / logo->height, high * 2.5f);
        const float h    = w * logo->height / logo->width;
        dl->AddImage(logo->texture(h * ImGui::GetIO().DisplayFramebufferScale.y), ImVec2(x, p.y + (header_h - h) / 2),
                     ImVec2(x + w, p.y + (header_h + h) / 2));
        x += w + px(10);
    }
    const auto clock     = clockText(st, color, elapsed);
    const float clock_w  = clock.empty() ? 0.0f : bold->CalcTextSizeA(clock_px, FLT_MAX, 0.0f, clock.c_str()).x;
    const float name_y   = p.y + (header_h - name_h - line_h) / 2;
    const float name_end = p.x + inner - clock_w - px(12);
    {
        std::string shown = name;
        if (isMatch() && obs) shown += name == obs->engineA() ? "  (A)" : "  (B)";
        ImGui::PushFont(bold, ImGui::GetStyle().FontSizeBase * 1.15f);  // name_px, before the interface scale
        ImGui::RenderTextEllipsis(dl, ImVec2(x, name_y), ImVec2(name_end, name_y + name_h), name_end, shown.c_str(),
                                  nullptr, nullptr);
        ImGui::PopFont();
        const char* side = white ? TR("White") : TR("Black");
        std::string doing;
        if (thinks) {
            doing = trf("{} · thinking", side);
        } else if (to_move) {
            doing = trf("{} · to move", side);
        } else if (last >= 0) {
            doing = trf("{} · played {}", side, numberedSan(st, static_cast<std::size_t>(last)));
        } else {
            doing = side;
        }
        ImGui::RenderTextEllipsis(dl, ImVec2(x, name_y + name_h + px(2)),
                                  ImVec2(name_end, name_y + name_h + px(2) + line_h), name_end, doing.c_str(), nullptr,
                                  nullptr);
    }
    if (!clock.empty()) {
        const float clock_h = bold->CalcTextSizeA(clock_px, FLT_MAX, 0.0f, clock.c_str()).y;
        dl->AddText(bold, clock_px, ImVec2(p.x + inner - clock_w, p.y + (header_h - clock_h) / 2),
                    thinks ? ui::ACCENT : ImGui::GetColorU32(ImGuiCol_Text), clock.c_str());
    }
    ImGui::Dummy(ImVec2(inner, header_h));

    float pv_h = 0.0f;
    if (search) {
        // Its search: the running one for the side to move, else what its last move reported, figure by figure; the
        // tablebase hits and the hash fill when the engine sends them.
        const auto& line = st.info[white ? 0 : 1];
        const auto info  = line.empty() ? fastchess::engine::UciInfo{} : fastchess::engine::UciEngine::parseInfo(line);
        std::optional<fastchess::Score> score;
        std::optional<std::array<int64_t, 3>> wdl;
        int64_t depth = 0, seldepth = 0, spent = 0;
        uint64_t nodes = 0, nps = 0;
        std::string pv;
        const bool running = thinks && !line.empty();
        if (running) {
            if (info.score) score = *info.score;
            depth    = info.depth.value_or(0);
            seldepth = info.seldepth.value_or(0);
            nodes    = info.nodes.value_or(0);
            nps      = info.nps.value_or(0);
            spent    = elapsed;
            wdl      = info.wdl;
            pv       = pvToSan(view.board, info.pv, info.pv.size());
        } else if (searched >= 0) {
            const auto& m = st.moves[static_cast<std::size_t>(searched)];
            score         = m.score;
            depth         = m.depth;
            seldepth      = m.seldepth;
            nodes         = m.nodes;
            nps           = m.nps;
            spent         = m.time_ms;
            wdl           = m.wdl;
            std::vector<std::string> moves;
            std::istringstream words(m.pv);
            for (std::string w; words >> w;) moves.push_back(w);
            pv = pvToSan(slotBoard(st, searched), moves, moves.size());
        }
        if (running || searched >= 0) {
            std::vector<std::pair<std::string, std::string>> figures;
            figures.emplace_back(TR("Depth"), fmt::format("{}/{}", depth, seldepth));
            figures.emplace_back(setup_.live_white_score ? TR("Score · White") : TR("Score · to move"),
                                 score ? ui::formatScore(liveScore(score, color)) : std::string("—"));
            figures.emplace_back(TR("Nodes"), ui::formatCount(nodes));
            figures.emplace_back(TR("Speed"), trf("{} n/s", ui::formatCount(nps)));
            if (info.tbhits) figures.emplace_back(TR("TB hits"), ui::formatCount(*info.tbhits));
            if (info.hashfull) figures.emplace_back(TR("Hash"), fmt::format("{} %", *info.hashfull / 10));
            figures.emplace_back(TR("This move"), ui::formatClock(spent));
            figureChips(figures);
            if (wdl) wdlLine(*wdl);
            // The whole line, wrapped, in a larger type; as much of it as the room left to the moves allows. It keeps
            // the most rows a line has needed in this game, a shorter line leaving them blank.
            {
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.08f);
                const float row  = ImGui::GetFontSize();
                const int room   = pv_room >= FLT_MAX / 2 ? 1000 : std::max(1, static_cast<int>(pv_room / row));
                const float wide = ImGui::GetContentRegionAvail().x;
                const auto shown = pv.empty() ? std::string() : withinLines(pv, wide, room);
                const int used = shown.empty() ? 0
                                               : static_cast<int>(std::lround(
                                                     ImGui::CalcTextSize(shown.c_str(), nullptr, false, wide).y / row));
                auto& rows     = view.pv_rows[white ? 0 : 1];
                rows           = std::min(room, std::max(rows, used));
                const float top = ImGui::GetCursorPosY();
                if (!shown.empty()) {
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextUnformatted(shown.c_str());
                    ImGui::PopTextWrapPos();
                }
                keepHeight(top, rows > 0 ? rows * row + ImGui::GetStyle().ItemSpacing.y : 0.0f);
                pv_h = ImGui::GetCursorPosY() - top;
                ImGui::PopFont();
            }
            // What it said when it played; while it thinks, the room of that sentence kept.
            const LiveMove* told = searched >= 0 ? &st.moves[static_cast<std::size_t>(searched)] : nullptr;
            const auto said      = told && told->decision_ms ? trf("What it said when it played; decided at {}.",
                                                                   ui::formatClock(*told->decision_ms))
                                                             : std::string(TR("What it said when it played."));
            if (!running) {
                ImGui::PushTextWrapPos(0);
                ImGui::TextColored(ui::MUTED4, "%s", said.c_str());
                ImGui::PopTextWrapPos();
            } else {
                const float h = ImGui::CalcTextSize(said.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x).y;
                keepHeight(ImGui::GetCursorPosY(), h + ImGui::GetStyle().ItemSpacing.y);
            }
        } else {
            ImGui::TextDisabled("—");
        }
    }
    // The rest of the card, besides its line, keeps the most it has taken: the line's rows are counted apart, so that
    // the room the line is given does not shrink with the blank kept here.
    auto& kept       = view.card_h[white ? 0 : 1];
    const float used = ImGui::GetCursorPosY() - card_top - pv_h;
    if (used < kept) keepHeight(card_top, kept + pv_h);
    kept = std::max(kept, used);
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    return pv_h;
}

// The game and round, the result once over, and the board's buttons: Detach (not in a detached board), Arrows, and Flip
// when the buttons under the board, which have it, are hidden.
void App::drawLiveGameLine(SlotView& view, int slot, bool allow_detach) {
    const auto& st = view.state;
    ImGui::Text(TR("Game %zu · round %zu"), st.game_id, st.round_id + 1);
    if (!st.active && !st.result.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "%s  %s", st.result.c_str(), TR(st.reason));
    }
    const auto check_w = [](const char* label) {
        return ImGui::CalcTextSize(label).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
    };
    const bool flip      = !setup_.nav_buttons;
    const float flip_w   = flip ? check_w(TR("Flip")) + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    const float arrows_w = check_w(TR("Arrows"));
    // Detach, or in a detached board, Big screen.
    const float detach_w = ImGui::CalcTextSize(allow_detach ? TR("Detach") : TR("Big screen")).x +
                           ImGui::GetStyle().FramePadding.x * 2 + px(8);
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < flip_w + arrows_w + detach_w) {
        ImGui::NewLine();  // on a line of their own in a narrow panel
    } else {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - flip_w - arrows_w - detach_w);
    }
    if (allow_detach) {
        if (ImGui::SmallButton(TR("Detach"))) showPanel(layout::detachedPanel(slot));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("Show this game in a panel of its own, which can be docked beside the others "
                                       "or moved to another screen."));
        }
        ImGui::SameLine();
    } else {
        if (ImGui::SmallButton(TR("Big screen"))) startBigBoard(slot);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("This game filling its window, in large type: on another screen (a "
                                       "television), the panel's window; docked here, the main window. F11: full "
                                       "screen, Esc: back."));
        }
        ImGui::SameLine();
    }
    if (ImGui::Checkbox(TR("Arrows"), &setup_.live_arrows)) saveSetupSoon();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The move the side to move is thinking of, and the reply it expects, in the "
                                   "colours of the engines that would play them."));
    }
    if (flip) {
        ImGui::SameLine();
        ImGui::Checkbox(TR("Flip"), &view.flip);
    }
}

std::optional<fastchess::Score> App::liveScore(std::optional<fastchess::Score> score, chess::Color mover) const {
    if (score && setup_.live_white_score && mover == chess::Color::BLACK) score->value = -score->value;
    return score;
}

const TableVerdict* App::liveVerdict(const chess::Board& board) {
    // Seven pieces at most and no castling: what Syzygy tables can know.
    if (board.occ().count() > 7 || !board.castlingRights().isEmpty()) return nullptr;
    const auto* t = run_->tournament();
    if (!t) return nullptr;
    std::string paths;
    bool ignore50  = false;
    const auto& tb = t->spec().conditions.tb_adjudication;
    if (tb.enabled) {
        paths    = tb.syzygy_dirs;
        ignore50 = tb.ignore_50_move_rule;
    } else {
        for (const auto& [option, value] : t->spec().forced_options)
            if (option == "SyzygyPath") paths = value;
    }
    if (paths.empty()) return nullptr;
    // A tournament on a thread of this process has the tables of the process for its games.
    if (run_->running() && !run_->remote()) return nullptr;

    auto& lt = live_tables_;
    if (lt.job.valid() && lt.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto verdict = lt.job.get();
        if (lt.known.size() > 50'000) lt.known.clear();
        lt.known[lt.job_fen] = verdict;
    }
    if (paths != lt.paths || ignore50 != lt.ignore50) {
        lt.known.clear();
        lt.paths    = paths;
        lt.ignore50 = ignore50;
    }
    auto fen = board.getFen();
    if (const auto it = lt.known.find(fen); it != lt.known.end()) return it->second ? &*it->second : nullptr;
    if (!lt.job.valid()) {
        lt.job_fen = std::move(fen);
        lt.job     = std::async(std::launch::async, [board, paths, ignore50] {
            const auto lock = lockSyzygy();
            return probeTables(board, paths, ignore50);
        });
    }
    return nullptr;
}

void App::drawLiveFacts(SlotView& view) {
    const auto& st      = view.state;
    const auto& opening = view.opening;
    ImGui::PushTextWrapPos(0);
    if (opening.name) {
        ImGui::PushFont(ui::titleFont(), 0.0f);
        ImGui::TextUnformatted(opening.name->eco.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        auto text = opening.name->name;
        if (opening.last_book >= 0 && opening.last_book < static_cast<int>(st.moves.size())) {
            text += "  ·  " + trf("book up to {}", numberedSan(st, static_cast<std::size_t>(opening.last_book)));
        }
        ImGui::TextUnformatted(text.c_str());
    }
    // The tables' verdict on the position shown.
    const int total     = static_cast<int>(st.moves.size());
    const int ply       = view.nav.shown(total);
    const auto* verdict = liveVerdict(ply == total ? view.board : slotBoard(st, ply));
    if (verdict) {
        const chess::Color stm = (ply == total ? view.board : slotBoard(st, ply)).sideToMove();
        const bool white_wins  = (verdict->result > 0) == (stm == chess::Color::WHITE);
        std::string headline, detail;
        if (verdict->result == 0) {
            headline = TR("Tablebases: draw");
        } else if (verdict->rule50) {
            headline = TR("Tablebases: draw by the 50-move rule");
            detail   = white_wins ? TR("White wins without it") : TR("Black wins without it");
        } else {
            headline = white_wins ? TR("Tablebases: White wins") : TR("Tablebases: Black wins");
        }
        if (verdict->result != 0 && verdict->dtz) {
            if (!detail.empty()) detail += ", ";
            detail += trf("DTZ {}: half-moves to a capture or a pawn move", *verdict->dtz);
        }
        // A disc: green when a side wins.
        const float r  = ImGui::GetTextLineHeight() * 0.22f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() / 2), r,
                                                    verdict->result != 0 && !verdict->rule50 ? ui::GOOD : ui::MUTED,
                                                    12);
        ImGui::Dummy(ImVec2(2 * r, ImGui::GetTextLineHeight()));
        ImGui::SameLine();
        ImGui::PushFont(ui::titleFont(), 0.0f);
        ImGui::TextUnformatted(headline.c_str());
        ImGui::PopFont();
        if (!detail.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ui::MUTED4, "· %s", detail.c_str());
        }
    }
    ImGui::PopTextWrapPos();
}

// The moves played, kept scrolled to the last one while the list is at its end. Each engine move with its score, depth
// and time, the reply it expected on hover; the book moves marked, then where the book ends. A click on a move shows
// the position after it on the board; the wheel scrolls the list.
void App::drawLiveMoves(SlotView& view, float height) {
    const auto& st   = view.state;
    const int total  = static_cast<int>(st.moves.size());
    const bool held  = view.nav.held(total);
    const int shown  = view.nav.shown(total);
    const bool moved = shown != view.listed_ply;  // keys or buttons: the move shown kept in sight
    const float top  = ImGui::GetCursorPosY();
    drawLiveFacts(view);
    height = std::max(ImGui::GetFrameHeight(), height - (ImGui::GetCursorPosY() - top));
    ImGui::BeginChild("##moves", ImVec2(0, height), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##movetable", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000.").x);
        ImGui::TableSetupColumn(TR("White"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Black"), ImGuiTableColumnFlags_WidthStretch);

        chess::Board start;
        start.setFen(st.start_fen);
        const int offset    = start.sideToMove() == chess::Color::BLACK ? 1 : 0;  // "1… e5" fills a row
        const int last_book = view.opening.last_book;
        // The move being thought of, after the last one.
        const bool thinking = st.active && st.thinking;
        const int plies     = total + (thinking ? 1 : 0);

        // What the engine said when it played the move, and the reply it expected: the second move of its line, where
        // its pondering would have started (fastchess plays without it).
        const auto tip = [&](int index) {
            const auto& m = st.moves[static_cast<std::size_t>(index)];
            auto text     = fmt::format("{} · {} · {} d{}/{} · {}", numberedSan(st, static_cast<std::size_t>(index)),
                                        m.mover == chess::Color::WHITE ? st.white : st.black,
                                        ui::formatScore(liveScore(m.score, m.mover)), m.depth, m.seldepth,
                                        ui::formatClock(m.time_ms));
            if (m.decision_ms) text += trf(", decided at {}", ui::formatClock(*m.decision_ms));
            std::istringstream words(m.pv);
            std::string played, reply;
            words >> played >> reply;
            if (!reply.empty()) {
                auto board      = slotBoard(st, index + 1);
                const auto move = chess::uci::uciToMove(board, reply);
                chess::Movelist legal;
                chess::movegen::legalmoves(legal, board);
                if (move != chess::Move::NO_MOVE && std::find(legal.begin(), legal.end(), move) != legal.end()) {
                    const auto san = fmt::format("{}{}{}", board.fullMoveNumber(),
                                                 board.sideToMove() == chess::Color::WHITE ? "." : "…",
                                                 chess::uci::moveToSan(board, move));
                    text += "\n" + trf("Expected reply: {}", san);
                }
            }
            ImGui::SetTooltip("%s", text.c_str());
        };
        const auto cell = [&](int index) {
            if (index == total) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "…  %s", TR("thinking"));
                return;
            }
            const auto& m      = st.moves[static_cast<std::size_t>(index)];
            const bool current = held && index + 1 == shown;
            if (m.book) ImGui::PushStyleColor(ImGuiCol_Text, ui::MUTED4);
            const auto label = fmt::format("{}##lm{}", m.san, index);
            if (ImGui::Selectable(label.c_str(), current, ImGuiSelectableFlags_None,
                                  ImGui::CalcTextSize(m.san.c_str())))
                view.nav.show(index + 1, total);
            if (m.book) ImGui::PopStyleColor();
            if (!m.book && ImGui::IsItemHovered()) tip(index);
            if (current && moved && !ImGui::IsItemVisible()) ImGui::SetScrollHereY();
            ImGui::SameLine();
            if (m.book) {
                ImGui::PushFont(ui::italicFont(), 0.0f);
                ImGui::TextColored(ui::MUTED4, "%s", TR("book"));
                ImGui::PopFont();
            } else {
                ImGui::TextColored(ui::MUTED4, "%s d%lld · %s", ui::formatScore(liveScore(m.score, m.mover)).c_str(),
                                   static_cast<long long>(m.depth), ui::formatClock(m.time_ms).c_str());
            }
        };

        const int rows = (plies + offset + 1) / 2;
        for (int r = 0; r < rows; ++r) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d.", start.fullMoveNumber() + r);
            for (int c = 0; c < 2; ++c) {
                ImGui::TableNextColumn();
                const int index = r * 2 + c - offset;
                if (index < 0) {
                    ImGui::TextDisabled("…");
                } else if (index < plies) {
                    cell(index);
                }
            }
            // The end of the book, after the row of its last move, once a move follows it.
            const int first = r * 2 - offset;
            if (last_book >= 0 && last_book >= first && last_book <= first + 1 && plies > last_book + 1) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", TR("· end of book ·"));
            }
        }
        ImGui::EndTable();
    }
    // Follow the game, if the list was at its end (or the game is a new one): earlier moves being read stay in sight,
    // as does the move shown while the board is stepped back. Repeated for a few frames because the table grows after
    // the first one.
    if (st.moves.size() != view.followed) {
        const bool new_game = view.followed == 0 || st.moves.size() < view.followed;
        const bool at_end   = view.follow_frames > 0 ||
                              ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetTextLineHeightWithSpacing();
        if (new_game || (at_end && !held)) view.follow_frames = 3;
        view.followed = st.moves.size();
    }
    // Back to the current move: the list too.
    if (moved && !held && view.listed_ply >= 0) view.follow_frames = 3;
    view.listed_ply = shown;
    if (view.follow_frames > 0) {
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
        view.follow_frames--;
    }
    ImGui::EndChild();
}

// The game graph, filling the room left.
void App::drawLiveChart(SlotView& view) {
    const auto& st = view.state;
    const auto game     = replayOfSlot(st);
    const int total     = static_cast<int>(st.moves.size());
    const auto* t       = run_->tournament();
    const bool chess960 = t && t->spec().conditions.variant == fastchess::VariantType::FRC;
    const auto graph    = buildGameGraph(game, chess960, live_graph_.chart, {}, {});
    GraphDraw draw;
    // The position shown marked when the board stepped back; a click shows another.
    draw.current = view.nav.held(total) ? view.nav.shown(total) : -1;
    draw.tip     = [&game](int ply) { return graphMoveTip(game, ply); };
    const auto ev =
        drawGameGraph("##live_graph", graph, live_graph_, draw,
                      ImVec2(ImGui::GetContentRegionAvail().x, std::max(60.0f, ImGui::GetContentRegionAvail().y)));
    if (ev.go_to >= 0) view.nav.show(std::clamp(ev.go_to, 0, total), total);
}

// The parts beside the board, one under the other: the players with their searches, the game, the moves, the chart.
void App::drawLiveSide(SlotView& view, unsigned parts, int slot, bool allow_detach) {
    const bool players = parts & LivePlayers, searches = parts & LiveSearches;
    if (players || searches) {
        // In cards, the moves below keep room for a few rows: the engines' lines give way, each to half of what is
        // left once the rest of the cards (measured the last frame) is drawn.
        const bool cards = largePanel(ImGui::GetContentRegionAvail().x);
        float pv_room    = FLT_MAX;
        if (cards && searches && (parts & LiveMoves)) {
            const float line = ImGui::GetTextLineHeightWithSpacing();
            pv_room          = std::max(line, (ImGui::GetContentRegionAvail().y - view.cards_fixed - 11 * line) / 2);
        }
        const float top = ImGui::GetCursorPosY();
        float lines = drawLivePlayer(view, view.flip ? chess::Color::WHITE : chess::Color::BLACK, searches, pv_room);
        ImGui::Separator();
        lines += drawLivePlayer(view, view.flip ? chess::Color::BLACK : chess::Color::WHITE, searches, pv_room);
        ImGui::Separator();
        if (cards) view.cards_fixed = ImGui::GetCursorPosY() - top - lines;
    }
    if (players) drawLiveGameLine(view, slot, allow_detach);

    const bool moves = parts & LiveMoves;
    if (!(parts & LiveChart)) {
        if (moves) drawLiveMoves(view, std::max(ImGui::GetFrameHeight(), ImGui::GetContentRegionAvail().y));
        return;
    }
    if (!moves) {
        drawLiveChart(view);
        return;
    }
    // Moves, then the evaluation curve.
    const float plot_h = std::max(px(110), ImGui::GetContentRegionAvail().y * 0.30f) +
                         ImGui::GetFrameHeightWithSpacing();  // and its menu
    // Too low for both (a small window): the moves only, and a line that says the curve is left out (#194).
    const float left    = ImGui::GetContentRegionAvail().y;
    const bool chart    = left - plot_h >= ImGui::GetFrameHeightWithSpacing() * 4;
    const float note_h  = chart ? 0.0f : ImGui::GetTextLineHeightWithSpacing();
    const float moves_h = chart ? left - plot_h - ImGui::GetStyle().ItemSpacing.y : left - note_h;
    drawLiveMoves(view, std::max(ImGui::GetFrameHeight(), moves_h));
    if (chart) {
        drawLiveChart(view);
    } else {
        ImGui::TextDisabled("%s", TR("The evaluation curve is left out: the window is too low for it."));
    }
}

void App::drawLiveGame(SlotView& view, const char* id, unsigned parts, int slot, bool allow_detach) {
    const auto& st = view.state;
    ImGui::PushID(id);

    if (!st.has_game) {
        ImGui::TextDisabled("%s", TR("No game in this slot yet."));
        ImGui::PopID();
        return;
    }

    // Without the board, the other parts fill the room; the board alone is as large as the room allows.
    const unsigned side = parts & ~LiveBoard;
    if (!(parts & LiveBoard) || side == 0) {
        if (parts & LiveBoard) {
            drawLiveBoardFitted(view);
        } else {
            ImGui::BeginChild("##live_side", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
            drawLiveSide(view, side, slot, allow_detach);
            ImGui::EndChild();
        }
        ImGui::PopID();
        return;
    }

    // The board as large as the panel allows, with the players, moves and charts beside it; under it when that
    // gives a much larger board (a narrow, tall panel).
    const ImVec2 avail     = ImGui::GetContentRegionAvail();
    const float info_h     = ImGui::GetTextLineHeightWithSpacing() + liveNavHeight();
    const float beside     = std::min(avail.y - 4.0f - info_h, avail.x - sidePanel() - ImGui::GetStyle().ItemSpacing.x);
    const float under      = std::min(avail.x, (avail.y - info_h) * 0.6f);
    const bool stacked     = beside < under * 0.75f;
    const float board_size = std::floor(std::max(px(160), stacked ? under : beside));

    if (stacked) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - board_size) * 0.5f));
    drawLiveBoard(view, board_size);

    float side_h = std::max(avail.y, board_size + info_h);
    if (stacked) {
        side_h = std::max(px(260), ImGui::GetContentRegionAvail().y);
    } else {
        ImGui::SameLine();
    }
    ImGui::BeginChild("##live_side", ImVec2(0, side_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    drawLiveSide(view, side, slot, allow_detach);
    ImGui::EndChild();
    ImGui::PopID();
}

unsigned App::livePartsHere() const {
    unsigned parts = 0;
    for (const auto& [part, panel] : LIVE_PARTS) {
        if (!panels_open_.count(panel)) parts |= part;
    }
    return parts;
}

// A part of the followed game in its own panel.
void App::drawLivePart(unsigned part) {
    if (!run_->observer() || slot_views_.empty()) {
        ImGui::TextDisabled("%s", TR("No game is being played."));
        return;
    }
    live_slot_ = std::clamp(live_slot_, 0, static_cast<int>(slot_views_.size()) - 1);
    auto& view = slot_views_[static_cast<std::size_t>(live_slot_)];
    if (!view.state.has_game) {
        ImGui::TextDisabled("%s", TR("No game in this slot yet."));
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) liveNavKeys(view);
    ImGui::PushID(static_cast<int>(part));
    switch (part) {
        case LiveBoard:
            drawLiveBoardFitted(view);
            break;
        case LivePlayers:
        case LiveSearches:
            drawLiveSide(view, part, live_slot_, true);
            break;
        case LiveMoves:
            drawLiveMoves(view, std::max(ImGui::GetFrameHeight(), ImGui::GetContentRegionAvail().y));
            break;
        case LiveChart:
            drawLiveChart(view);
            break;
        default:
            break;
    }
    ImGui::PopID();
}

// A detached board: every part of its game.
void App::drawDetachedBoard(int slot) {
    auto* obs = run_->observer();
    if (!obs || slot >= obs->slotCount()) {
        ImGui::TextDisabled("%s", TR("No game is being played."));
        return;
    }
    refreshSlot(slot);
    if (const auto big = big_boards_.find(slot); big != big_boards_.end()) {
        drawBigGame(big->second, false);
        return;
    }
    if (slot < static_cast<int>(slot_views_.size())) {
        auto& view = slot_views_[static_cast<std::size_t>(slot)];
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) liveNavKeys(view);
        drawLiveGame(view, "##detached", LiveAll, slot, false);
    }
}

}  // namespace fcg
