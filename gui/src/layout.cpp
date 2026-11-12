#include "layout.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <map>

#include <imgui.h>
#include <imgui_internal.h>

#include "i18n.hpp"

namespace fcg::layout {

namespace {

constexpr const char* DETACHED = "detached";

std::string windowName(std::string_view panel) { return "###" + std::string(panel); }

bool hasCentral(const Node& node) {
    if (node.split == Node::Split::None) return node.central;
    return std::any_of(node.children.begin(), node.children.end(), [](const Node& c) { return hasCentral(c); });
}

void collectFronts(const Node& node, std::set<std::string>& out) {
    if (node.split == Node::Split::None) {
        if (!node.tabs.empty()) out.insert(node.selected.empty() ? node.tabs.front() : node.selected);
        return;
    }
    for (const auto& c : node.children) collectFronts(c, out);
}

void collect(const Node& node, std::set<std::string>& out) {
    out.insert(node.tabs.begin(), node.tabs.end());
    for (const auto& c : node.children) collect(c, out);
}

// The leaf with the most tabs: where the panels not in a layout are put away, to come back if shown again.
const Node* fullestLeaf(const Node& node) {
    if (node.split == Node::Split::None) return &node;
    const Node* best = nullptr;
    for (const auto& c : node.children) {
        const Node* leaf = fullestLeaf(c);
        if (leaf && (!best || leaf->tabs.size() > best->tabs.size())) best = leaf;
    }
    return best;
}

void dockWindow(std::string_view panel, ImGuiID node, int order) {
    const auto name = windowName(panel);
    ImGui::DockBuilderDockWindow(name.c_str(), node);
    // DockBuilderDockWindow forgets the order of the tabs: they appear in that of the windows, sorted by DockOrder
    // when they appear together.
    const ImGuiID id = windowId(panel);
    if (ImGuiWindow* window = ImGui::FindWindowByID(id))
        window->DockOrder = static_cast<short>(order);
    else if (ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(id))
        settings->DockOrder = static_cast<short>(order);
}

// Builds `node` in the dock node `id`; `away` gets the dock node of the leaf the other panels go to.
void buildNode(ImGuiID id, const Node& node, const Node* fullest, ImGuiID& away) {
    if (node.split != Node::Split::None && node.children.size() == 2) {
        const bool horizontal = node.split == Node::Split::Horizontal;
        const float ratio     = std::clamp(node.ratio, 0.05f, 0.95f);
        ImGuiID first = 0, second = 0;
        // The part split off is the one at the direction; the other keeps the central node, wherever the layout
        // wants it.
        if (hasCentral(node.children[0])) {
            ImGui::DockBuilderSplitNode(id, horizontal ? ImGuiDir_Right : ImGuiDir_Down, 1.0f - ratio, &second, &first);
        } else {
            ImGui::DockBuilderSplitNode(id, horizontal ? ImGuiDir_Left : ImGuiDir_Up, ratio, &first, &second);
        }
        buildNode(first, node.children[0], fullest, away);
        buildNode(second, node.children[1], fullest, away);
        return;
    }
    for (std::size_t i = 0; i < node.tabs.size(); ++i) dockWindow(node.tabs[i], id, static_cast<int>(i));
    if (&node == fullest) away = id;
    if (ImGuiDockNode* dock = ImGui::DockBuilderGetNode(id)) {
        const auto& selected = node.selected.empty() && !node.tabs.empty() ? node.tabs.front() : node.selected;
        dock->SelectedTabId  = selected.empty() ? 0 : tabId(selected);
    }
}

// The panel of a window: its name after "###"; empty for the windows that are not panels.
std::string panelOf(const char* window_name) {
    const char* id = window_name ? std::strstr(window_name, "###") : nullptr;
    return id ? std::string(id + 3) : std::string();
}

// The panel of window settings: ImGui keeps what follows "###" as their name.
std::string settingsPanel(ImGuiWindowSettings* s) {
    const char* name = s->ID ? s->GetName() : nullptr;
    if (!name || std::strchr(name, '#')) return panelOf(name);
    const std::string panel(name);
    const bool known = detachedSlot(panel) || std::any_of(allPanels().begin(), allPanels().end(),
                                                          [&](const PanelInfo& p) { return panel == p.id; });
    return known ? panel : std::string();
}

// The leaf's panels in the order of its tabs, then those docked in it while hidden (closed, or of another format).
Node captureLeaf(ImGuiDockNode* dock, const std::function<bool(std::string_view)>& known) {
    Node leaf;
    leaf.central   = dock->IsCentralNode();
    const auto add = [&](const std::string& panel) {
        if (!panel.empty() && known(panel) && std::find(leaf.tabs.begin(), leaf.tabs.end(), panel) == leaf.tabs.end())
            leaf.tabs.push_back(panel);
    };
    if (dock->TabBar) {
        for (const auto& tab : dock->TabBar->Tabs)
            if (tab.Window) add(panelOf(tab.Window->Name));
    }
    for (ImGuiWindow* window : dock->Windows) add(panelOf(window->Name));
    // Hidden ones, in their last order.
    std::vector<std::pair<int, std::string>> hidden;
    auto& g = *GImGui;
    for (ImGuiWindow* window : g.Windows) {
        if (window->DockId == dock->ID && !window->DockIsActive)
            hidden.emplace_back(window->DockOrder, panelOf(window->Name));
    }
    for (auto* s = g.SettingsWindows.begin(); s != nullptr; s = g.SettingsWindows.next_chunk(s)) {
        if (s->ID && s->DockId == dock->ID && !ImGui::FindWindowByID(s->ID))
            hidden.emplace_back(s->DockOrder, settingsPanel(s));
    }
    std::stable_sort(hidden.begin(), hidden.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& h : hidden) add(h.second);

    for (const auto& panel : leaf.tabs) {
        if (tabId(panel) == dock->SelectedTabId) leaf.selected = panel;
    }
    return leaf;
}

Node captureNode(ImGuiDockNode* dock, const std::function<bool(std::string_view)>& known) {
    if (!dock) return {};
    if (!dock->IsSplitNode()) return captureLeaf(dock, known);
    Node first  = captureNode(dock->ChildNodes[0], known);
    Node second = captureNode(dock->ChildNodes[1], known);
    if (first.empty()) return second;
    if (second.empty()) return first;
    const int axis = dock->SplitAxis == ImGuiAxis_X ? 0 : 1;
    const float a  = dock->ChildNodes[0]->SizeRef[axis];
    const float b  = dock->ChildNodes[1]->SizeRef[axis];
    const float r  = a + b > 0 ? a / (a + b) : 0.5f;
    return Node::split2(axis == 0 ? Node::Split::Horizontal : Node::Split::Vertical, r, std::move(first),
                        std::move(second));
}

ImGuiID dockIdOf(ImGuiID window_id) {
    if (const ImGuiWindow* window = ImGui::FindWindowByID(window_id)) return window->DockId;
    if (const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(window_id)) return settings->DockId;
    return 0;
}

}  // namespace

const std::vector<PanelInfo>& allPanels() {
    static const std::vector<PanelInfo> list = {
        {panel::OVERVIEW, N_("Overview###tab_overview")},
        {panel::STANDINGS, N_("Standings###tab_standings")},
        {panel::BRACKET, N_("Bracket###tab_bracket")},
        {panel::SCHEDULE, N_("Schedule###tab_schedule")},
        {panel::BROADCAST, N_("Broadcast###tab_broadcast")},
        {panel::LIVE, N_("Live###tab_live")},
        {panel::GAMES, N_("Games###tab_games")},
        {panel::OPENINGS, N_("Openings###tab_openings")},
    };
    return list;
}

bool available(std::string_view panel, bool match, bool knockout) {
    if (panel == panel::STANDINGS || panel == panel::SCHEDULE) return !match;
    if (panel == panel::BRACKET) return knockout;
    return true;
}

std::string panelName(std::string_view panel) {
    if (const auto slot = detachedSlot(panel)) return trf("Game #{}", *slot + 1);
    for (const auto& p : allPanels()) {
        if (panel == p.id) {
            const std::string label = TR(p.label);
            return label.substr(0, label.find("##"));
        }
    }
    return std::string(panel);
}

std::string detachedPanel(int slot) { return DETACHED + std::to_string(slot); }

std::optional<int> detachedSlot(std::string_view panel) {
    const std::string_view prefix(DETACHED);
    if (panel.substr(0, prefix.size()) != prefix || panel.size() == prefix.size()) return std::nullopt;
    int slot        = -1;
    const auto rest = panel.substr(prefix.size());
    const auto res  = std::from_chars(rest.data(), rest.data() + rest.size(), slot);
    if (res.ec != std::errc() || res.ptr != rest.data() + rest.size() || slot < 0) return std::nullopt;
    return slot;
}

Node Node::leaf(std::vector<std::string> tabs, std::string selected) {
    Node n;
    n.tabs     = std::move(tabs);
    n.selected = std::move(selected);
    return n;
}

Node Node::split2(Split split, float ratio, Node first, Node second) {
    Node n;
    n.split = split;
    n.ratio = ratio;
    n.children.push_back(std::move(first));
    n.children.push_back(std::move(second));
    return n;
}

bool Node::empty() const {
    return tabs.empty() && std::all_of(children.begin(), children.end(), [](const Node& c) { return c.empty(); });
}

std::set<std::string> Layout::panels() const {
    std::set<std::string> out;
    collect(workspace, out);
    for (const auto& f : floating) collect(f.node, out);
    return out;
}

std::set<std::string> Layout::fronts() const {
    std::set<std::string> out;
    collectFronts(workspace, out);
    for (const auto& f : floating) collectFronts(f.node, out);
    return out;
}

// ---------------------------------------------------------------------------
// Ready-made layouts

Layout preset(Preset which) {
    using namespace panel;
    using S = Node::Split;
    Layout l;
    l.name = presetLabel(which);
    switch (which) {
        case Preset::Tabs:
        case Preset::Count:
            l.workspace =
                Node::leaf({OVERVIEW, STANDINGS, BRACKET, SCHEDULE, BROADCAST, LIVE, GAMES, OPENINGS}, OVERVIEW);
            l.workspace.central = true;
            break;
        case Preset::FollowGame: {
            // The board on the left, the players, searches and chart on the right; the standings under them, with the
            // other tabs.
            auto board    = Node::leaf({BOARD});
            board.central = true;
            auto right    = Node::split2(
                S::Vertical, 0.2f, Node::leaf({PLAYERS}),
                Node::split2(S::Vertical, 0.45f, Node::leaf({SEARCHES}), Node::leaf({CHART, MOVES}, CHART)));
            l.workspace = Node::split2(
                S::Vertical, 0.68f, Node::split2(S::Horizontal, 0.5f, std::move(board), std::move(right)),
                Node::leaf({STANDINGS, OVERVIEW, LIVE, SCHEDULE, BRACKET, GAMES, OPENINGS, BROADCAST}, STANDINGS));
            break;
        }
        case Preset::LargeTournament: {
            // Every game in the mosaic, the standings beside it, and the game followed under them.
            auto mosaic    = Node::leaf({LIVE});
            mosaic.central = true;
            l.workspace    = Node::split2(
                S::Horizontal, 0.62f, std::move(mosaic),
                Node::split2(
                    S::Vertical, 0.58f,
                    Node::leaf({STANDINGS, OVERVIEW, SCHEDULE, BRACKET, GAMES, OPENINGS, BROADCAST}, STANDINGS),
                    Node::leaf({BOARD, PLAYERS, SEARCHES, MOVES, CHART}, BOARD)));
            break;
        }
        case Preset::Broadcast: {
            // A large board, the players and their searches beside it, the standings under them.
            auto board    = Node::leaf({BOARD});
            board.central = true;
            l.workspace =
                Node::split2(S::Horizontal, 0.6f, std::move(board),
                             Node::split2(S::Vertical, 0.18f, Node::leaf({PLAYERS}),
                                          Node::split2(S::Vertical, 0.42f, Node::leaf({SEARCHES}),
                                                       Node::leaf({STANDINGS, MOVES, CHART, OVERVIEW, LIVE, SCHEDULE,
                                                                   BRACKET, GAMES, OPENINGS, BROADCAST},
                                                                  STANDINGS))));
            break;
        }
    }
    return l;
}

const char* presetKey(Preset which) {
    switch (which) {
        case Preset::FollowGame:
            return "follow";
        case Preset::LargeTournament:
            return "large";
        case Preset::Broadcast:
            return "broadcast";
        default:
            return "tabs";
    }
}

const char* presetLabel(Preset which) {
    switch (which) {
        case Preset::FollowGame:
            return N_("Follow a game");
        case Preset::LargeTournament:
            return N_("Large tournament");
        case Preset::Broadcast:
            return N_("Broadcast");
        default:
            return N_("Tabs");
    }
}

std::optional<Preset> presetByKey(std::string_view key) {
    for (int i = 0; i < static_cast<int>(Preset::Count); ++i) {
        if (key == presetKey(static_cast<Preset>(i))) return static_cast<Preset>(i);
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// JSON

void to_json(nlohmann::json& j, const Node& node) {
    j = nlohmann::json::object();
    if (node.split != Node::Split::None && node.children.size() == 2) {
        j["split"]  = node.split == Node::Split::Horizontal ? "horizontal" : "vertical";
        j["ratio"]  = node.ratio;
        j["first"]  = node.children[0];
        j["second"] = node.children[1];
        return;
    }
    j["tabs"] = node.tabs;
    if (!node.selected.empty()) j["selected"] = node.selected;
    if (node.central) j["central"] = true;
}

void from_json(const nlohmann::json& j, Node& node) {
    node             = {};
    const auto split = j.value("split", std::string());
    if ((split == "horizontal" || split == "vertical") && j.contains("first") && j.contains("second")) {
        node.split = split == "horizontal" ? Node::Split::Horizontal : Node::Split::Vertical;
        node.ratio = std::clamp(j.value("ratio", 0.5f), 0.05f, 0.95f);
        node.children.push_back(j.at("first").get<Node>());
        node.children.push_back(j.at("second").get<Node>());
        return;
    }
    node.tabs     = j.value("tabs", std::vector<std::string>());
    node.selected = j.value("selected", std::string());
    node.central  = j.value("central", false);
}

void to_json(nlohmann::json& j, const Layout& layout) {
    j = {{"name", layout.name}, {"workspace", layout.workspace}};
    if (!layout.floating.empty()) {
        auto& floating = j["floating"] = nlohmann::json::array();
        for (const auto& f : layout.floating) {
            floating.push_back({{"x", f.x}, {"y", f.y}, {"width", f.width}, {"height", f.height}, {"node", f.node}});
        }
    }
}

void from_json(const nlohmann::json& j, Layout& layout) {
    layout      = {};
    layout.name = j.value("name", std::string());
    if (j.contains("workspace")) layout.workspace = j.at("workspace").get<Node>();
    if (j.contains("floating") && j.at("floating").is_array()) {
        for (const auto& f : j.at("floating")) {
            Floating floating;
            floating.x      = f.value("x", 0.0f);
            floating.y      = f.value("y", 0.0f);
            floating.width  = std::max(80.0f, f.value("width", 400.0f));
            floating.height = std::max(60.0f, f.value("height", 300.0f));
            if (f.contains("node")) floating.node = f.at("node").get<Node>();
            if (!floating.node.empty()) layout.floating.push_back(std::move(floating));
        }
    }
}

// ---------------------------------------------------------------------------
// ImGui

ImGuiID windowId(std::string_view panel) { return ImHashStr(windowName(panel).c_str()); }

ImGuiID tabId(std::string_view panel) { return ImHashStr("#TAB", 0, windowId(panel)); }

void place(const Placement& placement) {
    ImGui::SetNextWindowPos(ImVec2(placement.x, placement.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(placement.width, placement.height), ImGuiCond_Always);
}

std::vector<Placement> build(ImGuiID dockspace, float width, float height, const Layout& layout, float origin_x,
                             float origin_y) {
    const Node* fullest = fullestLeaf(layout.workspace);
    ImGuiID away        = 0;

    // The panels of the dock space's old tree that the layout does not show, to put away in the new one.
    std::vector<std::string> others;
    const auto shown = layout.panels();
    auto& g          = *GImGui;
    for (ImGuiWindow* window : g.Windows) {
        const auto panel = panelOf(window->Name);
        if (!panel.empty() && !shown.count(panel) && dockedIn(dockspace, panel)) others.push_back(panel);
    }
    for (auto* s = g.SettingsWindows.begin(); s != nullptr; s = g.SettingsWindows.next_chunk(s)) {
        const auto panel = settingsPanel(s);
        if (!panel.empty() && !shown.count(panel) && !ImGui::FindWindowByID(s->ID) && dockedIn(dockspace, panel))
            others.push_back(panel);
    }

    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImVec2(std::max(width, 64.0f), std::max(height, 64.0f)));
    buildNode(dockspace, layout.workspace, fullest, away);
    const int base = static_cast<int>(fullest ? fullest->tabs.size() : 0);
    for (std::size_t i = 0; i < others.size(); ++i)
        dockWindow(others[i], away ? away : dockspace, base + static_cast<int>(i));
    ImGui::DockBuilderFinish(dockspace);

    std::vector<Placement> alone;
    for (const auto& f : layout.floating) {
        if (f.node.split == Node::Split::None && f.node.tabs.size() == 1) {
            alone.push_back({f.node.tabs.front(), origin_x + f.x, origin_y + f.y, f.width, f.height});
            continue;
        }
        const ImGuiID id = ImGui::DockBuilderAddNode(0, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodePos(id, ImVec2(origin_x + f.x, origin_y + f.y));
        ImGui::DockBuilderSetNodeSize(id, ImVec2(f.width, f.height));
        ImGuiID unused = 0;
        buildNode(id, f.node, nullptr, unused);
        ImGui::DockBuilderFinish(id);
    }
    return alone;
}

Layout capture(ImGuiID dockspace, const std::function<bool(std::string_view)>& known, float origin_x, float origin_y) {
    Layout layout;
    layout.workspace = captureNode(ImGui::DockBuilderGetNode(dockspace), known);
    if (layout.workspace.empty()) layout.workspace = {};

    // Floating dock nodes, and the panels floating alone.
    auto& g                    = *GImGui;
    std::set<std::string> seen = layout.panels();
    for (int n = 0; n < g.DockContext.Nodes.Data.Size; ++n) {
        auto* dock = static_cast<ImGuiDockNode*>(g.DockContext.Nodes.Data[n].val_p);
        if (!dock || dock->ParentNode || dock->IsDockSpace() || dock->ID == dockspace) continue;
        Floating f;
        f.node = captureNode(dock, [&](std::string_view p) { return known(p) && !seen.count(std::string(p)); });
        if (f.node.empty() || !dock->IsVisible) continue;
        f.x      = dock->Pos.x - origin_x;
        f.y      = dock->Pos.y - origin_y;
        f.width  = dock->Size.x;
        f.height = dock->Size.y;
        collect(f.node, seen);
        layout.floating.push_back(std::move(f));
    }
    for (ImGuiWindow* window : g.Windows) {
        const auto panel = panelOf(window->Name);
        if (panel.empty() || seen.count(panel) || !known(panel) || window->DockId != 0 || !window->WasActive) continue;
        Floating f;
        f.node   = Node::leaf({panel});
        f.x      = window->Pos.x - origin_x;
        f.y      = window->Pos.y - origin_y;
        f.width  = window->SizeFull.x;
        f.height = window->SizeFull.y;
        seen.insert(panel);
        layout.floating.push_back(std::move(f));
    }
    return layout;
}

bool bringToFront() {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window->DockIsActive) return window->DockId == 0;
    ImGuiTabBar* tab_bar = window->DockNode ? window->DockNode->TabBar : nullptr;
    if (!tab_bar) return false;
    if (tab_bar->SelectedTabId == window->TabId) return true;
    tab_bar->NextSelectedTabId = window->TabId;
    return false;
}

bool dockedIn(ImGuiID dockspace, std::string_view panel) {
    const ImGuiID dock_id = dockIdOf(windowId(panel));
    if (dock_id == 0) return false;
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dock_id);
    return node && ImGui::DockNodeGetRootNode(node)->ID == dockspace;
}

bool lost(std::string_view panel) {
    const ImGuiID id = windowId(panel);
    if (!ImGui::FindWindowByID(id) && !ImGui::FindWindowSettingsByID(id)) return true;
    const ImGuiID dock_id = dockIdOf(id);
    return dock_id != 0 && !ImGui::DockBuilderGetNode(dock_id);
}

bool usable(ImGuiID dockspace) {
    ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspace);
    if (!root) return false;
    auto& g           = *GImGui;
    const auto inside = [&](ImGuiID dock_id) {
        ImGuiDockNode* node = dock_id ? ImGui::DockBuilderGetNode(dock_id) : nullptr;
        return node && ImGui::DockNodeGetRootNode(node) == root;
    };
    for (ImGuiWindow* window : g.Windows) {
        if (!panelOf(window->Name).empty() && inside(window->DockId)) return true;
    }
    for (auto* s = g.SettingsWindows.begin(); s != nullptr; s = g.SettingsWindows.next_chunk(s)) {
        if (!settingsPanel(s).empty() && inside(s->DockId)) return true;
    }
    return false;
}

ImGuiID fallbackNode(ImGuiID dockspace) {
    ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspace);
    if (!root) return 0;
    ImGuiDockNode* best                             = nullptr;
    const std::function<void(ImGuiDockNode*)> visit = [&](ImGuiDockNode* node) {
        if (!node) return;
        if (node->IsSplitNode()) {
            visit(node->ChildNodes[0]);
            visit(node->ChildNodes[1]);
            return;
        }
        const auto area = [](const ImGuiDockNode* n) { return n->Size.x * n->Size.y; };
        if (!best || node->Windows.Size > best->Windows.Size ||
            (node->Windows.Size == best->Windows.Size && area(node) > area(best)))
            best = node;
    };
    visit(root);
    return best ? best->ID : dockspace;
}

}  // namespace fcg::layout
