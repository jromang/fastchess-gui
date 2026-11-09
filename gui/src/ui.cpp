#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <imgui_internal.h>
#include <implot.h>

#include <fmt/include/fmt/core.h>

namespace fcg::ui {

namespace {

Theme g_theme = Theme::Dark;

ImVec4 rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

void darkColors(ImVec4* c) {
    c[ImGuiCol_WindowBg]            = rgb(18, 20, 24);
    c[ImGuiCol_ChildBg]             = rgb(18, 20, 24, 0.0f);
    c[ImGuiCol_PopupBg]             = rgb(28, 31, 37, 0.98f);
    c[ImGuiCol_Border]              = rgb(38, 42, 50);
    c[ImGuiCol_FrameBg]             = rgb(29, 33, 41);
    c[ImGuiCol_FrameBgHovered]      = rgb(37, 42, 52);
    c[ImGuiCol_FrameBgActive]       = rgb(42, 49, 64);
    c[ImGuiCol_TitleBg]             = rgb(13, 15, 18);
    c[ImGuiCol_TitleBgActive]       = rgb(21, 24, 29);
    c[ImGuiCol_MenuBarBg]           = rgb(21, 24, 29);
    c[ImGuiCol_Header]              = rgb(30, 38, 51);
    c[ImGuiCol_HeaderHovered]       = rgb(37, 48, 66);
    c[ImGuiCol_HeaderActive]        = rgb(42, 56, 80);
    c[ImGuiCol_Button]              = rgb(29, 33, 41);
    c[ImGuiCol_ButtonHovered]       = rgb(37, 46, 62);
    c[ImGuiCol_ButtonActive]        = rgb(45, 58, 80);
    c[ImGuiCol_Tab]                 = rgb(21, 24, 29);
    c[ImGuiCol_TabHovered]          = rgb(37, 46, 62);
    c[ImGuiCol_TabSelected]         = rgb(30, 38, 51);
    c[ImGuiCol_TabSelectedOverline] = rgb(91, 157, 255);
    c[ImGuiCol_TabDimmed]           = rgb(21, 24, 29);
    c[ImGuiCol_TabDimmedSelected]   = rgb(26, 32, 42);
    c[ImGuiCol_CheckMark]           = rgb(91, 157, 255);
    c[ImGuiCol_SliderGrab]          = rgb(91, 157, 255);
    c[ImGuiCol_SliderGrabActive]    = rgb(127, 178, 255);
    c[ImGuiCol_PlotHistogram]       = rgb(91, 157, 255);
    c[ImGuiCol_TableHeaderBg]       = rgb(24, 27, 33);
    c[ImGuiCol_TableBorderLight]    = rgb(32, 36, 43);
    c[ImGuiCol_TableBorderStrong]   = rgb(38, 42, 50);
    c[ImGuiCol_TableRowBgAlt]       = rgb(255, 255, 255, 0.025f);
    c[ImGuiCol_Text]                = rgb(232, 234, 238);
    c[ImGuiCol_TextDisabled]        = rgb(138, 145, 158);
    c[ImGuiCol_Separator]           = rgb(38, 42, 50);
}

void lightColors(ImVec4* c) {
    c[ImGuiCol_WindowBg]            = rgb(244, 245, 247);
    c[ImGuiCol_ChildBg]             = rgb(244, 245, 247, 0.0f);
    c[ImGuiCol_PopupBg]             = rgb(255, 255, 255, 0.98f);
    c[ImGuiCol_Border]              = rgb(222, 225, 231);
    c[ImGuiCol_FrameBg]             = rgb(255, 255, 255);
    c[ImGuiCol_FrameBgHovered]      = rgb(236, 240, 246);
    c[ImGuiCol_FrameBgActive]       = rgb(224, 232, 245);
    c[ImGuiCol_TitleBg]             = rgb(233, 236, 240);
    c[ImGuiCol_TitleBgActive]       = rgb(255, 255, 255);
    c[ImGuiCol_MenuBarBg]           = rgb(255, 255, 255);
    c[ImGuiCol_Header]              = rgb(221, 231, 248);
    c[ImGuiCol_HeaderHovered]       = rgb(207, 222, 247);
    c[ImGuiCol_HeaderActive]        = rgb(190, 210, 244);
    c[ImGuiCol_Button]              = rgb(240, 242, 245);
    c[ImGuiCol_ButtonHovered]       = rgb(221, 231, 248);
    c[ImGuiCol_ButtonActive]        = rgb(200, 216, 245);
    c[ImGuiCol_Tab]                 = rgb(255, 255, 255);
    c[ImGuiCol_TabHovered]          = rgb(221, 231, 248);
    c[ImGuiCol_TabSelected]         = rgb(236, 241, 250);
    c[ImGuiCol_TabSelectedOverline] = rgb(47, 111, 222);
    c[ImGuiCol_TabDimmed]           = rgb(255, 255, 255);
    c[ImGuiCol_TabDimmedSelected]   = rgb(240, 243, 248);
    c[ImGuiCol_CheckMark]           = rgb(47, 111, 222);
    c[ImGuiCol_SliderGrab]          = rgb(47, 111, 222);
    c[ImGuiCol_SliderGrabActive]    = rgb(36, 89, 184);
    c[ImGuiCol_PlotHistogram]       = rgb(47, 111, 222);
    c[ImGuiCol_TableHeaderBg]       = rgb(238, 240, 244);
    c[ImGuiCol_TableBorderLight]    = rgb(230, 233, 238);
    c[ImGuiCol_TableBorderStrong]   = rgb(222, 225, 231);
    c[ImGuiCol_TableRowBgAlt]       = rgb(0, 0, 0, 0.025f);
    c[ImGuiCol_Text]                = rgb(27, 30, 36);
    c[ImGuiCol_TextDisabled]        = rgb(98, 105, 118);
    c[ImGuiCol_Separator]           = rgb(222, 225, 231);
}

}  // namespace

void applyTheme(Theme theme, float scale) {
    g_theme = theme;

    auto& style           = ImGui::GetStyle();
    const float font_dpi  = style.FontScaleDpi;
    const float font_main = style.FontScaleMain;
    const float font_base = style.FontSizeBase;
    style                 = ImGuiStyle();
    style.FontScaleDpi    = font_dpi;
    style.FontScaleMain   = font_main;
    style.FontSizeBase    = font_base;

    if (theme == Theme::Dark) {
        ImGui::StyleColorsDark();
        darkColors(style.Colors);
        ImPlot::StyleColorsDark();
        ACCENT  = IM_COL32(91, 157, 255, 255);
        GOOD    = IM_COL32(95, 211, 154, 255);
        BAD     = IM_COL32(255, 143, 135, 255);
        WARN    = IM_COL32(240, 163, 91, 255);
        MUTED   = IM_COL32(138, 145, 158, 255);
        MUTED4  = rgb(138, 145, 158);
        RAIL_BG = IM_COL32(13, 15, 18, 255);
        HEAD_BG = IM_COL32(21, 24, 29, 255);
        CARD_BG = IM_COL32(26, 29, 35, 255);
        CARD_BD = IM_COL32(38, 42, 50, 255);
        SELECT  = IM_COL32(30, 38, 51, 255);
    } else {
        ImGui::StyleColorsLight();
        lightColors(style.Colors);
        ImPlot::StyleColorsLight();
        ACCENT  = IM_COL32(47, 111, 222, 255);
        GOOD    = IM_COL32(29, 127, 78, 255);
        BAD     = IM_COL32(192, 59, 51, 255);
        WARN    = IM_COL32(168, 88, 15, 255);
        MUTED   = IM_COL32(98, 105, 118, 255);
        MUTED4  = rgb(98, 105, 118);
        RAIL_BG = IM_COL32(233, 236, 240, 255);
        HEAD_BG = IM_COL32(255, 255, 255, 255);
        CARD_BG = IM_COL32(255, 255, 255, 255);
        CARD_BD = IM_COL32(224, 227, 232, 255);
        SELECT  = IM_COL32(221, 227, 236, 255);
    }

    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 8.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 6.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 5.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBarBorderSize  = 1.0f;
    style.ItemSpacing       = ImVec2(8, 7);
    style.FramePadding      = ImVec2(8, 5);
    style.WindowPadding     = ImVec2(12, 10);
    style.CellPadding       = ImVec2(8, 5);
    style.ScaleAllSizes(scale);
}

Theme currentTheme() noexcept { return g_theme; }

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

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 col) {
    const float r  = size * 0.5f;
    const float t  = std::max(1.0f, size * 0.09f);  // stroke
    const float cx = c.x, cy = c.y;

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
        case Icon::Trophy: {
            // Cup, handles, stem and base.
            dl->PathLineTo(ImVec2(cx - r * 0.6f, cy - r));
            dl->PathLineTo(ImVec2(cx + r * 0.6f, cy - r));
            dl->PathLineTo(ImVec2(cx + r * 0.6f, cy - r * 0.35f));
            dl->PathArcTo(ImVec2(cx, cy - r * 0.35f), r * 0.6f, 0.0f, IM_PI);
            dl->PathStroke(col, ImDrawFlags_Closed, t);
            dl->AddBezierQuadratic(ImVec2(cx + r * 0.6f, cy - r * 0.8f), ImVec2(cx + r * 1.05f, cy - r * 0.75f),
                                   ImVec2(cx + r * 0.5f, cy - r * 0.05f), col, t);
            dl->AddBezierQuadratic(ImVec2(cx - r * 0.6f, cy - r * 0.8f), ImVec2(cx - r * 1.05f, cy - r * 0.75f),
                                   ImVec2(cx - r * 0.5f, cy - r * 0.05f), col, t);
            dl->AddLine(ImVec2(cx, cy + r * 0.25f), ImVec2(cx, cy + r * 0.75f), col, t);
            dl->AddLine(ImVec2(cx - r * 0.5f, cy + r * 0.85f), ImVec2(cx + r * 0.5f, cy + r * 0.85f), col, t);
            break;
        }
        case Icon::Engine: {
            // A chip: square, inner square, pins.
            dl->AddRect(ImVec2(cx - r * 0.62f, cy - r * 0.62f), ImVec2(cx + r * 0.62f, cy + r * 0.62f), col, r * 0.15f,
                        0, t);
            dl->AddRect(ImVec2(cx - r * 0.25f, cy - r * 0.25f), ImVec2(cx + r * 0.25f, cy + r * 0.25f), col, 0, 0, t);
            for (const float o : {-0.3f, 0.3f}) {
                dl->AddLine(ImVec2(cx + r * o, cy - r * 0.62f), ImVec2(cx + r * o, cy - r), col, t);
                dl->AddLine(ImVec2(cx + r * o, cy + r * 0.62f), ImVec2(cx + r * o, cy + r), col, t);
                dl->AddLine(ImVec2(cx - r * 0.62f, cy + r * o), ImVec2(cx - r, cy + r * o), col, t);
                dl->AddLine(ImVec2(cx + r * 0.62f, cy + r * o), ImVec2(cx + r, cy + r * o), col, t);
            }
            break;
        }
        case Icon::Log:
            for (int i = -1; i <= 1; ++i) {
                const float y = cy + i * r * 0.6f;
                dl->AddCircleFilled(ImVec2(cx - r * 0.8f, y), t * 0.8f, col);
                dl->AddLine(ImVec2(cx - r * 0.45f, y), ImVec2(cx + r, y), col, t);
            }
            break;
        case Icon::Settings: {
            // A gear: ring and eight teeth.
            dl->AddCircle(ImVec2(cx, cy), r * 0.5f, col, 0, t);
            dl->AddCircle(ImVec2(cx, cy), r * 0.18f, col, 0, t);
            for (int i = 0; i < 8; ++i) {
                const float a = i * IM_PI / 4.0f;
                dl->AddLine(ImVec2(cx + std::cos(a) * r * 0.55f, cy + std::sin(a) * r * 0.55f),
                            ImVec2(cx + std::cos(a) * r * 0.92f, cy + std::sin(a) * r * 0.92f), col, t * 1.6f);
            }
            break;
        }
        case Icon::Plus:
            dl->AddLine(ImVec2(cx - r * 0.8f, cy), ImVec2(cx + r * 0.8f, cy), col, t * 1.3f);
            dl->AddLine(ImVec2(cx, cy - r * 0.8f), ImVec2(cx, cy + r * 0.8f), col, t * 1.3f);
            break;
        case Icon::Folder:
            dl->PathLineTo(ImVec2(cx - r, cy - r * 0.7f));
            dl->PathLineTo(ImVec2(cx - r * 0.25f, cy - r * 0.7f));
            dl->PathLineTo(ImVec2(cx, cy - r * 0.4f));
            dl->PathLineTo(ImVec2(cx + r, cy - r * 0.4f));
            dl->PathLineTo(ImVec2(cx + r, cy + r * 0.7f));
            dl->PathLineTo(ImVec2(cx - r, cy + r * 0.7f));
            dl->PathStroke(col, ImDrawFlags_Closed, t);
            break;
        case Icon::Chart:
            for (int i = 0; i < 3; ++i) {
                const float x = cx + (i - 1) * r * 0.62f;
                const float h = r * (0.7f + 0.45f * i);
                dl->AddLine(ImVec2(x, cy + r * 0.85f), ImVec2(x, cy + r * 0.85f - h * 1.2f), col, t * 1.8f);
            }
            dl->AddLine(ImVec2(cx - r, cy + r * 0.95f), ImVec2(cx + r, cy + r * 0.95f), col, t);
            break;
        case Icon::Back:
            dl->AddLine(ImVec2(cx + r * 0.3f, cy - r * 0.7f), ImVec2(cx - r * 0.4f, cy), col, t * 1.3f);
            dl->AddLine(ImVec2(cx - r * 0.4f, cy), ImVec2(cx + r * 0.3f, cy + r * 0.7f), col, t * 1.3f);
            break;
    }
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
    drawIcon(dl, icon, ImVec2(p0.x + style.FramePadding.x + icon_sz * 0.5f, p0.y + h * 0.5f), icon_sz, col);

    if (text.x > 0) {
        dl->AddText(
            ImVec2(p0.x + style.FramePadding.x + icon_sz + style.ItemInnerSpacing.x, p0.y + style.FramePadding.y), col,
            label, ImGui::FindRenderedTextEnd(label));
    }

    ImGui::EndDisabled();
    return pressed && enabled;
}

bool beginPanel(const char* title, ImVec2 size) {
    // The id is what follows "###", which does not change with the language.
    const char* id = std::strstr(title, "###");
    return ImGui::BeginChild(id ? id + 3 : title, size, ImGuiChildFlags_None);
}

void endPanel() { ImGui::EndChild(); }

bool railButton(const char* id, Icon icon, const char* label, bool selected, float width) {
    const float h  = ImGui::GetFontSize() * 3.4f;
    const auto pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(id);
    const bool pressed = ImGui::InvisibleButton("##rail", ImVec2(width, h));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    const float pad  = width * 0.08f;
    const ImVec2 min = ImVec2(pos.x + pad, pos.y);
    const ImVec2 max = ImVec2(pos.x + width - pad, pos.y + h);
    if (selected || hovered)
        dl->AddRectFilled(min, max, selected ? SELECT : ImGui::GetColorU32(ImGuiCol_FrameBgHovered), 8.0f);

    const ImU32 col = selected ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
    drawIcon(dl, icon, ImVec2(pos.x + width * 0.5f, pos.y + h * 0.36f), ImGui::GetFontSize() * 1.15f, col);

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.8f);
    const ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    dl->AddText(ImVec2(pos.x + (width - ts.x) * 0.5f, pos.y + h * 0.66f), col, label,
                ImGui::FindRenderedTextEnd(label));
    ImGui::PopFont();
    return pressed;
}

void pageTitle(const char* text) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.45f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void beginCard(const char* id, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(CARD_BG));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(CARD_BD));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    ImGui::BeginChild(id, size,
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding |
                          (size.y == 0 ? ImGuiChildFlags_AutoResizeY : 0));
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
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
