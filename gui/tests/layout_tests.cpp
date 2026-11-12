// The layouts of the tournament workspace: built with ImGui's DockBuilder, read back from its dock nodes, kept in
// layout.ini and in the profile.

#include <doctest/doctest.hpp>

#include <cmath>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "layout.hpp"

using namespace fcg;
using layout::Layout;
using layout::Node;

namespace {

constexpr ImGuiID DOCK = 0x5eed;

// An ImGui context of its own, without a window: docking on, no layout.ini.
struct Context {
    ImGuiContext* context;
    std::set<std::string> open;            // the panels submitted at each frame
    std::vector<layout::Placement> alone;  // panels floating alone, placed at their next frame

    Context() : context(ImGui::CreateContext()) {
        auto& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1200, 800);
        io.DeltaTime   = 1.0f / 60.0f;
    }
    ~Context() { ImGui::DestroyContext(context); }

    // A frame as the workspace draws it: `before` in the window of the dock space, before it, then every open
    // panel, brought to the front of its node when `fronts` has it.
    void frame(const std::function<void()>& before = {}, std::set<std::string>* fronts = nullptr) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking);
        if (before) before();
        ImGui::DockSpace(DOCK, ImVec2(0, 0));
        ImGui::End();
        for (const auto& panel : open) {
            for (auto it = alone.begin(); it != alone.end(); ++it) {
                if (it->panel != panel) continue;
                layout::place(*it);
                alone.erase(it);
                break;
            }
            ImGui::Begin((panel + "###" + panel).c_str(), nullptr, ImGuiWindowFlags_NoFocusOnAppearing);
            if (fronts && fronts->count(panel) && layout::bringToFront()) fronts->erase(panel);
            ImGui::End();
        }
        ImGui::Render();
    }

    // Builds `l` and shows its panels for a few frames.
    void apply(const Layout& l) {
        open        = l.panels();
        auto fronts = l.fronts();
        frame([&] { alone = layout::build(DOCK, 1200, 800, l); }, &fronts);
        for (int i = 0; i < 4; ++i) frame({}, &fronts);
    }

    [[nodiscard]] Layout capture() const {
        return layout::capture(DOCK, [&](std::string_view p) { return open.count(std::string(p)) > 0; });
    }
};

// The same tree, the ratios within a percent.
void checkSame(const Node& a, const Node& b) {
    CHECK(a.split == b.split);
    CHECK(a.tabs == b.tabs);
    CHECK((a.selected.empty() && !a.tabs.empty() ? a.tabs.front() : a.selected) ==
          (b.selected.empty() && !b.tabs.empty() ? b.tabs.front() : b.selected));
    CHECK(a.central == b.central);
    REQUIRE(a.children.size() == b.children.size());
    if (a.split != Node::Split::None) CHECK(std::abs(a.ratio - b.ratio) < 0.01f);
    for (std::size_t i = 0; i < a.children.size(); ++i) checkSame(a.children[i], b.children[i]);
}

int leaves(const Node& n) {
    if (n.split == Node::Split::None) return 1;
    return leaves(n.children[0]) + leaves(n.children[1]);
}

}  // namespace

TEST_CASE("layouts: every ready-made layout builds the dock tree it describes") {
    for (int i = 0; i < static_cast<int>(layout::Preset::Count); ++i) {
        const auto which = static_cast<layout::Preset>(i);
        CAPTURE(layout::presetKey(which));
        const auto expected = layout::preset(which);
        Context c;
        c.apply(expected);
        const auto got = c.capture();
        checkSame(got.workspace, expected.workspace);
        CHECK(got.floating.empty());
        CHECK(got.panels() == expected.panels());

        // The tabs in front are those of the layout, not the last of each node.
        for (const auto& panel : expected.fronts()) {
            auto* window = ImGui::FindWindowByID(layout::windowId(panel));
            REQUIRE(window);
            REQUIRE(window->DockNode);
            REQUIRE(window->DockNode->TabBar);
            CHECK(window->DockNode->TabBar->SelectedTabId == window->TabId);
        }
    }

    // Tabs is the default: the tabs of the workspace before docking, in their order, the overview in front.
    const auto tabs = layout::preset(layout::Preset::Tabs);
    CHECK(tabs.workspace.split == Node::Split::None);
    CHECK(tabs.workspace.tabs == std::vector<std::string>{"tab_overview", "tab_standings", "tab_bracket",
                                                          "tab_schedule", "tab_broadcast", "tab_live", "tab_games",
                                                          "tab_openings"});
    CHECK(tabs.fronts() == std::set<std::string>{"tab_overview"});
    // The others show the game followed beside the standings, the mosaic, or a large board.
    for (auto which : {layout::Preset::FollowGame, layout::Preset::LargeTournament, layout::Preset::Broadcast}) {
        const auto l = layout::preset(which);
        CHECK(leaves(l.workspace) >= 2);
        CHECK(l.panels().count("tab_standings"));
        CHECK(l.panels().count(which == layout::Preset::LargeTournament ? "tab_live" : "live_board"));
        // Every tab of the default layout stays reachable.
        for (const auto& panel : tabs.workspace.tabs) CHECK(l.panels().count(panel));
    }
    CHECK(layout::presetByKey("follow") == layout::Preset::FollowGame);
    CHECK_FALSE(layout::presetByKey("nothing"));
}

TEST_CASE("layouts: the user's arrangement read back is built again as it was") {
    // A tree a user could make: the standings beside the games, the openings under them, the schedule floating.
    Layout mine;
    mine.name         = "Mine";
    auto standings    = Node::leaf({"tab_standings", "tab_overview"}, "tab_overview");
    standings.central = true;
    mine.workspace    = Node::split2(
        Node::Split::Horizontal, 0.3f, std::move(standings),
        Node::split2(Node::Split::Vertical, 0.7f, Node::leaf({"tab_games", "tab_live"}), Node::leaf({"tab_openings"})));
    mine.floating.push_back({Node::leaf({"tab_schedule"}), 100, 120, 400, 300});

    Layout read;
    {
        Context c;
        c.apply(mine);
        read = c.capture();
        checkSame(read.workspace, mine.workspace);
        REQUIRE(read.floating.size() == 1);
        CHECK(read.floating[0].node.tabs == std::vector<std::string>{"tab_schedule"});
        CHECK(std::abs(read.floating[0].x - 100) < 1);
        CHECK(std::abs(read.floating[0].y - 120) < 1);
        CHECK(std::abs(read.floating[0].width - 400) < 1);
        CHECK(std::abs(read.floating[0].height - 300) < 1);
    }

    // Saved in the profile as JSON, and read back.
    read.name              = "Mine";
    const nlohmann::json j = read;
    const auto again       = j.get<Layout>();
    CHECK(again == read);
    CHECK(nlohmann::json::parse(j.dump()).get<Layout>() == read);

    // Built in another session.
    Context c;
    c.apply(again);
    const auto rebuilt = c.capture();
    checkSame(rebuilt.workspace, mine.workspace);
    CHECK(rebuilt.floating.size() == 1);

    // A panel closed drops out; a node left empty goes with it.
    c.open.erase("tab_openings");
    for (int i = 0; i < 3; ++i) c.frame();
    const auto fewer = c.capture();
    CHECK_FALSE(fewer.panels().count("tab_openings"));
    CHECK(leaves(fewer.workspace) == 2);
}

TEST_CASE("layouts: a damaged or foreign layout file is read without harm") {
    // Missing pieces and wrong types: a leaf without tabs, a ratio out of range, a floating part without a node.
    const auto j = nlohmann::json::parse(R"({"name": "odd", "workspace": {"split": "vertical", "ratio": 7,
        "first": {"tabs": ["tab_games"]}, "second": {}}, "floating": [{"x": 1}]})");
    const auto l = j.get<Layout>();
    CHECK(l.name == "odd");
    REQUIRE(l.workspace.children.size() == 2);
    CHECK(l.workspace.ratio <= 0.95f);
    CHECK(l.workspace.children[1].empty());
    CHECK(l.floating.empty());
    CHECK(l.panels() == std::set<std::string>{"tab_games"});

    CHECK(layout::detachedSlot("detached3") == 3);
    CHECK(layout::detachedSlot(layout::detachedPanel(12)) == 12);
    CHECK_FALSE(layout::detachedSlot("detached"));
    CHECK_FALSE(layout::detachedSlot("detached-1"));
    CHECK_FALSE(layout::detachedSlot("tab_live"));
}

TEST_CASE("layouts: the default one when layout.ini has none, the one kept when it has") {
    std::string ini;
    {
        Context c;
        c.frame();
        // No layout.ini: the dock space exists (DockSpace() made it) but holds no panel.
        CHECK_FALSE(layout::usable(DOCK));
        CHECK(layout::lost("tab_standings"));

        c.apply(layout::preset(layout::Preset::FollowGame));
        CHECK(layout::usable(DOCK));
        CHECK_FALSE(layout::lost("tab_standings"));
        CHECK(layout::dockedIn(DOCK, "tab_standings"));
        CHECK_FALSE(layout::dockedIn(DOCK, "tab_nothing"));
        ini = ImGui::SaveIniSettingsToMemory();
    }

    // Read again in the next session, as layout.ini: the same tree, from the settings alone (no window yet).
    {
        Context c;
        ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
        c.frame();
        CHECK(layout::usable(DOCK));
        c.open = layout::preset(layout::Preset::FollowGame).panels();
        for (int i = 0; i < 3; ++i) c.frame();
        checkSame(c.capture().workspace, layout::preset(layout::Preset::FollowGame).workspace);
    }

    // A layout.ini of version 0.5.0 (the shell and a detached board, no dock space) and one damaged.
    for (const char* text : {"[Window][##shell]\nPos=0,0\nSize=1400,900\nCollapsed=0\n\n[Window][###detached0]\n"
                             "Pos=60,60\nSize=720,480\nCollapsed=0\n\n",
                             "[Docking][Data]\nDockSpace ID=0x00005EED Pos=garbage\n  DockNode Parent=\n\x01\x02",
                             "[Window][###tab_games]\nDockId=0x12345678,0\n"}) {
        Context c;
        ImGui::LoadIniSettingsFromMemory(text);
        c.frame();
        CHECK_FALSE(layout::usable(DOCK));
        // A panel docked in a node that is not there needs a place.
        CHECK(layout::lost("tab_games"));
        // The default layout built over it.
        c.apply(layout::preset(layout::Preset::Tabs));
        CHECK(layout::usable(DOCK));
        checkSame(c.capture().workspace, layout::preset(layout::Preset::Tabs).workspace);
    }
}

TEST_CASE("layouts: a panel shown again goes to the node with the most panels") {
    Context c;
    c.apply(layout::preset(layout::Preset::FollowGame));
    auto* node = ImGui::DockBuilderGetNode(layout::fallbackNode(DOCK));
    REQUIRE(node);
    // The standings' node, under the board, with the other tabs.
    CHECK(node->Windows.Size > 1);
    bool standings = false;
    for (auto* w : node->Windows) standings |= std::string(w->Name).find("###tab_standings") != std::string::npos;
    CHECK(standings);
    CHECK(layout::fallbackNode(0x1234) == 0);
}

TEST_CASE("layouts: the command palette lists every panel and every layout") {
    // The panels of a round robin, as the workspace lists them, and a detached board.
    std::vector<std::string> panels;
    for (const auto& p : layout::allPanels()) {
        if (layout::available(p.id, false, false)) panels.emplace_back(p.id);
    }
    CHECK(std::find(panels.begin(), panels.end(), "tab_bracket") == panels.end());  // a knockout's only
    panels.push_back(layout::detachedPanel(2));

    const auto commands = layout::commands(panels, {"Mine", "Club night"});
    const auto has      = [&](layout::Command::Kind kind, const std::string& label, const std::string& target) {
        return std::any_of(commands.begin(), commands.end(), [&](const layout::Command& c) {
            return c.kind == kind && c.label == label && c.target == target;
        });
    };
    using K = layout::Command::Kind;
    for (const auto& [id, name] : std::vector<std::pair<std::string, std::string>>{{"tab_overview", "Overview"},
                                                                                   {"tab_standings", "Standings"},
                                                                                   {"tab_schedule", "Schedule"},
                                                                                   {"tab_broadcast", "Broadcast"},
                                                                                   {"tab_live", "Live"},
                                                                                   {"tab_games", "Games"},
                                                                                   {"tab_openings", "Openings"},
                                                                                   {"live_board", "Board"},
                                                                                   {"live_players", "Players"},
                                                                                   {"live_searches", "Searches"},
                                                                                   {"live_moves", "Moves"},
                                                                                   {"live_chart", "Chart"},
                                                                                   {"detached2", "Game #3"}}) {
        CAPTURE(id);
        CHECK(has(K::Show, "Show " + name, id));
    }
    CHECK(has(K::Preset, "Tabs", "tabs"));
    CHECK(has(K::Preset, "Follow a game", "follow"));
    CHECK(has(K::Preset, "Large tournament", "large"));
    CHECK(has(K::Preset, "Broadcast", "broadcast"));
    CHECK(has(K::Saved, "Mine", "Mine"));
    CHECK(has(K::Saved, "Club night", "Club night"));
    CHECK(commands.size() == panels.size() + static_cast<std::size_t>(layout::Preset::Count) + 2);

    // Every panel a layout names is one of the workspace.
    for (int i = 0; i < static_cast<int>(layout::Preset::Count); ++i) {
        for (const auto& panel : layout::preset(static_cast<layout::Preset>(i)).panels()) {
            CAPTURE(panel);
            CHECK(std::any_of(layout::allPanels().begin(), layout::allPanels().end(),
                              [&](const layout::PanelInfo& p) { return panel == p.id; }));
        }
    }
}
