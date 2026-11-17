#pragma once

// Small formatting and widget helpers shared by the views.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include <chess.hpp>
#include <types/score.hpp>

#include "guide.hpp"

namespace fcg::ui {

enum class Theme { Dark, Light };

// Applies a theme to Dear ImGui and ImPlot and sets the colours below. Can be called again to switch: the style
// is rebuilt from scratch, then scaled.
void applyTheme(Theme theme, float scale);
[[nodiscard]] Theme currentTheme() noexcept;

// Colours of the current theme, for custom drawing, plots and highlights.
inline ImU32 ACCENT  = IM_COL32(110, 168, 222, 255);
inline ImU32 GOOD    = IM_COL32(120, 200, 120, 255);
inline ImU32 BAD     = IM_COL32(230, 110, 100, 255);
inline ImU32 WARN    = IM_COL32(240, 163, 91, 255);
inline ImU32 MUTED   = IM_COL32(150, 150, 150, 255);
inline ImVec4 MUTED4 = ImVec4(0.59f, 0.59f, 0.59f, 1.0f);
inline ImU32 RAIL_BG = IM_COL32(13, 15, 18, 255);  // navigation rail
inline ImU32 HEAD_BG = IM_COL32(21, 24, 29, 255);  // page and tournament headers
inline ImU32 CARD_BG = IM_COL32(26, 29, 35, 255);  // cards on a page
inline ImU32 CARD_BD = IM_COL32(38, 42, 50, 255);  // card borders
inline ImU32 SELECT  = IM_COL32(30, 38, 51, 255);  // selected rail item

// "1:02:03", "2:03.4" or "4.2s".
std::string formatClock(int64_t ms);

// 12345678 -> "12.3M"
std::string formatCount(uint64_t n);

// Engine score from the mover's point of view, as "+0.35" or "#5".
std::string formatScore(const std::optional<fastchess::Score>& score);

// White point of view, in pawns, clamped to +-limit; mates map to +-limit.
float whitePawns(const std::optional<fastchess::Score>& score, chess::Color mover, float limit = 8.0f);

// Duration "1 h 02 min", "3 min 05 s", "12 s".
std::string formatDuration(double seconds);

enum class Icon {
    Play,
    Pause,
    Stop,
    Swap,
    Trophy,
    Engine,
    Book,
    Suite,
    Log,
    Settings,
    Plus,
    Folder,
    Back,
    Chart,
    Layout,
    Quit,
    Pencil,  // edit in place
    More,    // ⋯: a menu of what else can be done
    Star,    // a seed
    Board,   // a knight: the Board page
    Paste,
    Edit,
    Analyse,
    Unload,  // power: ends an engine's process
    // Through a game's moves, under the boards.
    First,
    Previous,
    Next,
    Last,
    Flip,
    Live,  // back to the move being played
    // The guide panel.
    Forward,
    External,  // open in the browser
    Close,
    Contents,
    Search,
    Help,  // a circled ?
    Copy,
    // Engines on a machine, and its files.
    Server,
    Program,
    File,
    FolderClosed,
    FolderUp
};

// `icon` in a line of text, as wide as the font is high, in `col`.
void inlineIcon(Icon icon, ImU32 col);

// A small square button showing `icon`, its tooltip `tip`; true when clicked.
bool iconOnlyButton(const char* id, Icon icon, const char* tip, bool enabled = true);

// Draws `icon` centred on `center`, `size` pixels wide, in `col`.
void drawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 col);

// The fonts loaded at startup: titles and icons; the text font is ImGui's default.
void setFonts(ImFont* text, ImFont* titles, ImFont* icons);
// The titles' font (semibold), nullptr before setFonts(). Like every font, it is made at the size it is drawn at.
[[nodiscard]] ImFont* titleFont();
// The guide's italics (Inter Italic) and code (ImGui's scalable monospaced font); the text font until set.
void setGuideFonts(ImFont* italic, ImFont* mono);
[[nodiscard]] ImFont* italicFont();
[[nodiscard]] ImFont* monoFont();

// Chinese needs a system font: requestCjkFont() when Chinese text is to be shown, and it is merged in by
// loadRequestedFonts(), called between frames.
void requestCjkFont();
void loadRequestedFonts(float size);

// A button with a small drawn icon, so no icon font is needed.
bool iconButton(const char* id, Icon icon, const char* label, bool enabled = true);

// The content region of a page section. The views used to be dockable windows; they now draw inside a child of
// the page. Always pair with endPanel(), even when it returns false.
bool beginPanel(const char* title, ImVec2 size = ImVec2(0, 0));
void endPanel();

// A tall navigation button for the rail: icon above a short label. `height` 0: the usual one.
bool railButton(const char* id, Icon icon, const char* label, bool selected, float width, float height = 0);

// The usual height of a rail button.
[[nodiscard]] float railButtonHeight();

// Large page title.
void pageTitle(const char* text);
// The same, followed by a round `?` that opens the guide at `topic` (an id of guide::topics, as F1 does).
void pageTitle(const char* text, const char* topic);

// Help at the cursor (docs/design/help.md, "Topics"). The guide panel opens through the function given here (the
// browser at the guide on GitHub until one is given).
void setGuideOpener(std::function<void(const char* topic)> open);
void openGuide(const char* topic);
// At the start of each frame: the topic under the mouse in the frame before becomes hoveredTopic().
void beginTopicFrame();
// The topic under the mouse in the last frame: the innermost item or region marked, nullptr when none.
[[nodiscard]] const char* hoveredTopic();
// Marks the last item with `topic`: under the mouse, it is the topic F1 opens. Call it after the item's tooltip:
// when one is shown, it ends with "F1: more in the guide".
void topic(const char* topic);
// Marks a region, from here to the end of the scope: the mouse in it and in no item or region marked inside it,
// `topic` is the one F1 opens.
class TopicRegion {
   public:
    explicit TopicRegion(const char* topic);
    ~TopicRegion();
    TopicRegion(const TopicRegion&)            = delete;
    TopicRegion& operator=(const TopicRegion&) = delete;

   private:
    const char* topic_;
    int serial_;
};
// A small round `?` on the line, opening the guide at `topic`, its tooltip "In the guide: <title>".
void guideLink(const char* id, const char* topic);

// A rounded card filling the available width, with a border; pair with endCard().
void beginCard(const char* id, ImVec2 size = ImVec2(0, 0));
void endCard();

enum class CopyAs { None, Text, Bbcode, GauntletText, GauntletBbcode };

// A "Copy" button with a menu: as plain text, or as BBCode for a forum post, and with `gauntlet` the gauntlet's
// table in both forms too; with `header`, a check for a header on top, which the menu stays open for. Returns the
// choice, None until one is made.
CopyAs copyButton(const char* id, bool small = false, bool gauntlet = false, bool* header = nullptr);

// Opens a file or a folder with the program the system associates with it. When the system refuses, the path goes
// to the clipboard and drawOpenPathError(), called once a frame, says so.
void openPath(const std::filesystem::path& path);
void drawOpenPathError();

// file:///... with every byte outside [A-Za-z0-9/-._~:] percent-encoded.
[[nodiscard]] std::string fileUrl(const std::filesystem::path& path);

// "(?)" with a tooltip.
void help(const char* text);

// The value of arithmetic typed in a number field: "40*60", "15+10", "(3+2)*60", with + - * / and parentheses, the
// decimal mark a point or a comma. nullopt when the text is not one (or divides by zero).
[[nodiscard]] std::optional<double> evaluate(std::string_view text);

// Number fields whose text may be arithmetic (see evaluate), applied when the field is left or Enter pressed, like
// ImGui::InputInt(label, v, 0, 0) and its kind. True when the value changed.
bool inputInt(const char* label, int* value);
bool inputInt64(const char* label, int64_t* value);
bool inputDouble(const char* label, double* value, const char* format = "%.3f");

// A small round label with a count, such as the games being played, drawn on top of what is at `center`.
void drawBadge(ImDrawList* dl, ImVec2 center, const char* text);
[[nodiscard]] float badgeWidth(const char* text);

// With Ctrl held, the mouse wheel of this frame: +1 up, -1 down, 0 without Ctrl or a turn. It is taken from the frame
// (call before drawing), so that the chart under the mouse (ImPlot reads io.MouseWheel) does not zoom as well.
int takeCtrlWheel();

// Label in the left column, widget in the right one (for the setup panel).
void label(const char* text, float width);

// Width of the list of a list + detail page: `fraction` of `total` (0: a quarter), within [min, max].
[[nodiscard]] float paneWidth(float fraction, float total, float min, float max);

// A thin bar after the list, which the mouse drags to change `fraction`, the list's share of `total`. True when it
// changed.
bool splitter(const char* id, float& fraction, float total);

// Before a button labelled `next`: on the same line if it fits, else on the next one.
void sameLineOrWrap(const char* next);
// The same for items `width` wide, `spacing` after the previous one (-1: the usual spacing).
void sameLineOrWrap(float width, float spacing = -1.0f);

// A grey message in the middle of the space left, for an empty detail pane.
void emptyState(const char* text);

// The inside of a combo of many names, such as the engines of a large library, once BeginCombo returned true: a
// search field on top, focused, then the names containing what is typed (Enter takes the first), only the rows in
// sight drawn. `current` is marked and scrolled to. `more`, when given, holds by name other text the search looks in
// too (an engine's author, program and notes, see engineSearchText). Returns the index of the name chosen, or -1. Call
// EndCombo after.
int searchComboItems(const std::vector<std::string>& names, const std::string& current,
                     const std::vector<std::string>* more = nullptr);

}  // namespace fcg::ui
