#include "ui.hpp"

#include <algorithm>
#include <cmath>

#include <imgui_internal.h>

#include <fmt/include/fmt/core.h>

namespace fcg::ui {

void applyTheme(float scale) {
    ImGui::StyleColorsDark();

    auto& style             = ImGui::GetStyle();
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 3.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.ItemSpacing       = ImVec2(8, 6);
    style.FramePadding      = ImVec2(6, 4);
    style.WindowPadding     = ImVec2(10, 8);

    auto* c                       = style.Colors;
    c[ImGuiCol_WindowBg]          = ImVec4(0.11f, 0.12f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg]           = ImVec4(0.11f, 0.12f, 0.13f, 0.00f);
    c[ImGuiCol_PopupBg]           = ImVec4(0.13f, 0.14f, 0.15f, 0.98f);
    c[ImGuiCol_Border]            = ImVec4(0.22f, 0.23f, 0.25f, 1.00f);
    c[ImGuiCol_FrameBg]           = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.22f, 0.24f, 0.27f, 1.00f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.25f, 0.28f, 0.32f, 1.00f);
    c[ImGuiCol_TitleBg]           = ImVec4(0.09f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
    c[ImGuiCol_MenuBarBg]         = ImVec4(0.09f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_Header]            = ImVec4(0.20f, 0.25f, 0.31f, 1.00f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.24f, 0.31f, 0.40f, 1.00f);
    c[ImGuiCol_HeaderActive]      = ImVec4(0.27f, 0.36f, 0.47f, 1.00f);
    c[ImGuiCol_Button]            = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.26f, 0.32f, 0.40f, 1.00f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.30f, 0.38f, 0.48f, 1.00f);
    c[ImGuiCol_Tab]               = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
    c[ImGuiCol_TabHovered]        = ImVec4(0.26f, 0.32f, 0.40f, 1.00f);
    c[ImGuiCol_TabSelected]       = ImVec4(0.21f, 0.26f, 0.32f, 1.00f);
    c[ImGuiCol_TabDimmed]         = ImVec4(0.12f, 0.13f, 0.14f, 1.00f);
    c[ImGuiCol_TabDimmedSelected] = ImVec4(0.17f, 0.20f, 0.24f, 1.00f);
    c[ImGuiCol_CheckMark]         = ImVec4(0.43f, 0.66f, 0.87f, 1.00f);
    c[ImGuiCol_SliderGrab]        = ImVec4(0.43f, 0.66f, 0.87f, 1.00f);
    c[ImGuiCol_SliderGrabActive]  = ImVec4(0.53f, 0.74f, 0.93f, 1.00f);
    c[ImGuiCol_PlotHistogram]     = ImVec4(0.43f, 0.66f, 0.87f, 1.00f);
    c[ImGuiCol_DockingPreview]    = ImVec4(0.43f, 0.66f, 0.87f, 0.50f);
    c[ImGuiCol_TableHeaderBg]     = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
    c[ImGuiCol_TableRowBgAlt]     = ImVec4(1.00f, 1.00f, 1.00f, 0.025f);

    style.ScaleAllSizes(scale);
}

std::string formatClock(int64_t ms) {
    if (ms < 0) ms = 0;
    const auto h  = ms / 3'600'000;
    const auto m  = ms / 60'000 % 60;
    const auto s  = ms / 1000 % 60;
    const auto ds = ms / 100 % 10;

    if (h > 0) return fmt::format("{}:{:02}:{:02}", h, m, s);
    if (m > 0) return fmt::format("{}:{:02}.{}", m, s, ds);
    if (s >= 10) return fmt::format("{}.{}s", s, ds);
    return fmt::format("{:.2f}s", ms / 1000.0);
}

std::string formatCount(uint64_t n) {
    if (n >= 10'000'000'000ULL) return fmt::format("{:.0f}G", n / 1e9);
    if (n >= 1'000'000'000ULL) return fmt::format("{:.1f}G", n / 1e9);
    if (n >= 10'000'000ULL) return fmt::format("{:.0f}M", n / 1e6);
    if (n >= 1'000'000ULL) return fmt::format("{:.1f}M", n / 1e6);
    if (n >= 10'000ULL) return fmt::format("{:.0f}k", n / 1e3);
    if (n >= 1'000ULL) return fmt::format("{:.1f}k", n / 1e3);
    return fmt::format("{}", n);
}

std::string formatScore(const std::optional<fastchess::Score>& score) {
    if (!score) return "";
    if (score->type == fastchess::ScoreType::MATE) return fmt::format("#{}", score->value);
    if (score->type == fastchess::ScoreType::CP) return fmt::format("{:+.2f}", score->value / 100.0);
    return "?";
}

float whitePawns(const std::optional<fastchess::Score>& score, chess::Color mover, float limit) {
    if (!score) return std::nanf("");

    const float sign = mover == chess::Color::WHITE ? 1.0f : -1.0f;
    if (score->type == fastchess::ScoreType::MATE) return score->value >= 0 ? sign * limit : -sign * limit;
    if (score->type != fastchess::ScoreType::CP) return std::nanf("");

    return std::clamp(sign * static_cast<float>(score->value) / 100.0f, -limit, limit);
}

std::string formatDuration(double seconds) {
    if (!(seconds >= 0)) seconds = 0;
    const auto total = static_cast<int64_t>(seconds);
    const auto h     = total / 3600;
    const auto m     = total / 60 % 60;
    const auto s     = total % 60;

    if (h > 0) return fmt::format("{} h {:02} min", h, m);
    if (m > 0) return fmt::format("{} min {:02} s", m, s);
    return fmt::format("{} s", s);
}

bool iconButton(const char* id, Icon icon, const char* label, bool enabled) {
    ImGui::BeginDisabled(!enabled);

    const float h       = ImGui::GetFrameHeight();
    const float icon_sz = h * 0.42f;
    const ImVec2 text   = ImGui::CalcTextSize(label, nullptr, true);
    const auto& style   = ImGui::GetStyle();
    const ImVec2 size(style.FramePadding.x * 2 + icon_sz + (text.x > 0 ? style.ItemInnerSpacing.x + text.x : 0), h);

    ImGui::PushID(id);
    const bool pressed = ImGui::Button("##icon", size);
    ImGui::PopID();

    const ImVec2 p0 = ImGui::GetItemRectMin();
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    const float cx  = p0.x + style.FramePadding.x + icon_sz * 0.5f;
    const float cy  = p0.y + h * 0.5f;
    const float r   = icon_sz * 0.5f;

    switch (icon) {
        case Icon::Play:
            dl->AddTriangleFilled(ImVec2(cx - r * 0.75f, cy - r), ImVec2(cx - r * 0.75f, cy + r), ImVec2(cx + r, cy),
                                  GOOD);
            break;
        case Icon::Pause:
            dl->AddRectFilled(ImVec2(cx - r * 0.8f, cy - r), ImVec2(cx - r * 0.25f, cy + r), col);
            dl->AddRectFilled(ImVec2(cx + r * 0.25f, cy - r), ImVec2(cx + r * 0.8f, cy + r), col);
            break;
        case Icon::Stop:
            dl->AddRectFilled(ImVec2(cx - r * 0.85f, cy - r * 0.85f), ImVec2(cx + r * 0.85f, cy + r * 0.85f), BAD);
            break;
        case Icon::Swap:
            dl->AddLine(ImVec2(cx - r, cy - r * 0.45f), ImVec2(cx + r, cy - r * 0.45f), col, 1.5f);
            dl->AddTriangleFilled(ImVec2(cx + r, cy - r * 0.9f), ImVec2(cx + r, cy),
                                  ImVec2(cx + r * 1.4f, cy - r * 0.45f), col);
            dl->AddLine(ImVec2(cx - r, cy + r * 0.45f), ImVec2(cx + r, cy + r * 0.45f), col, 1.5f);
            dl->AddTriangleFilled(ImVec2(cx - r, cy), ImVec2(cx - r, cy + r * 0.9f),
                                  ImVec2(cx - r * 1.4f, cy + r * 0.45f), col);
            break;
    }

    if (text.x > 0) {
        dl->AddText(
            ImVec2(p0.x + style.FramePadding.x + icon_sz + style.ItemInnerSpacing.x, p0.y + style.FramePadding.y), col,
            label, ImGui::FindRenderedTextEnd(label));
    }

    ImGui::EndDisabled();
    return pressed && enabled;
}

void help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void label(const char* text, float width) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(width);
}

}  // namespace fcg::ui
