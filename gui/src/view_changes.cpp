// Programs rebuilt or replaced: what the new read of an engine found (on its card), the engines of a setup whose
// program changed since their last tournament (in the setup's summary), and the participants of a resumed tournament
// whose program changed since they joined it (in the tournament's header).

#include <algorithm>
#include <optional>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "engine_changes.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

// How an option is described, as short as it goes: its default, then its bounds or its choices. The type first when
// it is not the one it had (`with_type`).
std::string optionText(const UciOptionInfo& o, bool with_type) {
    std::string text;
    if (o.type == "button") {
        text = TR("button");
    } else if (o.type == "string") {
        text = o.default_value.empty() ? std::string(TR("empty")) : o.default_value;
    } else {
        text = o.default_value;
    }
    if (o.type == "spin" && (!o.min.empty() || !o.max.empty())) text += fmt::format(" ({}…{})", o.min, o.max);
    if (o.type == "combo" && !o.vars.empty()) {
        std::string vars;
        for (const auto& v : o.vars) vars += (vars.empty() ? "" : " / ") + v;
        text += fmt::format(" ({})", vars);
    }
    return with_type && o.type != "button" ? fmt::format("{}: {}", o.type, text) : text;
}

// A table cell's text, the whole of it in a tooltip when the column cuts it.
void cellText(const std::string& text, bool disabled = false) {
    if (disabled)
        ImGui::TextDisabled("%s", text.c_str());
    else
        ImGui::TextUnformatted(text.c_str());
    if (ImGui::IsItemHovered() && ImGui::CalcTextSize(text.c_str()).x > ImGui::GetColumnWidth())
        ImGui::SetTooltip("%s", text.c_str());
}

void warnMark() {
    ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
    ImGui::TextUnformatted("!");
    ImGui::PopStyleColor();
    ImGui::SameLine();
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n : names) out += (out.empty() ? "" : ", ") + n;
    return out;
}

}  // namespace

void App::showEngineChanges(const std::string& name) {
    for (std::size_t i = 0; i < engines_.size(); ++i) {
        if (engines_[i].name != name) continue;
        page_            = Page::Engines;
        selected_engine_ = static_cast<int>(i);
        engine_marked_.clear();
        return;
    }
}

void App::settleEngineChanges(const std::string& name, std::optional<ValueChoice> choice) {
    for (auto& e : engines_) {
        if (e.name != name || !e.changes) continue;
        if (choice)
            (void)chooseAllValues(e, *choice);
        else
            e.changes.reset();
        storage_.saveEngines(engines_);
        return;
    }
}

void App::drawProgramChanges(int index, bool locked) {
    auto& e = engines_[index];
    if (!e.changes) return;
    const auto& c = *e.changes;

    // What was clicked, done once drawn: a choice may close the notice.
    std::optional<std::pair<std::string, ValueChoice>> pick;
    std::optional<ValueChoice> all;
    bool dismiss = false, rename = false;

    ui::beginCard("program_changes");
    const auto* dismiss_label = TR("Dismiss");
    const float dismiss_w     = ImGui::CalcTextSize(dismiss_label).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
    ImGui::TextUnformatted("Δ");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Its program changed: what the new read found"));
    if (!c.when.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", c.when.c_str());
    }
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - dismiss_w));
    if (ImGui::SmallButton(dismiss_label)) dismiss = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("Closes this notice. The options keep the values they have now."));

    const auto none = [](const std::string& s) { return s.empty() ? std::string(TR("none")) : s; };
    if (c.nameChanged()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(trf("Name: {} → {}", none(c.id_name_before), none(c.id_name_after)).c_str());
        // Named after what the engine reported: it may take its new name.
        if (e.name == c.id_name_before && !c.id_name_after.empty()) {
            ImGui::SameLine();
            ImGui::BeginDisabled(locked);
            if (ImGui::SmallButton(trf("Rename it “{}”", c.id_name_after).c_str())) rename = true;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", TR("The library's name, which the next tournaments give it. The tournaments "
                                           "already played keep the old one, and so do their games and ratings."));
            }
        }
    }
    if (c.authorChanged())
        ImGui::TextUnformatted(trf("Author: {} → {}", none(c.id_author_before), none(c.id_author_after)).c_str());

    const ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (!c.options.empty() && ImGui::BeginTable("##changes", 5, flags)) {
        // The choices in full; the descriptions share the rest.
        const auto button = [](const char* label) {
            return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
        };
        const float choices = std::max(button(TR("Keep my value")), button(TR("Keep the old default"))) +
                              button(TR("Use the new default")) + ImGui::GetStyle().ItemSpacing.x;
        ImGui::TableSetupColumn(TR("Option"), ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn(TR("Before"), ImGuiTableColumnFlags_WidthStretch, 0.27f);
        ImGui::TableSetupColumn(TR("Now"), ImGuiTableColumnFlags_WidthStretch, 0.27f);
        ImGui::TableSetupColumn(TR("Your value"), ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, choices);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < c.options.size(); ++i) {
            const auto& o         = c.options[i];
            const bool added      = o.before.name.empty();
            const bool gone       = o.after.name.empty();
            const bool type_moved = !added && !gone && o.before.type != o.after.type;
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            ImGui::TableNextColumn();
            cellText(o.name);
            ImGui::TableNextColumn();
            cellText(added ? std::string(TR("new")) : optionText(o.before, type_moved), added);
            ImGui::TableNextColumn();
            cellText(gone ? std::string(TR("gone")) : optionText(o.after, type_moved || added), gone);

            ImGui::TableNextColumn();
            if (!o.has_value) {
                ImGui::TextDisabled("%s", TR("default"));
            } else if (gone) {
                cellText(o.value, true);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", TR("Dropped from the library with the option: the engine no longer has "
                                               "it. Set it again on the option that replaces it, if one does."));
            } else if (!fitsOption(o.after, o.value)) {
                ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
                cellText(o.value);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", TR("Not a value the new build takes: fastchess would play without it."));
            } else {
                cellText(o.value);
            }

            ImGui::TableNextColumn();
            if (needsChoice(o)) {
                if (o.choice == 0) {
                    ImGui::BeginDisabled(locked);
                    if (ImGui::SmallButton(o.has_value ? TR("Keep my value") : TR("Keep the old default")))
                        pick.emplace(o.name, ValueChoice::Keep);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("%s", (o.has_value ? trf("{} stays at {}.", o.name, o.value)
                                                             : trf("{} is set to {}, its old default, as it played "
                                                                   "before.",
                                                                   o.name, o.before.default_value))
                                                    .c_str());
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(TR("Use the new default"))) pick.emplace(o.name, ValueChoice::NewDefault);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("%s", trf("{} plays with {}.", o.name, o.after.default_value).c_str());
                    ImGui::EndDisabled();
                } else {
                    ImGui::TextDisabled("%s", o.choice == 1 ? TR("✓ kept") : TR("✓ new default"));
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (choicesLeft(c) > 1) {
        ImGui::BeginDisabled(locked);
        if (ImGui::Button(TR("Keep my values"))) all = ValueChoice::Keep;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", TR("Every option listed plays as before the new build: with your value, or the "
                                       "old default where you had none."));
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Use the new defaults"))) all = ValueChoice::NewDefault;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("Every option listed plays with the default of the new build."));
        ImGui::EndDisabled();
    }
    ui::endCard();

    if (rename) {
        const auto name = c.id_name_after;
        renameEngine(index, name);
    }
    auto& entry = engines_[index];
    if (pick) (void)chooseValue(entry, pick->first, pick->second);
    if (all) (void)chooseAllValues(entry, *all);
    if (dismiss) entry.changes.reset();
    if (rename || pick || all || dismiss) storage_.saveEngines(engines_);
}

void App::drawRebuiltSincePlayed(const std::vector<std::string>& names) {
    refreshTournaments();  // the programs each engine played its last tournament with, read in the background
    for (const auto& e : engines_) {
        if (std::find(names.begin(), names.end(), e.name) == names.end()) continue;
        const auto* played = rebuiltSincePlayed(e, played_programs_);
        if (!played) continue;
        warnMark();
        ImGui::TextWrapped(
            "%s",
            trf("The program of {} changed since its last tournament, “{}”.", e.name, played->tournament).c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", trf("{}\nA new build, or another program: its results may not be those of the "
                                        "engine that played {}.",
                                        e.cmd, played->tournament)
                                        .c_str());
        }
        if (e.changes) {
            ImGui::Indent(ImGui::CalcTextSize("! ").x);
            if (ImGui::SmallButton(fmt::format("{}##changes_{}", TR("What changed"), e.name).c_str()))
                showEngineChanges(e.name);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("Its card, with the options whose default changed, new or gone."));
            ImGui::Unindent(ImGui::CalcTextSize("! ").x);
        }
    }
}

void App::drawResumedPrograms() {
    if (resumed_programs_.empty() || !run_.running()) return;
    std::vector<std::string> changed, missing;
    std::string paths;
    for (const auto& p : resumed_programs_) {
        (p.missing ? missing : changed).push_back(p.name);
        paths += (paths.empty() ? "" : "\n") + fmt::format("{}: {}", p.name, p.cmd);
    }
    std::string text;
    if (!changed.empty()) {
        text =
            trn("The program of {} changed since it joined the tournament: its next games are played by the new "
                "build.",
                "The programs of {} changed since they joined the tournament: their next games are played by the "
                "new builds.",
                changed.size(), joinNames(changed));
    }
    if (!missing.empty()) {
        text += (text.empty() ? "" : " ") +
                trn("The program of {} is missing: its games cannot start until it is back.",
                    "The programs of {} are missing: their games cannot start until they are back.", missing.size(),
                    joinNames(missing));
    }

    // The text, then Continue and Pause at its right.
    const bool can_pause = !run_.paused() && !run_.stopping();
    const auto& style    = ImGui::GetStyle();
    const auto button    = [&](const char* label) {
        return ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2 + style.ItemSpacing.x;
    };
    const char* go_on   = TR("Continue##resumed_programs");
    const char* pause   = TR("Pause##resumed_programs");
    const float buttons = button(go_on) + (can_pause ? button(pause) : 0.0f) + style.ItemSpacing.x;
    const float right   = ImGui::GetContentRegionMax().x;
    warnMark();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
    ImGui::PushTextWrapPos(std::max(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 10, right - buttons));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", paths.c_str());

    ImGui::SameLine();
    if (ImGui::SmallButton(go_on)) resumed_programs_.clear();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("The tournament goes on with the programs as they are now; this notice closes."));
    if (can_pause) {
        ImGui::SameLine();
        if (ImGui::SmallButton(pause)) {
            setRunPaused(true);
            resumed_programs_.clear();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("No new game pair starts, so that the program it joined with can be put back; "
                                       "Resume goes on."));
        }
    }
}

}  // namespace fcg
