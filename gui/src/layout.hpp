#pragma once

// The panels of the tournament workspace docked together: the ready-made layouts and the user's own, kept as trees of
// panels (not as ImGui's settings text), built with ImGui's DockBuilder and read back from its dock nodes.
//
// A panel is named by what follows "###" in its window's name ("tab_standings"): the same in every language, and
// the id ImGui hashes for the window.

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

typedef unsigned int ImGuiID;

namespace fcg::layout {

// The panels of the workspace, in the order of the tabs of the default layout, then the parts of the game followed.
namespace panel {
inline constexpr const char* OVERVIEW  = "tab_overview";
inline constexpr const char* STANDINGS = "tab_standings";
inline constexpr const char* BRACKET   = "tab_bracket";
inline constexpr const char* SCHEDULE  = "tab_schedule";
inline constexpr const char* BROADCAST = "tab_broadcast";
inline constexpr const char* LIVE      = "tab_live";
inline constexpr const char* GAMES     = "tab_games";
inline constexpr const char* OPENINGS  = "tab_openings";
inline constexpr const char* BOARD     = "live_board";
inline constexpr const char* PLAYERS   = "live_players";
inline constexpr const char* SEARCHES  = "live_searches";
inline constexpr const char* MOVES     = "live_moves";
inline constexpr const char* CHART     = "live_chart";
}  // namespace panel

// A panel: its id and its name, in English with its id ("Standings###tab_standings"), to translate.
struct PanelInfo {
    const char* id;
    const char* label;
};

// Every panel but the detached boards, in the order of the tabs of the default layout, then the parts of the game
// followed.
[[nodiscard]] const std::vector<PanelInfo>& allPanels();

// Whether a tournament has the panel: no standings nor schedule for a match, a bracket for a knockout only.
[[nodiscard]] bool available(std::string_view panel, bool match, bool knockout);

// The name of a panel, translated: "Standings", "Game #3".
[[nodiscard]] std::string panelName(std::string_view panel);

// The panel of a detached board ("detached3"), and the slot of such a panel.
[[nodiscard]] std::string detachedPanel(int slot);
[[nodiscard]] std::optional<int> detachedSlot(std::string_view panel);

// A node of a dock tree: two nodes side by side or one above the other, or panels as tabs.
struct Node {
    enum class Split { None, Horizontal, Vertical };  // Horizontal: side by side
    Split split = Split::None;
    float ratio = 0.5f;             // the first child's share of the width (or the height)
    std::vector<Node> children;     // two when split
    std::vector<std::string> tabs;  // the panels, in the order of their tabs
    std::string selected;           // the tab in front, empty for the first
    bool central = false;           // the leaf that stays on screen when its panels are all closed

    [[nodiscard]] static Node leaf(std::vector<std::string> tabs, std::string selected = {});
    [[nodiscard]] static Node split2(Split split, float ratio, Node first, Node second);
    [[nodiscard]] bool empty() const;
    bool operator==(const Node&) const = default;
};

// Panels out of the workspace, docked together or alone: where, relative to the main window.
struct Floating {
    Node node;
    float x = 0, y = 0, width = 0, height = 0;
    bool operator==(const Floating&) const = default;
};

struct Layout {
    std::string name;
    Node workspace;
    std::vector<Floating> floating;

    // Every panel it shows, docked or floating.
    [[nodiscard]] std::set<std::string> panels() const;
    // The panel in front in each of its nodes.
    [[nodiscard]] std::set<std::string> fronts() const;
    bool operator==(const Layout&) const = default;
};

// The ready-made layouts.
enum class Preset { Tabs, FollowGame, LargeTournament, Broadcast, Count };
[[nodiscard]] Layout preset(Preset which);
[[nodiscard]] const char* presetKey(Preset which);    // "tabs", "follow", "large", "broadcast": for --focus layout=
[[nodiscard]] const char* presetLabel(Preset which);  // English, to translate: "Follow a game"
[[nodiscard]] std::optional<Preset> presetByKey(std::string_view key);

void to_json(nlohmann::json& j, const Node& node);
void from_json(const nlohmann::json& j, Node& node);
void to_json(nlohmann::json& j, const Layout& layout);
void from_json(const nlohmann::json& j, Layout& layout);

// The id ImGui gives the window of a panel, and the tab of that window in a dock node.
[[nodiscard]] ImGuiID windowId(std::string_view panel);
[[nodiscard]] ImGuiID tabId(std::string_view panel);

// A panel floating alone, placed by its window rather than by a dock node: before its next Begin().
struct Placement {
    std::string panel;
    float x = 0, y = 0, width = 0, height = 0;  // on the screen
};
void place(const Placement& placement);  // SetNextWindowPos() and SetNextWindowSize(), which undock it

// Needs an ImGui context, in a frame, before the dock space is submitted. Replaces the dock space `dockspace`
// (`width` x `height`) with the layout's tree and adds its floating nodes at `origin` (the main window's position).
// The windows of its panels are docked in them (also those not created yet); the panels of the old tree it does not
// show are put away in its node with the most tabs, to come back there if shown again; the panels floating out of
// the tree are left where they are. Returns the panels floating alone, to place().
std::vector<Placement> build(ImGuiID dockspace, float width, float height, const Layout& layout, float origin_x = 0,
                             float origin_y = 0);

// The layout on screen: the dock space's tree and the floating panels, those `known` accepts (open panels of the
// workspace), relative to `origin`. The panels of a node not on screen (a format without them) count when `known`
// accepts them; nodes left without panels drop out.
[[nodiscard]] Layout capture(ImGuiID dockspace, const std::function<bool(std::string_view)>& known, float origin_x = 0,
                             float origin_y = 0);

// Called after Begin(): asks for the window's tab to be the one in front in its dock node (ImGui selects the tab
// that appeared last when several appear together). True once it is, or when the window is not docked.
bool bringToFront();

// Whether the window of `panel` is docked in the dock space's tree (or will be, from its settings).
[[nodiscard]] bool dockedIn(ImGuiID dockspace, std::string_view panel);

// Whether the window of `panel` needs a place: docked in a node that does not exist (a layout.ini of another version,
// or a node that went away while it was closed), or never placed.
[[nodiscard]] bool lost(std::string_view panel);

// Whether the dock space exists with panels in it: else (no layout.ini yet, one of an earlier version, or damaged) the
// default layout is built.
[[nodiscard]] bool usable(ImGuiID dockspace);

// Where a panel without a place goes: the leaf of the dock space with the most panels (the larger one first), 0 when
// the dock space does not exist.
[[nodiscard]] ImGuiID fallbackNode(ImGuiID dockspace);

}  // namespace fcg::layout
