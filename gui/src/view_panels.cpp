// The panels of the tournament workspace: each view, each part of the game followed and each detached board a window
// docked in the workspace's dock space, as tabs by default (as before docking), or side by side, or floating, even
// out of the main window. ImGui keeps where they are docked in layout.ini; which are open is kept there too, by the
// handler below. The Layout menu of the tournament header builds a ready-made layout or one the user saved
// (layouts.json), see layout.hpp.

#include <algorithm>
#include <cstring>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

#include "app.hpp"
#include "i18n.hpp"
#include "layout.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

// What layout.ini keeps of the panels: the open ones, comma separated. Outside the App, which goes away before ImGui
// writes layout.ini a last time.
struct PanelsIni {
    bool read = false;  // layout.ini had it (or the panels were set since)
    std::string open;
};

PanelsIni& panelsIni() {
    static PanelsIni ini;
    return ini;
}

void* iniOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name) {
    return std::strcmp(name, "Workspace") == 0 ? &panelsIni() : nullptr;
}

void iniLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
    auto& ini = *static_cast<PanelsIni*>(entry);
    if (std::strncmp(line, "Open=", 5) == 0) {
        ini.open = line + 5;
        ini.read = true;
    }
}

void iniWrite(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
    const auto& ini = panelsIni();
    if (!ini.read) return;
    buf->appendf("[%s][Workspace]\nOpen=%s\n\n", handler->TypeName, ini.open.c_str());
}

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// The frames a tab asked for (or in front in a layout) waits to be in front, the frames the tabs of a layout built are
// put in order.
constexpr int REQUEST_FRAMES = 30;
constexpr int ORDER_FRAMES   = 3;

const char* panelOfTab(int tab) {
    using namespace layout::panel;
    static constexpr const char* panels[] = {OVERVIEW, STANDINGS, BRACKET, SCHEDULE, LIVE, GAMES, OPENINGS, BROADCAST};
    return panels[tab];
}

}  // namespace

ImGuiID App::workspaceDock() {
    static const ImGuiID id = ImHashStr("fcg_workspace_dock");
    return id;
}

void App::registerPanelSettings() {
    if (ImGui::FindSettingsHandler("FcgPanels")) return;
    ImGuiSettingsHandler handler;
    handler.TypeName   = "FcgPanels";
    handler.TypeHash   = ImHashStr("FcgPanels");
    handler.ReadOpenFn = iniOpen;
    handler.ReadLineFn = iniLine;
    handler.WriteAllFn = iniWrite;
    ImGui::AddSettingsHandler(&handler);
}

void App::loadPanels() {
    if (panels_loaded_) return;
    panels_loaded_  = true;
    const auto& ini = panelsIni();
    if (!ini.read) return;
    std::size_t start = 0;
    while (start <= ini.open.size()) {
        const auto end = std::min(ini.open.find(',', start), ini.open.size());
        if (end > start) panels_open_.insert(ini.open.substr(start, end - start));
        start = end + 1;
    }
}

void App::panelsChanged() {
    auto& ini = panelsIni();
    ini.read  = true;
    ini.open.clear();
    for (const auto& p : panels_open_) ini.open += (ini.open.empty() ? "" : ",") + p;
    ImGui::MarkIniSettingsDirty();
}

bool App::panelAvailable(std::string_view panel) const {
    const auto* t = run_.tournament();
    if (!t) return false;
    // A detached board while its game can be played.
    if (const auto slot = layout::detachedSlot(panel)) {
        const auto* obs = run_.observer();
        return obs && *slot < obs->slotCount();
    }
    return layout::available(panel, isMatch(), t->spec().format == tour::Format::Knockout);
}

std::vector<std::string> App::panelsAvailable() const {
    std::vector<std::string> out;
    for (const auto& info : layout::allPanels()) {
        if (panelAvailable(info.id)) out.emplace_back(info.id);
    }
    // The detached boards open, in the order of their slots.
    std::vector<int> slots;
    for (const auto& p : panels_open_) {
        if (const auto slot = layout::detachedSlot(p); slot && panelAvailable(p)) slots.push_back(*slot);
    }
    std::sort(slots.begin(), slots.end());
    for (const int slot : slots) out.push_back(layout::detachedPanel(slot));
    return out;
}

void App::showPanel(const std::string& panel) {
    if (!panels_open_.count(panel)) {
        panels_open_.insert(panel);
        // Docked again in the workspace; a board detached floats, as its button says.
        if (!layout::detachedSlot(panel)) redock_.insert(panel);
        panelsChanged();
    }
    panel_request_        = panel;
    panel_request_frames_ = 0;
}

void App::closePanel(const std::string& panel) {
    if (panels_open_.erase(panel)) panelsChanged();
    if (panel_request_ == panel) panel_request_.reset();
}

void App::applyLayout(const layout::Layout& l) {
    page_           = Page::Tournament;
    layout_request_ = l;
}

bool App::applyLayout(const std::string& name) {
    if (const auto which = layout::presetByKey(name)) {
        applyLayout(layout::preset(*which));
        return true;
    }
    for (const auto& l : layouts_) {
        if (l.name == name) {
            applyLayout(l);
            return true;
        }
    }
    return false;
}

layout::Layout App::currentLayout() const {
    const auto* viewport = ImGui::GetMainViewport();
    return layout::capture(
        workspaceDock(), [this](std::string_view p) { return panels_open_.count(std::string(p)) > 0; }, viewport->Pos.x,
        viewport->Pos.y);
}

bool App::saveLayoutAs(const std::string& text) {
    const auto name = layout::trimName(text);
    if (name.empty()) return false;
    auto l = currentLayout();
    l.name = name;
    const auto it =
        std::find_if(layouts_.begin(), layouts_.end(), [&](const layout::Layout& x) { return x.name == name; });
    if (it != layouts_.end()) {
        *it = std::move(l);
    } else {
        layouts_.push_back(std::move(l));
    }
    storage_.saveLayouts(layouts_);
    Console::instance().add(trf("Layout saved: {}", name));
    return true;
}

void App::deleteLayout(const std::string& name) {
    const auto it =
        std::remove_if(layouts_.begin(), layouts_.end(), [&](const layout::Layout& x) { return x.name == name; });
    if (it == layouts_.end()) return;
    layouts_.erase(it, layouts_.end());
    storage_.saveLayouts(layouts_);
}

void App::drawWorkspaceDock() {
    const ImGuiID dock = workspaceDock();
    loadPanels();
    // Once a session: a layout.ini without a place for the panels open (none yet, one of an earlier version, or
    // damaged) gets the default layout. Every panel floating is a layout of the user's.
    if (!layout_checked_) {
        layout_checked_ = true;
        if (!layout_request_ && (!panelsIni().read || !layout::usable(panels_open_)))
            layout_request_ = layout::preset(layout::Preset::Tabs);
    }
    if (layout_request_) {
        const ImVec2 size    = ImGui::GetContentRegionAvail();
        const auto* viewport = ImGui::GetMainViewport();
        // The panels of another format (the bracket of a knockout) are open, put away, to be there in that format; a
        // node with none of the format's panels is left out rather than empty.
        auto open        = layout_request_->panels();
        const auto shown = layout::only(*layout_request_, [this](std::string_view p) { return panelAvailable(p); });
        const auto shown_panels = shown.panels();
        std::vector<std::string> hidden;
        for (const auto& p : open) {
            if (!shown_panels.count(p)) hidden.push_back(p);
        }
        // The boards detached and floating stay, unless the layout places them.
        for (const auto& p : panels_open_) {
            if (layout::detachedSlot(p) && !layout::dockedIn(dock, p)) open.insert(p);
        }
        // No panel keeps the focus: the tab of the focused window is the one in front of its node.
        ImGui::FocusWindow(nullptr);
        placements_   = layout::build(dock, size.x, size.y, shown, viewport->Pos.x, viewport->Pos.y, hidden);
        panels_open_  = std::move(open);
        fronts_       = shown.fronts();
        built_        = shown;
        built_frames_ = 0;
        redock_.clear();
        panelsChanged();
        layout_request_.reset();
    }
    ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton);
}

void App::drawPanels() {
    loadPanels();
    if (tab_request_ && run_.tournament()) {
        const std::string panel = panelOfTab(static_cast<int>(*tab_request_));
        tab_request_.reset();
        if (panelAvailable(panel)) showPanel(panel);
    }
    if (!run_.tournament()) return;  // what is asked for waits for the tournament (--focus before --autostart)
    if (panel_request_ && !panelAvailable(*panel_request_)) panel_request_.reset();

    // The game followed, for the panels of its parts, also when the live view is not drawn.
    if (const auto* obs = run_.observer(); obs && obs->slotCount() > 0) {
        live_slot_ = std::clamp(live_slot_, 0, obs->slotCount() - 1);
        refreshSlot(live_slot_);
    }

    // Docked in the workspace, a panel is shown with it; out of it, on every page. One closed, or that lost its
    // place, waits for the workspace to be docked again; a board detached for the first time floats in the middle.
    // The dock space submitted this frame, not the page: the page can change while the shell is drawn (Open on the
    // list of tournaments), and a panel drawn docked without its dock space would be undocked.
    const ImGuiID dock   = workspaceDock();
    const bool workspace = layout::shown(dock);
    bool focused_one     = false;
    std::string first_shown;
    for (const auto& panel : panelsAvailable()) {
        if (!panels_open_.count(panel)) continue;
        const auto slot   = layout::detachedSlot(panel);
        const bool docked = layout::dockedIn(dock, panel);
        const bool lost   = layout::lost(panel);
        const bool place  = redock_.count(panel) > 0 || (lost && !slot);
        if (!workspace && (docked || place)) continue;
        if (place) {
            if (!docked) ImGui::SetNextWindowDockID(layout::fallbackNode(dock), ImGuiCond_Always);
            redock_.erase(panel);
        } else if (lost) {
            const auto* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(
                ImVec2(std::min(px(720), viewport->WorkSize.x * 0.8f), std::min(px(480), viewport->WorkSize.y * 0.8f)),
                ImGuiCond_Always);
        }
        const bool requested = panel_request_ == panel;
        if (requested) ImGui::SetNextWindowFocus();
        placeIfAsked(panel);

        std::string title;
        if (slot) {
            title = trf("Game #{}", *slot + 1) + "###" + panel;
        } else {
            const auto it = std::find_if(layout::allPanels().begin(), layout::allPanels().end(),
                                         [&](const layout::PanelInfo& p) { return panel == p.id; });
            title         = TR(it->label);
        }
        // The live view's name leaves room for its badge.
        if (panel == layout::panel::LIVE) {
            const int playing = gamesPlaying();
            if (playing > 0) {
                const auto name = title.substr(0, title.find("##"));
                const float room =
                    ui::badgeWidth(std::to_string(playing).c_str()) + ImGui::GetStyle().ItemInnerSpacing.x;
                std::string padded = name;
                while (ImGui::CalcTextSize((padded + " ").c_str()).x - ImGui::CalcTextSize(name.c_str()).x < room)
                    padded += ' ';
                title = padded + "###" + panel;
            }
        }

        // Not focused as they appear: the focused window's tab is the one in front, so a layout built would show the
        // last panel of each node. The board fills its panel, without a scroll bar.
        auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing;
        if (panel == layout::panel::BOARD) flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        bool open          = true;
        const bool visible = ImGui::Begin(title.c_str(), &open, flags);
        if (panel == layout::panel::LIVE) drawLiveBadge();
        // A tab asked for is asked for until it is in front: a dock node's first frames can ignore it. Not for ever,
        // as it is focused meanwhile.
        if (requested && (layout::bringToFront() || ++panel_request_frames_ > REQUEST_FRAMES)) panel_request_.reset();
        if (fronts_.count(panel) && !requested && layout::bringToFront()) fronts_.erase(panel);
        if (visible) {
            if (first_shown.empty()) first_shown = panel;
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                panel_focused_ = panel;
                focused_one    = true;
            }
            drawPanelContent(panel);
        } else if (panel_focused_ == panel && !focused_one) {
            panel_focused_.clear();  // hidden behind another tab: the guide follows what is shown
        }
        ImGui::End();
        if (!open) closePanel(panel);
    }

    // The layout built last: its tabs in its order, though ImGui adds those of new windows after the others; those
    // in front, if they can be.
    if (workspace && (built_ || !fronts_.empty())) {
        ++built_frames_;
        if (built_) layout::orderTabs(*built_);
        if (built_frames_ >= ORDER_FRAMES) built_.reset();
        if (built_frames_ > REQUEST_FRAMES) fronts_.clear();
    }

    // The guide (F1, the `?` of the title) opens at the section of the panel focused last, else of the first shown.
    // The parts of the game followed and the detached boards are those of the live view.
    const auto& guide_panel = panel_focused_.empty() ? first_shown : panel_focused_;
    if (!guide_panel.empty()) shown_tab_ = Tab::Live;
    for (int tab = 0; tab <= static_cast<int>(Tab::Broadcast); ++tab) {
        if (guide_panel == panelOfTab(tab)) shown_tab_ = static_cast<Tab>(tab);
    }

    drawSaveLayout();
}

void App::placeIfAsked(const std::string& panel) {
    const auto it = std::find_if(placements_.begin(), placements_.end(),
                                 [&](const layout::Placement& p) { return p.panel == panel; });
    if (it == placements_.end()) return;
    layout::place(*it);
    placements_.erase(it);
}

void App::drawPanelContent(const std::string& panel) {
    using namespace layout::panel;
    if (const auto slot = layout::detachedSlot(panel)) {
        drawDetachedBoard(*slot);
        return;
    }
    for (const auto& [part, id] : LIVE_PARTS) {
        if (panel == id) {
            drawLivePart(part);
            return;
        }
    }
    if (panel == OVERVIEW) {
        if (isMatch()) {
            drawResults();
            drawCharts();
        } else {
            drawTournamentOverview();
        }
    } else if (panel == STANDINGS) {
        drawStandings();
    } else if (panel == BRACKET) {
        drawBracket();
    } else if (panel == SCHEDULE) {
        drawSchedule();
    } else if (panel == BROADCAST) {
        drawBroadcast();
    } else if (panel == LIVE) {
        drawLive();
    } else if (panel == GAMES) {
        drawGames();
    } else if (panel == OPENINGS) {
        drawOpenings();
    }
}

void App::drawLiveBadge() {
    const int playing = gamesPlaying();
    if (playing <= 0) return;
    // Begin() leaves its tab (or its title bar, floating) as the last item. The tab is drawn by the dock node's host.
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* dl      = window->DockIsActive && window->DockNode && window->DockNode->HostWindow
                              ? window->DockNode->HostWindow->DrawList
                              : window->DrawList;
    const auto count    = std::to_string(playing);
    const auto min      = ImGui::GetItemRectMin();
    const auto max      = ImGui::GetItemRectMax();
    const auto name     = std::string(TR("Live###tab_live"));
    const float x       = min.x + ImGui::GetStyle().FramePadding.x +
                          ImGui::CalcTextSize(name.substr(0, name.find("##")).c_str()).x +
                          ImGui::GetStyle().ItemInnerSpacing.x + ui::badgeWidth(count.c_str()) * 0.5f;
    dl->PushClipRectFullScreen();
    ui::drawBadge(dl, ImVec2(x, (min.y + max.y) * 0.5f), count.c_str());
    dl->PopClipRect();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", trn("{} game being played", "{} games being played", playing, playing).c_str());
}

// ---------------------------------------------------------------------------
// Layouts

void App::drawLayoutMenu() {
    if (ui::iconButton("layout", ui::Icon::Layout, TR("Layout")) || std::exchange(open_layout_menu_, false))
        ImGui::OpenPopup("layout_menu");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Arrange the panels: a ready-made layout, or one of yours. A panel dragged by its "
                                   "tab docks beside another, or floats."));
    }
    // Under its button.
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("layout_menu")) return;

    ImGui::TextDisabled("%s", TR("Ready-made"));
    for (int i = 0; i < static_cast<int>(layout::Preset::Count); ++i) {
        const auto which = static_cast<layout::Preset>(i);
        if (ImGui::MenuItem(TR(layout::presetLabel(which)))) applyLayout(layout::preset(which));
    }
    if (!layouts_.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", TR("Yours"));
        // By their rank: a name can be anything, "##" too.
        for (std::size_t i = 0; i < layouts_.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::MenuItem(layouts_[i].name.c_str())) applyLayout(layouts_[i]);
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    // Every panel, ticked when open: one closed comes back docked in the workspace.
    if (ImGui::BeginMenu(TR("Panels"))) {
        for (const auto& panel : panelsAvailable()) {
            const bool open = panels_open_.count(panel) > 0;
            if (ImGui::MenuItem(layout::panelName(panel).c_str(), nullptr, open)) {
                if (open)
                    closePanel(panel);
                else
                    showPanel(panel);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(TR("Save the layout as…"))) {
        layout_name_.clear();
        open_save_layout_ = true;
    }
    if (!layouts_.empty() && ImGui::BeginMenu(TR("Delete"))) {
        std::string gone;
        for (std::size_t i = 0; i < layouts_.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::MenuItem(layouts_[i].name.c_str())) gone = layouts_[i].name;
            ImGui::PopID();
        }
        ImGui::EndMenu();
        if (!gone.empty()) deleteLayout(gone);
    }
    ImGui::Separator();
    if (ImGui::MenuItem(TR("Default layout"))) applyLayout(layout::preset(layout::Preset::Tabs));
    ImGui::EndPopup();
}

void App::drawSaveLayout() {
    if (open_save_layout_) {
        ImGui::OpenPopup("###save_layout");
        open_save_layout_ = false;
    }
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(TR("Save the layout###save_layout"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted(TR("The panels and where they are, under a name of yours."));
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    char buffer[128];
    const auto n = std::min(layout_name_.size(), sizeof(buffer) - 1);
    std::copy_n(layout_name_.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(px(320));
    const bool enter = ImGui::InputTextWithHint("##layout_name", TR("Name"), buffer, sizeof(buffer),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    layout_name_     = buffer;
    // Spaces around the name are not part of it.
    const auto name = layout::trimName(layout_name_);
    const bool replaces =
        std::any_of(layouts_.begin(), layouts_.end(), [&](const layout::Layout& l) { return l.name == name; });
    if (replaces) ImGui::TextDisabled("%s", TR("Replaces your layout of that name."));
    ImGui::Spacing();
    ImGui::BeginDisabled(name.empty());
    if (ImGui::Button(TR("Save")) || (enter && !name.empty())) {
        saveLayoutAs(name);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

}  // namespace fcg
