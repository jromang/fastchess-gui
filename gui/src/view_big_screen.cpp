// The big screen: one game of the live view filling the window, or a detached board's window, for a television, a
// club or a stream. Large names and clocks, the evaluation bar, the depth, the last move framed on the board and the
// moves in large type, light on black; the controls show for a few seconds when the mouse moves.

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string_view>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <fmt/include/fmt/format.h>
#include <engine/uci_engine.hpp>

#include "app.hpp"
#include "big_screen.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

// High contrast, whatever the theme: light on black, the side to move in yellow.
constexpr ImU32 BIG_BG      = IM_COL32(8, 9, 11, 255);
constexpr ImU32 BIG_TEXT    = IM_COL32(250, 250, 250, 255);
constexpr ImU32 BIG_MUTED   = IM_COL32(165, 172, 182, 255);
constexpr ImU32 BIG_MOVE    = IM_COL32(255, 210, 70, 255);  // the side to move, its clock, the last move
constexpr ImU32 BIG_LOW     = IM_COL32(255, 95, 90, 255);   // a clock under ten seconds
constexpr ImU32 BIG_MARK    = IM_COL32(255, 210, 70, 60);   // the last move in the list
constexpr ImU32 BIG_BAR_BD  = IM_COL32(120, 120, 120, 255);
constexpr auto CONTROLS_FOR = std::chrono::seconds(3);  // the controls stay that long after the mouse moved

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// Text in `font` at `size` pixels, made at that size (crisp at any size), from its top left corner.
ImVec2 textSize(ImFont* font, float size, std::string_view text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size());
}

void text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, std::string_view text) {
    dl->AddText(font, size, ImVec2(std::floor(pos.x), std::floor(pos.y)), col, text.data(), text.data() + text.size());
}

// `text` at the largest size up to `size` that fits `width`, down to 70 %; cut with "…" beyond. Returns its size.
float fitted(ImFont* font, float size, float width, std::string& text) {
    const float wide = textSize(font, size, text).x;
    if (wide <= width) return size;
    const float smaller = std::max(size * 0.7f, size * width / wide);
    if (textSize(font, smaller, text).x <= width) return smaller;
    while (text.size() > 1 && textSize(font, smaller, text + "…").x > width) {
        text.pop_back();
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
    }
    text += "…";
    return smaller;
}

// What the side to move is searching, else what the last move reported: the score from White's point of view, the
// depth and the speed.
struct Search {
    std::optional<fastchess::Score> white;  // White's point of view
    int64_t depth    = 0;
    int64_t seldepth = 0;
    uint64_t nps     = 0;
};

Search searchOf(const SlotState& st) {
    Search s;
    const auto& line = st.info[st.stm == chess::Color::WHITE ? 0 : 1];
    if (st.active && st.thinking && !line.empty()) {
        const auto info = fastchess::engine::UciEngine::parseInfo(line);
        if (info.score) {
            s.white = *info.score;
            if (st.stm == chess::Color::BLACK) s.white->value = -s.white->value;
        }
        s.depth    = info.depth.value_or(0);
        s.seldepth = info.seldepth.value_or(0);
        s.nps      = info.nps.value_or(0);
        if (s.white) return s;
    }
    for (auto it = st.moves.rbegin(); it != st.moves.rend(); ++it) {
        if (it->book || !it->score) continue;
        s.white = *it->score;
        if (it->mover == chess::Color::BLACK) s.white->value = -s.white->value;
        s.depth    = it->depth;
        s.seldepth = it->seldepth;
        s.nps      = it->nps;
        break;
    }
    return s;
}

// The share of the bar that is White's, from White's score.
float barOf(const Search& s) {
    if (!s.white) return 0.5f;
    if (s.white->type == fastchess::ScoreType::MATE) return evalBarFraction(0, true, s.white->value >= 0 ? 1 : -1);
    return evalBarFraction(static_cast<int>(s.white->value), false, 0);
}

// The game as the big screen weighs it, to follow the games in progress.
BigCandidate candidateOf(const SlotState& st) {
    BigCandidate c;
    c.active = st.active && st.has_game;
    c.plies  = st.moves.size();
    if (const auto s = searchOf(st); s.white) {
        c.mate    = s.white->type == fastchess::ScoreType::MATE;
        c.eval_cp = static_cast<int>(s.white->value);
    }
    return c;
}

// "34. Nf3" or "34… Nf6": a move with its number.
std::string numbered(const SlotState& st, std::size_t index) {
    chess::Board start;
    start.setFen(st.start_fen);
    const bool black_first = start.sideToMove() == chess::Color::BLACK;
    const std::size_t ply  = index + (black_first ? 1 : 0);
    const int number       = start.fullMoveNumber() + static_cast<int>(ply / 2);
    return fmt::format("{}{} {}", number, ply % 2 == 0 ? "." : "…", st.moves[index].san);
}

// An engine's line: the side to move's running search, else what its last move said; the score from White's side.
struct BigLine {
    std::optional<fastchess::Score> white;
    int64_t depth = 0;
    std::string pv;  // in SAN, with move numbers
    bool running = false;
};

BigLine bigLine(const SlotState& st, const chess::Board& board, chess::Color color) {
    BigLine line;
    const auto& info_line = st.info[color == chess::Color::WHITE ? 0 : 1];
    const auto from_white = [&](std::optional<fastchess::Score> score) {
        if (score && color == chess::Color::BLACK) score->value = -score->value;
        return score;
    };
    if (st.active && st.stm == color && st.thinking && !info_line.empty()) {
        const auto info = fastchess::engine::UciEngine::parseInfo(info_line);
        if (info.score) line.white = from_white(*info.score);
        line.depth   = info.depth.value_or(0);
        line.pv      = pvToSan(board, info.pv, info.pv.size());
        line.running = true;
        return line;
    }
    for (int i = static_cast<int>(st.moves.size()) - 1; i >= 0; --i) {
        const auto& m = st.moves[static_cast<std::size_t>(i)];
        if (m.mover != color || m.book) continue;
        std::vector<std::string> moves;
        std::istringstream words(m.pv);
        for (std::string w; words >> w;) moves.push_back(w);
        line.white = from_white(m.score);
        line.depth = m.depth;
        line.pv    = pvToSan(slotBoard(st, i), moves, moves.size());
        break;
    }
    return line;
}

// `text` cut after the last word that keeps it within `lines` lines `width` wide at `size`, with "…" when cut.
std::string withinLines(ImFont* font, float size, float width, const std::string& text, int lines) {
    const float most = textSize(font, size, "Ag").y * static_cast<float>(lines) + 0.5f;
    if (font->CalcTextSizeA(size, FLT_MAX, width, text.c_str()).y <= most) return text;
    std::string out;
    std::istringstream words(text);
    for (std::string w; words >> w;) {
        auto next = out.empty() ? w : out + " " + w;
        if (font->CalcTextSizeA(size, FLT_MAX, width, (next + " …").c_str()).y > most) break;
        out = std::move(next);
    }
    return out + " …";
}

SDL_Window* windowOf(const ImGuiViewport* viewport) {
    if (!viewport || !viewport->PlatformHandle) return nullptr;
    return SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<intptr_t>(viewport->PlatformHandle)));
}

}  // namespace

void App::startBigScreen(int slot) {
    big_.on          = true;
    big_.view.slot   = std::max(0, slot);
    big_.view.panel  = -1;
    big_.view.turned = Clock::now();
    big_.moved       = Clock::now();  // the controls show at first
    big_.made_full   = false;
}

void App::startBigBoard(int slot) {
    // Docked in the main window (or floating in it), the board would only fill its panel: the main window shows it.
    if (ImGui::GetWindowViewport() == ImGui::GetMainViewport()) {
        startBigScreen(slot);
        return;
    }
    BigView view;
    view.slot         = slot;
    view.panel        = slot;
    view.turned       = Clock::now();
    big_boards_[slot] = view;
    big_.moved        = Clock::now();
}

void App::leaveBigScreen() {
    if (big_.made_full && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN))
        SDL_SetWindowFullscreen(window_, false);
    big_.on        = false;
    big_.made_full = false;
}

void App::leaveBigBoard(int slot) {
    // Its window, full screen, goes back to its size; not the main one, whose full screen is the user's.
    if (const auto it = big_board_windows_.find(slot); it != big_board_windows_.end()) {
        if (it->second && it->second != window_ && (SDL_GetWindowFlags(it->second) & SDL_WINDOW_FULLSCREEN)) {
            SDL_SetWindowFullscreen(it->second, false);
        }
        big_board_windows_.erase(it);
    }
    big_boards_.erase(slot);
}

void App::toggleFullscreen(SDL_Window* window) {
    if (!window) return;
    const bool full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(window, !full);
    if (window == window_) big_.made_full = big_.on && !full;
}

void App::handleBigScreenKeys() {
    const auto& io = ImGui::GetIO();
    if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || ImGui::IsAnyMouseDown()) big_.moved = Clock::now();
    // Not while typing, nor while a popup (the palette, a menu) takes the keys.
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;

    // The window a key is for: a detached board shown as a big screen and focused (in the last frame), else the main
    // window.
    BigView* view      = big_.on ? &big_.view : nullptr;
    SDL_Window* window = window_;
    const int board    = big_board_focused_;
    const bool board_keys =
        !big_.on && big_boards_.count(board) > 0 && ImGui::GetFrameCount() - big_board_focused_frame_ <= 1;
    if (board_keys) {
        view = &big_boards_[board];
        if (const auto it = big_board_windows_.find(board); it != big_board_windows_.end() && it->second) {
            window = it->second;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) toggleFullscreen(window);
    if (!view) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (board_keys) {
            leaveBigBoard(board);
        } else {
            leaveBigScreen();
        }
        return;
    }
    const int slots = run_->observer() ? run_->observer()->slotCount() : 0;
    if (slots > 0 && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        view->slot   = (view->slot + 1) % slots;
        view->turned = Clock::now();
    }
    if (slots > 0 && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        view->slot   = (view->slot + slots - 1) % slots;
        view->turned = Clock::now();
    }
    const auto scale = [&](float by) {
        setup_.big_scale = std::clamp(std::round((setup_.big_scale + by) * 10.0f) / 10.0f, 0.5f, 2.0f);
        saveSetupSoon();
    };
    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) scale(0.1f);
    if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) scale(-0.1f);
}

void App::drawBigScreen() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##big_screen", nullptr, flags);
    ImGui::PopStyleVar(3);
    // Over the panels floating in the window, which stay for when it is left.
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    drawBigGame(big_.view, true);
    ImGui::End();
}

void App::drawBigGame(BigView& big, bool main_window) {
    ImDrawList* dl      = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size   = ImGui::GetContentRegionAvail();
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), BIG_BG);
    if (!main_window) {
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            big_board_focused_       = big.panel;
            big_board_focused_frame_ = ImGui::GetFrameCount();
        }
        big_board_windows_[big.panel] = windowOf(ImGui::GetWindowViewport());
    }

    ImFont* font  = ImGui::GetFont();
    ImFont* bold  = ui::titleFont() ? ui::titleFont() : font;
    const float u = bigTextSize(size.x, size.y, setup_.big_scale);
    const float m = std::floor(u * 0.6f);

    auto* obs       = run_->observer();
    const int slots = obs ? obs->slotCount() : 0;
    for (int i = 0; i < slots; ++i) refreshSlot(i);
    big.slot = slots > 0 ? std::clamp(big.slot, 0, slots - 1) : 0;

    // Following the games in progress: in turn, or the most interesting one, every big_seconds.
    const auto follow = static_cast<BigFollow>(setup_.big_follow);
    const auto now    = Clock::now();
    if (follow != BigFollow::Game && slots > 1 && now - big.turned >= std::chrono::seconds(setup_.big_seconds)) {
        std::vector<BigCandidate> games;
        for (int i = 0; i < slots; ++i) games.push_back(candidateOf(slot_views_[static_cast<std::size_t>(i)].state));
        big.slot   = follow == BigFollow::InTurn ? nextGame(games, big.slot) : mostInteresting(games, big.slot);
        big.turned = now;
    }
    if (slots > 0) hearMoves(big.slot);

    const auto centered = [&](const std::string& message) {
        const auto s = textSize(font, u, message);
        text(dl, font, u, ImVec2(origin.x + (size.x - s.x) / 2, origin.y + (size.y - s.y) / 2), BIG_MUTED, message);
    };
    if (slots == 0 || big.slot >= static_cast<int>(slot_views_.size())) {
        centered(TR("No game is being played."));
        drawBigControls(big, main_window, origin, slots);
        return;
    }
    auto& v        = slot_views_[static_cast<std::size_t>(big.slot)];
    const auto& st = v.state;
    if (!st.has_game) {
        centered(TR("Waiting for the next game…"));
        drawBigControls(big, main_window, origin, slots);
        return;
    }

    const auto search   = searchOf(st);
    const bool portrait = size.y > size.x * 1.15f;
    const float bar_w   = std::floor(u * 0.75f);
    const float gap     = std::floor(u * 0.3f);

    // The board, as large as the room beside (or above) the players allows, with the evaluation bar beside it.
    float board = portrait ? std::min(size.x - 2 * m - bar_w - gap, size.y * 0.56f)
                           : std::min(size.y - 2 * m, size.x * 0.62f - bar_w - gap - m);
    board       = std::floor(std::max(board, u * 4));
    const ImVec2 bar_at(portrait ? origin.x + (size.x - board - bar_w - gap) / 2 : origin.x + m,
                        portrait ? origin.y + m : origin.y + (size.y - board) / 2);
    const ImVec2 board_at(bar_at.x + bar_w + gap, bar_at.y);
    {
        // White's share at the bottom, or at the top with the board turned.
        const float white = barOf(search);
        const float split = v.flip ? bar_at.y + board * white : bar_at.y + board * (1.0f - white);
        dl->AddRectFilled(bar_at, ImVec2(bar_at.x + bar_w, bar_at.y + board), IM_COL32(30, 30, 30, 255));
        if (v.flip) {
            dl->AddRectFilled(bar_at, ImVec2(bar_at.x + bar_w, split), IM_COL32(240, 240, 240, 255));
        } else {
            dl->AddRectFilled(ImVec2(bar_at.x, split), ImVec2(bar_at.x + bar_w, bar_at.y + board),
                              IM_COL32(240, 240, 240, 255));
        }
        dl->AddLine(ImVec2(bar_at.x, bar_at.y + board / 2), ImVec2(bar_at.x + bar_w, bar_at.y + board / 2),
                    IM_COL32(128, 128, 128, 255), std::max(1.0f, u / 24));
        dl->AddRect(bar_at, ImVec2(bar_at.x + bar_w, bar_at.y + board), BIG_BAR_BD);
    }
    ImGui::SetCursorScreenPos(board_at);
    BoardDecor decor;
    decor.flipped    = v.flip;
    decor.eval_white = -1.0f;
    if (!st.moves.empty()) {
        decor.last_move        = st.moves.back().move;
        decor.last_move_frame  = std::max(3.0f, board / 110.0f);
        decor.last_move_colour = BIG_MOVE;
    }
    if (setup_.live_arrows) decor.arrows = pvArrows(st, v.board);
    drawBoard(atlas_, v.board, board, decor);

    // The result over the board, once the game is over.
    if (!st.active && !st.result.empty()) {
        const std::string result = st.result == "1/2-1/2" ? "½-½" : st.result;
        const std::string reason = TR(st.reason);
        const auto rs = textSize(bold, u * 2.4f, result), ws = textSize(font, u * 0.8f, reason);
        const float band = rs.y + ws.y + u * 0.6f, top = board_at.y + (board - band) / 2;
        dl->AddRectFilled(ImVec2(board_at.x, top), ImVec2(board_at.x + board, top + band), IM_COL32(0, 0, 0, 190));
        text(dl, bold, u * 2.4f, ImVec2(board_at.x + (board - rs.x) / 2, top + u * 0.2f), BIG_MOVE, result);
        text(dl, font, u * 0.8f, ImVec2(board_at.x + (board - ws.x) / 2, top + u * 0.3f + rs.y), BIG_TEXT, reason);
    }

    // The players: the one at the top of the board first; each a name and a clock.
    const auto elapsed  = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.think_start).count();
    const float name_px = u * 1.3f, clock_px = u * 2.2f;
    const auto player_h = [&] { return textSize(bold, name_px, "Ag").y + textSize(bold, clock_px, "0:").y; };
    const auto player   = [&](chess::Color color, ImVec2 at, float width) {
        const bool white   = color == chess::Color::WHITE;
        const bool to_move = st.active && st.stm == color;
        const bool thinks  = to_move && st.thinking;
        const float disc   = name_px * 0.3f;
        const float line   = textSize(bold, name_px, "Ag").y;
        dl->AddCircleFilled(ImVec2(at.x + disc, at.y + line / 2), disc,
                            white ? IM_COL32(245, 245, 245, 255) : IM_COL32(20, 20, 20, 255), 24);
        dl->AddCircle(ImVec2(at.x + disc, at.y + line / 2), disc, to_move ? BIG_MOVE : BIG_BAR_BD, 24,
                      to_move ? std::max(2.0f, u / 10) : 1.0f);
        auto name  = white ? st.white : st.black;
        float left = disc * 2 + u * 0.35f;
        // The engine's logo as high as its name and clock together, at most twice as wide.
        if (const auto* logo = engineLogo(name)) {
            const float high = player_h();
            const float w    = std::min(high * logo->width / logo->height, high * 2.0f);
            const float h    = w * logo->height / logo->width;
            dl->AddImage(logo->texture(h * ImGui::GetIO().DisplayFramebufferScale.y),
                         ImVec2(at.x + left, at.y + (high - h) / 2), ImVec2(at.x + left + w, at.y + (high + h) / 2));
            left += w + u * 0.35f;
        }
        const float fit = fitted(bold, name_px, width - left, name);
        text(dl, bold, fit, ImVec2(at.x + left, at.y + (line - textSize(bold, fit, "Ag").y) / 2),
             to_move ? BIG_MOVE : BIG_TEXT, name);

        std::string clock;
        int64_t ms = 0;
        if (st.clocks) {
            ms    = (white ? st.white_ms : st.black_ms) - (thinks ? elapsed : 0);
            clock = ui::formatClock(ms);
        } else if (thinks) {
            clock = ui::formatClock(elapsed);
        }
        if (!clock.empty()) {
            const ImU32 col = st.clocks && ms < 10'000 && st.active ? BIG_LOW : thinks ? BIG_MOVE : BIG_TEXT;
            text(dl, bold, clock_px, ImVec2(at.x + left, at.y + line), col, clock);
        }
    };

    // The middle: the game, the last move in large type, the search, then the moves as far as they fit.
    const auto middle = [&](ImVec2 at, float width, float bottom) {
        const float info_px = u * 0.75f;
        const auto info = trf("Game {} · round {} · move {}", st.game_id, st.round_id + 1, (st.moves.size() + 2) / 2);
        text(dl, font, info_px, at, BIG_MUTED, info);
        at.y += textSize(font, info_px, info).y + u * 0.35f;

        // The opening, as far as the game went.
        if (v.opening.name) {
            const float open_px   = u * 0.72f;
            const std::string eco = v.opening.name->eco;
            std::string name      = v.opening.name->name;
            const float eco_w     = textSize(bold, open_px, eco).x + u * 0.35f;
            text(dl, bold, open_px, at, BIG_TEXT, eco);
            const float fit = fitted(font, open_px, width - eco_w, name);
            text(dl, font, fit, ImVec2(at.x + eco_w, at.y), BIG_TEXT, name);
            at.y += textSize(font, open_px, "Ag").y + u * 0.35f;
        }

        if (!st.moves.empty()) {
            auto last       = numbered(st, st.moves.size() - 1);
            const float fit = fitted(bold, u * 1.6f, width, last);
            text(dl, bold, fit, at, BIG_MOVE, last);
            at.y += textSize(bold, fit, last).y + u * 0.2f;
        }
        if (search.white || search.depth > 0) {
            std::string line = ui::formatScore(search.white);
            if (search.depth > 0) {
                line += "   " + trf("depth {}", search.depth);
                if (search.seldepth > 0) line += fmt::format("/{}", search.seldepth);
            }
            if (search.nps > 0) line += "   " + trf("{} n/s", ui::formatCount(search.nps));
            const float fit = fitted(font, u, width, line);
            text(dl, font, fit, at, BIG_TEXT, line);
            at.y += textSize(font, fit, line).y + u * 0.45f;
        }

        // The engines' lines: the side to move's as it searches, the other's from its last move, two lines each at
        // most.
        if (setup_.big_lines) {
            const float head_px = u * 0.68f, pv_px = u * 0.8f, pad = u * 0.4f;
            const ImVec2 box(at.x, at.y);
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);
            float y   = at.y + pad;
            bool none = true;
            for (const auto color : {st.stm, ~st.stm}) {
                const auto line = bigLine(st, v.board, color);
                if (line.pv.empty()) continue;
                none             = false;
                const bool white = color == chess::Color::WHITE;
                const float disc = head_px * 0.3f, head_h = textSize(bold, head_px, "Ag").y;
                const ImVec2 c(at.x + pad + disc, y + head_h / 2);
                dl->AddCircleFilled(c, disc, white ? IM_COL32(245, 245, 245, 255) : IM_COL32(20, 20, 20, 255), 20);
                dl->AddCircle(c, disc, line.running ? BIG_MOVE : BIG_BAR_BD, 20, line.running ? 2.0f : 1.0f);
                float x          = at.x + pad + 2 * disc + u * 0.3f;
                std::string name = white ? st.white : st.black;
                std::string said = ui::formatScore(line.white);
                if (line.depth > 0) said += fmt::format(" · d{}", line.depth);
                if (!line.running) said += TR(", at its last move");
                const float said_w = textSize(font, head_px * 0.9f, said).x;
                const float fit = fitted(bold, head_px, width - 2 * pad - (x - at.x - pad) - said_w - u * 0.4f, name);
                text(dl, bold, fit, ImVec2(x, y), BIG_TEXT, name);
                x += textSize(bold, fit, name).x + u * 0.4f;
                text(dl, font, head_px * 0.9f, ImVec2(x, y + head_h * 0.05f), BIG_MUTED, said);
                y += head_h + u * 0.12f;
                const float pv_w = width - 2 * pad;
                const auto pv    = withinLines(font, pv_px, pv_w, line.pv, 2);
                dl->AddText(font, pv_px, ImVec2(std::floor(at.x + pad), std::floor(y)),
                            line.running ? BIG_MOVE : IM_COL32(214, 218, 224, 255), pv.c_str(), nullptr, pv_w);
                y += font->CalcTextSizeA(pv_px, FLT_MAX, pv_w, pv.c_str()).y + u * 0.3f;
            }
            dl->ChannelsSetCurrent(0);
            if (!none) {
                y += pad - u * 0.3f;
                dl->AddRectFilled(box, ImVec2(box.x + width, y), IM_COL32(22, 26, 33, 255), u * 0.4f);
                at.y = y + u * 0.35f;
            }
            dl->ChannelsMerge();
        }

        // The tables' verdict, once they know the position.
        if (const auto* verdict = liveVerdict(v.board)) {
            const float tb_px     = u * 0.72f;
            const bool white_wins = (verdict->result > 0) == (v.board.sideToMove() == chess::Color::WHITE);
            const bool decisive   = verdict->result != 0 && !verdict->rule50;
            std::string head      = verdict->result == 0 ? TR("Tablebases: draw")
                                    : verdict->rule50    ? TR("Tablebases: draw by the 50-move rule")
                                    : white_wins         ? TR("Tablebases: White wins")
                                                         : TR("Tablebases: Black wins");
            std::string tail;
            if (verdict->result != 0 && verdict->dtz) tail = fmt::format("· DTZ {}", *verdict->dtz);
            if (verdict->pieces > 0) tail += (tail.empty() ? "" : "  ") + trf("({} pieces or fewer)", verdict->pieces);
            const float h = textSize(font, tb_px, "Ag").y, r = tb_px * 0.22f;
            dl->AddCircleFilled(ImVec2(at.x + r, at.y + h / 2), r, decisive ? IM_COL32(63, 178, 127, 255) : BIG_MUTED,
                                16);
            const float x  = at.x + 2 * r + u * 0.3f;
            const float fw = fitted(font, tb_px, width - (x - at.x), head);
            text(dl, font, fw, ImVec2(x, at.y), BIG_TEXT, head);
            if (!tail.empty()) {
                const float tx = x + textSize(font, fw, head).x + u * 0.3f;
                if (tx < at.x + width) {
                    std::string rest = tail;
                    const float ft   = fitted(font, tb_px * 0.9f, at.x + width - tx, rest);
                    text(dl, font, ft, ImVec2(tx, at.y + h * 0.05f), BIG_MUTED, rest);
                }
            }
            at.y += h + u * 0.45f;
        }

        // The moves, two to a row, the last one marked, as many of the last ones as fit.
        const float move_px = u * 1.05f;
        const float row     = textSize(font, move_px, "Ag").y * 1.18f;
        const int rows_fit  = static_cast<int>((bottom - at.y) / row);
        if (rows_fit <= 0 || st.moves.empty()) return;
        chess::Board start;
        start.setFen(st.start_fen);
        const std::size_t skip = start.sideToMove() == chess::Color::BLACK ? 1 : 0;  // "1… e5" fills a row
        const std::size_t rows = (st.moves.size() + skip + 1) / 2;
        const std::size_t from = rows > static_cast<std::size_t>(rows_fit) ? rows - rows_fit : 0;
        const float number_w   = textSize(font, move_px, "000.").x;
        const float col_w      = std::min((width - number_w) / 2, u * 7.0f);  // the two moves of a row together
        for (std::size_t r = from; r < rows; ++r) {
            const float y = at.y + static_cast<float>(r - from) * row;
            text(dl, font, move_px, ImVec2(at.x, y), BIG_MUTED,
                 fmt::format("{}.", start.fullMoveNumber() + static_cast<int>(r)));
            for (int c = 0; c < 2; ++c) {
                const std::size_t ply = r * 2 + c;
                if (ply < skip || ply - skip >= st.moves.size()) continue;
                const std::size_t i = ply - skip;
                const ImVec2 cell(at.x + number_w + c * col_w, y);
                if (i + 1 == st.moves.size()) {
                    dl->AddRectFilled(ImVec2(cell.x - u * 0.15f, y), ImVec2(cell.x + col_w - u * 0.2f, y + row),
                                      BIG_MARK, u * 0.15f);
                }
                auto san        = st.moves[i].san;
                const float fit = fitted(font, move_px, col_w - u * 0.3f, san);
                text(dl, font, fit, cell, st.moves[i].book ? BIG_MUTED : BIG_TEXT, san);
            }
        }
    };

    const auto top_color    = v.flip ? chess::Color::WHITE : chess::Color::BLACK;
    const auto bottom_color = v.flip ? chess::Color::BLACK : chess::Color::WHITE;
    if (portrait) {
        // Under the board: the two players side by side, then the rest.
        const float y     = board_at.y + board + m;
        const float width = (size.x - 3 * m) / 2;
        player(top_color, ImVec2(origin.x + m, y), width);
        player(bottom_color, ImVec2(origin.x + 2 * m + width, y), width);
        middle(ImVec2(origin.x + m, y + player_h() + m), size.x - 2 * m, origin.y + size.y - m);
    } else {
        const float x     = board_at.x + board + m * 1.4f;
        const float width = origin.x + size.x - m - x;
        player(top_color, ImVec2(x, board_at.y), width);
        const float bottom_y = board_at.y + board - player_h();
        player(bottom_color, ImVec2(x, bottom_y), width);
        middle(ImVec2(x, board_at.y + player_h() + m), width, bottom_y - m);
    }

    drawBigControls(big, main_window, origin, slots);
}

void App::drawBigControls(BigView& big, bool main_window, ImVec2 origin, int slots) {
    // Shown for a few seconds once the mouse moved, and while it is over them; the pointer hidden with them.
    const bool recent = Clock::now() - big_.moved < CONTROLS_FOR;
    if (!recent && !big.controls_hovered && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsWindowHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        return;
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x + px(12), origin.y + px(10)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(40, 42, 46, 235));
    ImGui::PushStyleColor(ImGuiCol_Text, BIG_TEXT);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 73, 80, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(95, 99, 108, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(70, 73, 80, 255));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(40, 42, 46, 250));
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(95, 99, 108, 255));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, BIG_MOVE);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(10), px(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(8));
    ImGui::BeginChild(
        "##big_controls", ImVec2(0, 0),
        ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar);

    if (ImGui::Button("<")) {
        big.slot   = slots > 0 ? (big.slot + slots - 1) % slots : 0;
        big.turned = Clock::now();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The arrow keys too."));
    ImGui::SameLine();
    ImGui::TextUnformatted(trf("Board {} of {}", big.slot + 1, std::max(1, slots)).c_str());
    ImGui::SameLine();
    if (ImGui::Button(">")) {
        big.slot   = slots > 0 ? (big.slot + 1) % slots : 0;
        big.turned = Clock::now();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The arrow keys too."));

    ImGui::SameLine(0, px(18));
    const char* follows[] = {TR("This game"), TR("Every game in turn"), TR("The most interesting")};
    ImGui::SetNextItemWidth(px(190));
    if (ImGui::Combo("##big_follow", &setup_.big_follow, follows, IM_ARRAYSIZE(follows))) {
        big.turned = Clock::now();
        saveSetupSoon();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Every game in turn: the games in progress one after the other.\nThe most "
                                   "interesting: the game whose evaluation is closest to level, the longest of those "
                                   "as close; it stays while it is."));
    }
    if (setup_.big_follow != static_cast<int>(BigFollow::Game)) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(110));
        if (ImGui::InputInt(TR("s##big_seconds"), &setup_.big_seconds, 5, 30)) {
            setup_.big_seconds = std::clamp(setup_.big_seconds, 5, 600);
            saveSetupSoon();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Seconds before the next game is chosen."));
    }

    ImGui::SameLine(0, px(18));
    if (ImGui::Button("A-")) {
        setup_.big_scale = std::clamp(std::round((setup_.big_scale - 0.1f) * 10.0f) / 10.0f, 0.5f, 2.0f);
        saveSetupSoon();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("The size of the text, for the distance it is read from (+ and - too)."));
    ImGui::SameLine();
    ImGui::Text("%d %%", static_cast<int>(std::lround(setup_.big_scale * 100)));
    ImGui::SameLine();
    if (ImGui::Button("A+")) {
        setup_.big_scale = std::clamp(std::round((setup_.big_scale + 0.1f) * 10.0f) / 10.0f, 0.5f, 2.0f);
        saveSetupSoon();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("The size of the text, for the distance it is read from (+ and - too)."));

    if (big.slot < static_cast<int>(slot_views_.size())) {
        ImGui::SameLine(0, px(18));
        ImGui::Checkbox(TR("Flip"), &slot_views_[static_cast<std::size_t>(big.slot)].flip);
    }
    ImGui::SameLine(0, px(18));
    if (ImGui::Checkbox(TR("Lines"), &setup_.big_lines)) saveSetupSoon();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The engines' lines under the search: the side to move's as it searches, the "
                                   "other's from its last move."));
    }
    ImGui::SameLine(0, px(18));
    if (ImGui::Button(TR("Full screen (F11)"))) {
        toggleFullscreen(main_window ? window_ : windowOf(ImGui::GetWindowViewport()));
    }
    ImGui::SameLine();
    const bool leave     = ImGui::Button(TR("Leave (Esc)"));
    big.controls_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(8);

    // Last: a detached board's view goes with it.
    if (leave) {
        if (main_window) {
            leaveBigScreen();
        } else {
            leaveBigBoard(big.panel);
        }
    }
}

}  // namespace fcg
