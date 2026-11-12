// The panels of the tournament workspace: each view a window docked in the workspace's dock space, as tabs by
// default (as before docking), or side by side, or floating, even out of the main window. ImGui keeps where they are
// docked in layout.ini; which are open is kept there too, by the handler below.

#include <algorithm>
#include <cstring>

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
    return layout::available(panel, isMatch(), t->spec().format == tour::Format::Knockout);
}

void App::showPanel(const std::string& panel) {
    if (!panels_open_.count(panel)) {
        panels_open_.insert(panel);
        redock_.insert(panel);
        panelsChanged();
    }
    panel_request_ = panel;
}

void App::closePanel(const std::string& panel) {
    if (panels_open_.erase(panel)) panelsChanged();
    if (panel_request_ == panel) panel_request_.reset();
}

void App::drawWorkspaceDock() {
    const ImGuiID dock = workspaceDock();
    loadPanels();
    // Once a session: a layout.ini without the workspace's panels (none yet, one of an earlier version, or damaged)
    // gets the default layout.
    if (!layout_checked_) {
        layout_checked_ = true;
        if (!panelsIni().read || !layout::usable(dock)) layout_request_ = layout::preset(layout::Preset::Tabs);
    }
    if (layout_request_) {
        const ImVec2 size    = ImGui::GetContentRegionAvail();
        const auto* viewport = ImGui::GetMainViewport();
        placements_          = layout::build(dock, size.x, size.y, *layout_request_, viewport->Pos.x, viewport->Pos.y);
        panels_open_         = layout_request_->panels();
        fronts_              = layout_request_->fronts();
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

    // Docked in the workspace, a panel is shown with it; out of it, on every page. One closed, or that lost its
    // place, waits for the workspace to be docked again.
    const ImGuiID dock   = workspaceDock();
    const bool workspace = page_ == Page::Tournament;
    bool focused_one     = false;
    std::string first_shown;
    for (const auto& info : layout::allPanels()) {
        const std::string panel = info.id;
        if (!panels_open_.count(panel) || !panelAvailable(panel)) continue;
        const bool docked = layout::dockedIn(dock, panel);
        const bool place  = redock_.count(panel) > 0 || layout::lost(panel);
        if (!workspace && (docked || place)) continue;
        if (place) {
            if (!docked) ImGui::SetNextWindowDockID(layout::fallbackNode(dock), ImGuiCond_Always);
            redock_.erase(panel);
        }
        const bool requested = panel_request_ == panel;
        if (requested) ImGui::SetNextWindowFocus();
        placeIfAsked(panel);

        // The live view's name leaves room for its badge.
        std::string title = TR(info.label);
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
        // last panel of each node.
        bool open = true;
        const bool visible =
            ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing);
        if (panel == layout::panel::LIVE) drawLiveBadge();
        // A tab asked for is asked for until it is in front: a dock node's first frames can ignore it.
        if (requested && layout::bringToFront()) panel_request_.reset();
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

    // The guide (F1, the `?` of the title) opens at the section of the panel focused last, else of the first shown.
    const auto& guide_panel = panel_focused_.empty() ? first_shown : panel_focused_;
    for (int tab = 0; tab <= static_cast<int>(Tab::Broadcast); ++tab) {
        if (guide_panel == panelOfTab(tab)) shown_tab_ = static_cast<Tab>(tab);
    }
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

}  // namespace fcg
