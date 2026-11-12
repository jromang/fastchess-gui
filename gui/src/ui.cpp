#include "ui.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

#include <SDL3/SDL_misc.h>
#include <imgui_internal.h>
#include <implot.h>

#include <fmt/include/fmt/format.h>

#include "i18n.hpp"

namespace fcg::ui {

namespace {

Theme g_theme = Theme::Light;

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
    c[ImGuiCol_WindowBg] = rgb(244, 245, 247);
    c[ImGuiCol_ChildBg]  = rgb(244, 245, 247, 0.0f);
    c[ImGuiCol_PopupBg]  = rgb(255, 255, 255, 0.98f);
    c[ImGuiCol_Border]   = rgb(222, 225, 231);
    // Grey, not white: fields, check boxes and radio buttons must show on the white cards too.
    c[ImGuiCol_FrameBg]             = rgb(232, 235, 240);
    c[ImGuiCol_FrameBgHovered]      = rgb(222, 228, 237);
    c[ImGuiCol_FrameBgActive]       = rgb(212, 223, 240);
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
    // Dates on time axes as 2026-11-09 and 14:05, not 11/9/26 and 2:05pm.
    ImPlot::GetStyle().UseISO8601     = true;
    ImPlot::GetStyle().Use24HourClock = true;

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

    if (h > 0) return trf("{} h {:02} min", h, m);
    if (m > 0) return trf("{} min {:02} s", m, s);
    return trf("{} s", s);
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
        case Icon::Book:
            return "\uE05F";  // book-open
        case Icon::Suite:
            return "\uE1D0";  // list-checks
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
        case Icon::Quit:
            return "\uE10E";  // log-out
    }
    return "";
}

}  // namespace

void setFonts(ImFont* text, ImFont* titles, ImFont* icons) {
    ImGui::GetIO().FontDefault = text;
    g_title_font               = titles;
    g_icon_font                = icons;
}

namespace {
bool g_cjk_requested = false;
bool g_cjk_loaded    = false;
}  // namespace

void requestCjkFont() { g_cjk_requested = true; }

void loadRequestedFonts(float size) {
    if (!g_cjk_requested || g_cjk_loaded) return;
    g_cjk_loaded = true;

    // A system font with the Chinese characters, read once and merged into the text and the title fonts: some
    // 20 MB, so only when Chinese is shown. Its glyphs are made when first drawn.
    struct Candidate {
        const char* path;
        int face;  // in a collection: Noto Sans CJK has JP, KR, SC (2), TC...
    };
#ifdef _WIN32
    const Candidate candidates[] = {{"C:\\Windows\\Fonts\\msyh.ttc", 0},
                                    {"C:\\Windows\\Fonts\\msyh.ttf", 0},
                                    {"C:\\Windows\\Fonts\\simsun.ttc", 0}};
#else
    const Candidate candidates[] = {{"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 2},
                                    {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 2},
                                    {"/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", 2},
                                    {"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", 0},
                                    {"/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc", 0}};
#endif
    static std::vector<unsigned char> data;  // the atlas reads from it for as long as the program runs
    for (const auto& c : candidates) {
        std::ifstream in(c.path, std::ios::binary);
        if (!in) continue;
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (data.empty()) continue;
        for (ImFont* font : {ImGui::GetIO().FontDefault, g_title_font}) {
            if (!font) continue;
            ImFontConfig config;
            config.MergeMode            = true;
            config.DstFont              = font;
            config.FontNo               = static_cast<ImU32>(c.face);
            config.FontDataOwnedByAtlas = false;
            ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data.data(), static_cast<int>(data.size()), size, &config);
        }
        return;
    }
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

float railButtonHeight() { return ImGui::GetFontSize() * 3.4f; }

bool railButton(const char* id, Icon icon, const char* label, bool selected, float width, float height) {
    const float h  = height > 0 ? height : railButtonHeight();
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
    const float k = ImGui::GetFontSize() / 16.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * k, 12 * k));
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

int takeCtrlWheel() {
    auto& io = ImGui::GetIO();
    if (!io.KeyCtrl || io.MouseWheel == 0) return 0;
    const int step = io.MouseWheel > 0 ? 1 : -1;
    io.MouseWheel  = 0;
    return step;
}

namespace {

// The arithmetic of a number field, read from left to right: expression := term (('+' | '-') term)*, term :=
// factor (('*' | '/') factor)*, factor := ('+' | '-') factor | number | '(' expression ')'.
class Arithmetic {
   public:
    explicit Arithmetic(std::string_view text) : text_(text) {}

    std::optional<double> value() {
        const auto v = expression(0);
        skipSpaces();
        if (!v || at_ != text_.size() || !std::isfinite(*v)) return std::nullopt;
        return v;
    }

   private:
    void skipSpaces() {
        while (at_ < text_.size() && text_[at_] == ' ') ++at_;
    }

    bool take(char c) {
        skipSpaces();
        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }

    std::optional<double> expression(int depth) {
        auto v = term(depth);
        while (v) {
            if (take('+')) {
                const auto r = term(depth);
                v            = r ? std::optional(*v + *r) : std::nullopt;
            } else if (take('-')) {
                const auto r = term(depth);
                v            = r ? std::optional(*v - *r) : std::nullopt;
            } else {
                break;
            }
        }
        return v;
    }

    std::optional<double> term(int depth) {
        auto v = factor(depth);
        while (v) {
            if (take('*')) {
                const auto r = factor(depth);
                v            = r ? std::optional(*v * *r) : std::nullopt;
            } else if (take('/')) {
                const auto r = factor(depth);
                v            = r && *r != 0 ? std::optional(*v / *r) : std::nullopt;
            } else {
                break;
            }
        }
        return v;
    }

    std::optional<double> factor(int depth) {
        if (depth > 64) return std::nullopt;  // parentheses without end
        if (take('-')) {
            const auto v = factor(depth + 1);
            return v ? std::optional(-*v) : std::nullopt;
        }
        if (take('+')) return factor(depth + 1);
        if (take('(')) {
            const auto v = expression(depth + 1);
            return v && take(')') ? v : std::nullopt;
        }
        return number();
    }

    std::optional<double> number() {
        skipSpaces();
        std::string digits;
        bool point = false;
        while (at_ < text_.size()) {
            const char c = text_[at_];
            if (c >= '0' && c <= '9') {
                digits += c;
            } else if ((c == '.' || c == ',') && !point) {
                digits += '.';
                point = true;
            } else {
                break;
            }
            ++at_;
        }
        if (digits.empty() || digits == ".") return std::nullopt;
        return std::strtod(digits.c_str(), nullptr);
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

// The characters arithmetic is typed with.
int arithmeticChars(ImGuiInputTextCallbackData* data) {
    const auto c  = data->EventChar;
    const bool ok = (c >= '0' && c <= '9') || c == '.' || c == ',' || c == '+' || c == '-' || c == '*' || c == '/' ||
                    c == '(' || c == ')' || c == ' ';
    return ok ? 0 : 1;
}

// A number field: `shown` is the value as text, `apply` takes what was typed, when the field is left.
template <typename Apply>
bool numberField(const char* label, const std::string& shown, Apply apply) {
    char buffer[64];
    const auto n = std::min(shown.size(), sizeof(buffer) - 1);
    std::copy_n(shown.data(), n, buffer);
    buffer[n] = 0;
    ImGui::InputText(label, buffer, sizeof(buffer),
                     ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CallbackCharFilter, arithmeticChars);
    // As ImGui's own number fields: applied when the field is left (Enter, Tab, a click elsewhere); Escape has put the
    // text back. Left unchanged, a value shown rounded stays as it is.
    if (!ImGui::IsItemDeactivated() || shown == buffer) return false;
    const auto value = evaluate(buffer);
    return value && apply(*value);
}

}  // namespace

std::optional<double> evaluate(std::string_view text) { return Arithmetic(text).value(); }

bool inputInt(const char* label, int* value) {
    return numberField(label, std::to_string(*value), [&](double v) {
        const int now = static_cast<int>(std::llround(std::clamp(v, double(INT_MIN), double(INT_MAX))));
        return std::exchange(*value, now) != now;
    });
}

bool inputInt64(const char* label, int64_t* value) {
    return numberField(label, std::to_string(*value), [&](double v) {
        // The largest double below 2^63, so that the conversion cannot overflow.
        const auto now =
            static_cast<int64_t>(std::llround(std::clamp(v, -9.2233720368547748e18, 9.2233720368547748e18)));
        return std::exchange(*value, now) != now;
    });
}

bool inputDouble(const char* label, double* value, const char* format) {
    char shown[64];
    std::snprintf(shown, sizeof(shown), format, *value);
    return numberField(label, shown, [&](double v) { return std::exchange(*value, v) != v; });
}

void drawBadge(ImDrawList* dl, ImVec2 center, const char* text) {
    const float size = ImGui::GetFontSize() * 0.75f;
    const ImVec2 ts  = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const float h    = size * 1.35f;
    const float w    = std::max(h, ts.x + size * 0.9f);
    const ImVec2 min = ImVec2(center.x - w * 0.5f, center.y - h * 0.5f);
    dl->AddRectFilled(min, ImVec2(min.x + w, min.y + h), ACCENT, h * 0.5f);
    dl->AddText(ImGui::GetFont(), size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), IM_COL32_WHITE, text);
}

float badgeWidth(const char* text) {
    const float size = ImGui::GetFontSize() * 0.75f;
    const float h    = size * 1.35f;
    return std::max(h, ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x + size * 0.9f);
}

void label(const char* text, float width) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(width);
}

float paneWidth(float fraction, float total, float min, float max) {
    const float width = (fraction > 0 ? fraction : 0.25f) * total;
    return std::floor(std::clamp(width, min, std::max(min, std::min(max, total - min))));
}

bool splitter(const char* id, float& fraction, float total) {
    const float width = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton(id, ImVec2(width, std::max(1.0f, ImGui::GetContentRegionAvail().y)));
    const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (hovered) {
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const float x = std::floor((min.x + max.x) * 0.5f);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), ACCENT, 2.0f);
    }
    bool changed = false;
    if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.x != 0 && total > 0) {
        fraction = std::clamp((fraction > 0 ? fraction : 0.25f) + ImGui::GetIO().MouseDelta.x / total, 0.1f, 0.8f);
        changed  = true;
    }
    ImGui::SameLine(0, 0);
    return changed;
}

void sameLineOrWrap(const char* next) {
    sameLineOrWrap(ImGui::CalcTextSize(next, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2);
}

void sameLineOrWrap(float width, float spacing) {
    ImGui::SameLine(0, spacing);
    if (ImGui::GetContentRegionAvail().x < width) ImGui::NewLine();
}

void emptyState(const char* text) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float wrap   = std::min(avail.x, ImGui::GetFontSize() * 24);
    const ImVec2 size  = ImGui::CalcTextSize(text, nullptr, false, wrap);
    const ImVec2 at    = ImGui::GetCursorPos();
    ImGui::SetCursorPos(
        ImVec2(at.x + std::max(0.0f, (avail.x - size.x) * 0.5f), at.y + std::max(0.0f, (avail.y - size.y) * 0.4f)));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

std::string fileUrl(const std::filesystem::path& path) {
    const auto u8   = path.generic_u8string();  // forward slashes, also on Windows: file:///C:/...
    std::string url = "file://";
    if (u8.empty() || u8.front() != u8'/') url += '/';
    for (const char8_t c : u8) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' ||
                           c == '-' || c == '.' || c == '_' || c == '~' || c == ':';
        if (plain)
            url += static_cast<char>(c);
        else
            url += fmt::format("%{:02X}", static_cast<unsigned>(c));
    }
    return url;
}

namespace {
std::string open_error;
}

void openPath(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    const std::string text(u8.begin(), u8.end());
#ifdef _WIN32
    // The path itself: ShellExecute opens it with Explorer, with no URL to escape.
    const auto& target = text;
#else
    const auto target = fileUrl(path);
#endif
    if (SDL_OpenURL(target.c_str())) return;
    open_error = fmt::format("{}\n{}", text, SDL_GetError());
    ImGui::SetClipboardText(text.c_str());
}

void drawOpenPathError() {
    if (!open_error.empty() && !ImGui::IsPopupOpen("##open_error")) ImGui::OpenPopup("##open_error");
    if (!ImGui::BeginPopupModal("##open_error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(TR("The system could not open:"));
    ImGui::TextDisabled("%s", open_error.c_str());
    ImGui::TextUnformatted(TR("Its path is in the clipboard."));
    if (ImGui::Button(TR("Close"))) {
        open_error.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

CopyAs copyButton(const char* id, bool small) {
    // The label is followed by room for a small arrow, drawn rather than taken from the font.
    const float arrow   = ImGui::GetFontSize() * 0.6f;
    const auto label    = fmt::format("{}     ##{}", TR("Copy"), id);
    const bool clicked  = small ? ImGui::SmallButton(label.c_str()) : ImGui::Button(label.c_str());
    const ImVec2 max    = ImGui::GetItemRectMax();
    const ImVec2 min    = ImGui::GetItemRectMin();
    const float padding = small ? 0.0f : ImGui::GetStyle().FramePadding.x;
    ImGui::RenderArrow(ImGui::GetWindowDrawList(),
                       ImVec2(max.x - padding - arrow * 1.4f, min.y + (max.y - min.y - arrow) * 0.5f),
                       ImGui::GetColorU32(ImGuiCol_Text), ImGuiDir_Down, 0.6f);

    const auto popup = fmt::format("##copy_menu_{}", id);
    if (clicked) ImGui::OpenPopup(popup.c_str());
    CopyAs choice = CopyAs::None;
    if (ImGui::BeginPopup(popup.c_str())) {
        if (ImGui::MenuItem(TR("As text"))) choice = CopyAs::Text;
        if (ImGui::MenuItem(TR("As BBCode, for a forum"))) choice = CopyAs::Bbcode;
        ImGui::EndPopup();
    }
    return choice;
}

int searchComboItems(const std::vector<std::string>& names, const std::string& current,
                     const std::vector<std::string>* more) {
    static std::string search;  // one combo is open at a time
    const bool appearing = ImGui::IsWindowAppearing();
    if (appearing) {
        search.clear();
        ImGui::SetKeyboardFocusHere();
    }
    char buffer[128];
    const auto n = std::min(search.size(), sizeof(buffer) - 1);
    std::copy_n(search.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##combo_search", TR("Search"), buffer, sizeof(buffer))) search = buffer;
    const bool enter = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);

    const auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const auto needle = lower(search);
    std::vector<int> shown;
    int current_row = -1;
    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
        const bool found = needle.empty() || lower(names[i]).find(needle) != std::string::npos ||
                           (more && i < static_cast<int>(more->size()) &&
                            lower((*more)[i]).find(needle) != std::string::npos);
        if (!found) continue;
        if (names[i] == current) current_row = static_cast<int>(shown.size());
        shown.push_back(i);
    }
    if (enter && !shown.empty()) {
        ImGui::CloseCurrentPopup();
        return shown.front();
    }
    if (shown.empty()) {
        if (!names.empty()) ImGui::TextDisabled("%s", TR("Nothing found."));  // the caller says why there is nothing
        return -1;
    }

    // The rows scroll under the search field.
    const float row    = ImGui::GetTextLineHeightWithSpacing();
    const float height = std::min(12.0f, static_cast<float>(shown.size())) * row + ImGui::GetStyle().WindowPadding.y;
    int chosen         = -1;
    ImGui::BeginChild("##combo_rows", ImVec2(0, height));
    if (appearing && current_row >= 0) ImGui::SetScrollY(std::max(0.0f, (current_row - 5) * row));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown.size()));
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            ImGui::PushID(shown[r]);
            if (ImGui::Selectable(names[shown[r]].c_str(), r == current_row)) chosen = shown[r];
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    // In a child window, a row does not close the combo by itself.
    if (chosen >= 0) ImGui::CloseCurrentPopup();
    return chosen;
}

}  // namespace fcg::ui
