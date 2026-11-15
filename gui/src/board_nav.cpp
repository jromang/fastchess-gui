#include "board_nav.hpp"

#include <algorithm>

#include <imgui.h>

#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

int stepPly(NavStep step, int ply, int total) noexcept {
    total = std::max(0, total);
    switch (step) {
        case NavStep::First:
            return 0;
        case NavStep::Previous:
            return std::clamp(ply - 1, 0, total);
        case NavStep::Next:
            return std::clamp(ply + 1, 0, total);
        case NavStep::Last:
            return total;
        case NavStep::None:
            break;
    }
    return std::clamp(ply, 0, total);
}

int FollowedPly::shown(int total) const noexcept { return held(total) ? std::max(0, *held_) : std::max(0, total); }

bool FollowedPly::held(int total) const noexcept { return held_ && *held_ < total; }

void FollowedPly::show(int ply, int total) noexcept {
    if (ply >= total) {
        held_.reset();
    } else {
        held_ = std::max(0, ply);
    }
}

void FollowedPly::step(NavStep step, int total) noexcept {
    if (step != NavStep::None) show(stepPly(step, shown(total), total), total);
}

NavStep navKeys(bool& flip) {
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || io.KeyCtrl || io.KeyAlt || io.KeySuper) return NavStep::None;
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return NavStep::None;
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) flip = !flip;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) return NavStep::Previous;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) return NavStep::Next;
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) return NavStep::First;
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) return NavStep::Last;
    return NavStep::None;
}

float boardNavHeight() { return ImGui::GetFrameHeightWithSpacing(); }

namespace {

// A square button with an icon, its tooltip shown also while it is disabled; `on`: drawn pressed (Flip).
bool navButton(const char* id, ui::Icon icon, bool enabled, const char* tip, bool on = false) {
    const float h = ImGui::GetFrameHeight();
    ImGui::BeginDisabled(!enabled);
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    const bool pressed = ImGui::Button(id, ImVec2(h, h));
    if (on) ImGui::PopStyleColor();
    const ImVec2 min = ImGui::GetItemRectMin();
    ui::drawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(min.x + h * 0.5f, min.y + h * 0.5f), h * 0.6f,
                 ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
    return pressed && enabled;
}

}  // namespace

NavClick drawBoardNav(const char* id, int ply, int total, bool& flip, bool buttons, bool live, float width) {
    NavClick click;
    ImGui::PushID(id);
    const float gap   = ImGui::GetStyle().ItemInnerSpacing.x;
    const float start = ImGui::GetCursorScreenPos().x;
    bool first        = true;
    if (buttons) {
        ImGui::BeginGroup();
        if (navButton("##first", ui::Icon::First, ply > 0, TR("Start position (Home)"))) click.step = NavStep::First;
        ImGui::SameLine(0, gap);
        if (navButton("##previous", ui::Icon::Previous, ply > 0, TR("Previous move (←)")))
            click.step = NavStep::Previous;
        ImGui::SameLine(0, gap);
        if (navButton("##next", ui::Icon::Next, ply < total, TR("Next move (→)"))) click.step = NavStep::Next;
        ImGui::SameLine(0, gap);
        if (navButton("##last", ui::Icon::Last, ply < total, TR("Last move (End)"))) click.step = NavStep::Last;
        ImGui::SameLine(0, gap * 3);
        if (navButton("##flip", ui::Icon::Flip, true, TR("Flip the board (F)"), flip)) flip = !flip;
        ImGui::EndGroup();
        // Right click on the row: hide it, for those who use the keys.
        if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()) &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("##nav_menu");
        }
        if (ImGui::BeginPopup("##nav_menu")) {
            if (ImGui::MenuItem(TR("Hide the navigation buttons"))) click.hide = true;
            ImGui::TextDisabled("%s", TR("The keys still work. Settings or the command palette shows them again."));
            ImGui::EndPopup();
        }
        first = false;
    }

    if (live && ply < total) {
        const char* back    = TR("Back to the current move");
        const float h       = ImGui::GetFrameHeight();
        const float used    = first ? 0.0f : ImGui::GetItemRectMax().x - start;
        const float spacing = first ? 0.0f : ImGui::GetStyle().ItemSpacing.x;
        const float words   = h * 0.42f + ImGui::CalcTextSize(back).x + ImGui::GetStyle().FramePadding.x * 2 +
                              ImGui::GetStyle().ItemInnerSpacing.x;
        if (!first) ImGui::SameLine(0, spacing);
        // In the accent colour: the board no longer follows the game.
        const ImVec4 accent = ImGui::ColorConvertU32ToFloat4(ui::ACCENT);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 0.50f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(accent.x, accent.y, accent.z, 0.70f));
        const bool room = used + spacing + words <= width;
        if (room ? ui::iconButton("##back", ui::Icon::Live, back) : navButton("##back", ui::Icon::Live, true, back))
            click.step = NavStep::Last;
        ImGui::PopStyleColor(3);
        if (room && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("The board follows the game again (End)."));
        }
    }
    ImGui::PopID();
    return click;
}

bool drawBackOverBoard() {
    const ImVec2 board_min = ImGui::GetItemRectMin();
    const ImVec2 board_max = ImGui::GetItemRectMax();
    const bool board_hover = ImGui::IsItemHovered();

    const char* text  = TR("Back to the current move");
    const auto& style = ImGui::GetStyle();
    const ImVec2 ext  = ImGui::CalcTextSize(text);
    const ImVec2 size(ext.x + style.FramePadding.x * 2, ext.y + style.FramePadding.y * 2);
    const float mid = (board_min.x + board_max.x) * 0.5f;
    const ImVec2 min(mid - size.x * 0.5f, board_max.y - size.y - style.ItemSpacing.y * 2);
    const ImVec2 max(min.x + size.x, min.y + size.y);

    const bool hovered = board_hover && ImGui::IsMouseHoveringRect(min, max);
    ImDrawList* dl     = ImGui::GetWindowDrawList();
    const ImVec4 a     = ImGui::ColorConvertU32ToFloat4(ui::ACCENT);
    dl->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, hovered ? 1.0f : 0.85f)),
                      size.y * 0.5f);
    dl->AddText(ImVec2(min.x + style.FramePadding.x, min.y + style.FramePadding.y), IM_COL32(255, 255, 255, 255), text);
    if (hovered) ImGui::SetTooltip("%s", TR("The board follows the game again (End)."));
    return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

}  // namespace fcg
