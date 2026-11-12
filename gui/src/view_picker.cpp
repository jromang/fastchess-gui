// The setup's picker of participants: the library's engines with their reference Elo, sorted and filtered by it, and
// opponents suggested around the rating of the first participant.

#include <algorithm>
#include <cctype>

#include <imgui.h>
#include <imgui_internal.h>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool lessCaseInsensitive(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

}  // namespace

bool App::participantPicker(std::vector<ParticipantSetup>& list) {
    auto& p            = picker_;
    const auto in_list = [&](const std::string& name) {
        return std::any_of(list.begin(), list.end(), [&](const auto& q) { return q.name == name; });
    };

    // Opened for a screenshot: the popup of the combo, as BeginCombo names it.
    const int open_for_shot = std::exchange(open_picker_, 0);
    if (open_for_shot) ImGui::OpenPopup(ImHashStr("##ComboPopup", 0, ImGui::GetID("##add_participant")));
    ImGui::SetNextItemWidth(px(260));
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(660), 0), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::BeginCombo("##add_participant", TR("Add an engine…"), ImGuiComboFlags_HeightLargest)) return false;
    bool changed = false;
    if (ImGui::IsWindowAppearing()) {
        p.search.clear();
        p.ticked.clear();
        p.message.clear();
        ImGui::SetKeyboardFocusHere();
    }

    // Filters: words, a rating range, the engines that work.
    {
        char buffer[128];
        const auto n = std::min(p.search.size(), sizeof(buffer) - 1);
        std::copy_n(p.search.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(px(250));
        if (ImGui::InputTextWithHint("##picker_search", TR("Search: name, program, author, notes"), buffer,
                                     sizeof(buffer)))
            p.search = buffer;
    }
    const bool enter = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Elo"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(56));
    if (ImGui::InputInt("##picker_elo_min", &p.elo_min, 0, 0)) p.elo_min = std::clamp(p.elo_min, 0, 5000);
    ImGui::SameLine();
    ImGui::TextUnformatted("–");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(56));
    if (ImGui::InputInt("##picker_elo_max", &p.elo_max, 0, 0)) p.elo_max = std::clamp(p.elo_max, 0, 5000);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Reference Elo from … to …; 0: no bound."));
    ImGui::SameLine();
    ImGui::Checkbox(TR("Only those that work"), &p.works_only);

    // Opponents around the rating of the first participant: the tested engine of a gauntlet.
    const auto seed  = list.empty() ? engines_.end()
                                    : std::find_if(engines_.begin(), engines_.end(),
                                                   [&](const EngineEntry& e) { return e.name == list.front().name; });
    const bool rated = seed != engines_.end() && seed->elo > 0;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Suggest"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(44));
    if (ImGui::InputInt("##picker_count", &p.suggest, 0, 0)) p.suggest = std::clamp(p.suggest, 1, 500);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("opponents within ±"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(50));
    if (ImGui::InputInt("##picker_range", &p.range, 0, 0)) p.range = std::clamp(p.range, 1, 3000);
    ImGui::SameLine();
    ImGui::TextUnformatted(rated ? trf("Elo of {} ({})", seed->name, seed->elo).c_str() : TR("Elo of the first"));
    ImGui::SameLine();
    ImGui::BeginDisabled(!rated);
    if (ImGui::Button(TR("Suggest##picker")) || (open_for_shot == 2 && rated)) {
        std::vector<std::string> taken;
        for (const auto& q : list) taken.push_back(q.name);
        const auto names = suggestOpponents(engines_, seed->elo, p.range, p.suggest, taken);
        p.ticked         = {names.begin(), names.end()};
        // The rows shown are those it chose among.
        p.search.clear();
        p.elo_min    = std::max(0, seed->elo - p.range);
        p.elo_max    = std::min(5000, seed->elo + p.range);
        p.works_only = true;
        p.message    = names.empty() ? std::string(TR("No engine that works within that range. Engines that were "
                                                      "never checked do not count: check them first (Engines, "
                                                      "More…)."))
                                     : trn("{} opponent ticked, the closest first.",
                                           "{} opponents ticked, the closest first.", names.size(), names.size());
    }
    ImGui::EndDisabled();
    if (!rated && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(
            "%s", list.empty() ? TR("Add the engine to test first: it is the first participant.")
                               : trf("The first participant, {}, has no reference Elo.", list.front().name).c_str());
    }
    if (!p.message.empty()) ImGui::TextDisabled("%s", p.message.c_str());

    // The engines not in the list yet; inactive ones are not offered.
    std::vector<int> rows;
    for (int i = 0; i < static_cast<int>(engines_.size()); ++i) {
        const auto& e = engines_[i];
        if (e.inactive || in_list(e.name) || !engineMatches(e, p.search)) continue;
        if (p.works_only && !engineWorks(e)) continue;
        if ((p.elo_min > 0 && e.elo < p.elo_min) || (p.elo_max > 0 && (e.elo <= 0 || e.elo > p.elo_max))) continue;
        rows.push_back(i);
    }

    int add           = -1;  // a row clicked: that engine, at once
    const float row_h = ImGui::GetFrameHeightWithSpacing();
    const auto flags  = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("##picker_rows", 4, flags,
                          ImVec2(0, row_h * std::clamp(static_cast<float>(rows.size()) + 1, 4.0f, 14.0f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                ImGui::GetFrameHeight());
        ImGui::TableSetupColumn(TR("Engine"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn(TR("Elo"),
                                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, px(70));
        ImGui::TableSetupColumn(TR("Works"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, px(56));
        ImGui::TableHeadersRow();

        // By name, or by reference Elo (the engines without one last), as the header says.
        int column      = 1;
        bool descending = false;
        if (const auto* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
            column     = specs->Specs[0].ColumnIndex;
            descending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
        }
        std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
            const auto &x = engines_[a], &y = engines_[b];
            if (column == 2 && x.elo != y.elo) {
                if (x.elo <= 0 || y.elo <= 0) return y.elo <= 0;
                return descending ? x.elo > y.elo : x.elo < y.elo;
            }
            return descending && column == 1 ? lessCaseInsensitive(y.name, x.name)
                                             : lessCaseInsensitive(x.name, y.name);
        });

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const auto& e = engines_[rows[r]];
                ImGui::TableNextRow();
                ImGui::PushID(rows[r]);
                ImGui::TableNextColumn();
                bool ticked = p.ticked.count(e.name) > 0;
                if (ImGui::Checkbox("##ticked", &ticked)) {
                    if (ticked)
                        p.ticked.insert(e.name);
                    else
                        p.ticked.erase(e.name);
                }
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                if (ImGui::Selectable(e.name.c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups)) add = rows[r];
                if (!e.notes.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.notes.c_str());
                ImGui::TableNextColumn();
                if (e.elo > 0)
                    ImGui::Text("%d", e.elo);
                else
                    ImGui::TextDisabled("—");
                ImGui::TableNextColumn();
                const bool works = engineWorks(e);
                ImGui::PushStyleColor(ImGuiCol_Text, works ? ui::GOOD : e.check_ok < 0 ? ui::MUTED : ui::WARN);
                ImGui::TextUnformatted(works ? "✓" : e.check_ok < 0 ? "·" : "!");
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (engines_.empty()) ImGui::TextDisabled("%s", TR("Add engines in the Engines window."));
    if (enter && !rows.empty()) add = rows.front();

    // The ticked ones, in the order shown, then those the filters hide.
    std::vector<std::string> ticked;
    for (const int i : rows) {
        if (p.ticked.count(engines_[i].name)) ticked.push_back(engines_[i].name);
    }
    for (const auto& e : engines_) {
        if (p.ticked.count(e.name) && !in_list(e.name) &&
            std::find(ticked.begin(), ticked.end(), e.name) == ticked.end())
            ticked.push_back(e.name);
    }
    ImGui::BeginDisabled(ticked.empty());
    if (ImGui::Button(trn("Add the {} ticked", "Add the {} ticked", ticked.size(), ticked.size()).c_str())) {
        for (const auto& name : ticked) list.push_back({name});
        changed = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("Click an engine to add it, or tick several."));

    if (add >= 0) {
        list.push_back({engines_[add].name});
        changed = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndCombo();
    return changed;
}

}  // namespace fcg
