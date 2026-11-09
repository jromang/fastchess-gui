#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <imgui_internal.h>
#include <implot.h>

#include <fmt/include/fmt/format.h>

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

namespace {

ImFont* g_title_font = nullptr;
ImFont* g_icon_font  = nullptr;

// Lucide 1.59 code points (font/codepoints.json).
const char* glyph(Icon icon) {
    switch (icon) {
        case Icon::Play:
            return "\uE13C";  // play
        case Icon::Pause:
            return "\uE12E";  // pause
        case Icon::Stop:
            return "\uE167";  // square
        case Icon::Swap:
            return "\uE24A";  // arrow-left-right
        case Icon::Trophy:
            return "\uE373";  // trophy
        case Icon::Engine:
            return "\uE0A9";  // cpu
        case Icon::Log:
            return "\uE45F";  // scroll-text
        case Icon::Settings:
            return "\uE154";  // settings
        case Icon::Plus:
            return "\uE13D";  // plus
        case Icon::Folder:
            return "\uE247";  // folder-open
        case Icon::Back:
            return "\uE048";  // arrow-left
        case Icon::Chart:
            return "\uE2A3";  // chart-column
    }
    return "";
}

}  // namespace

void setFonts(ImFont* text, ImFont* titles, ImFont* icons) {
    ImGui::GetIO().FontDefault = text;
    g_title_font               = titles;
    g_icon_font                = icons;
}

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 col) {
    ImFont* font     = g_icon_font ? g_icon_font : ImGui::GetFont();
    const char* text = glyph(icon);
    const ImVec2 ext = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(font, size, ImVec2(c.x - ext.x * 0.5f, c.y - ext.y * 0.5f), col, text);
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
    ImGui::PushFont(g_title_font, ImGui::GetStyle().FontSizeBase * 1.45f);
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
