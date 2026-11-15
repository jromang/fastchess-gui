// The participants of the setup and the engine library beside them: engines added by a double-click, Enter or a
// drag, several at once with Ctrl or Shift, moved by a drag (seeds first), removed with Delete; each participant's
// own settings in its ⋯ menu.

#include <algorithm>
#include <cctype>

#include <imgui.h>
#include <imgui_internal.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "i18n.hpp"
#include "participants.hpp"
#include "setup_checks.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool lessCaseInsensitive(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A tooltip at the cursor for the item just drawn, wrapped.
void itemHelp(const std::string& text) {
    if (text.empty() || !ImGui::BeginItemTooltip()) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// What is wrong with a library engine, empty when nothing is known to be: its program gone or changed, its last
// check failed. `bad`: it cannot start at all.
std::string engineProblem(const EngineEntry& e, bool& bad) {
    bad = false;
    switch (e.program_state) {
        case ProgramState::Missing:
            bad = true;
            return trf("Program not found: {}", e.cmd);
        case ProgramState::MissingDir:
            bad = true;
            return trf("Working directory not found: {}", e.dir);
        case ProgramState::Changed:
            return TR("The program changed since it was last read (rebuilt or replaced).");
        default:
            break;
    }
    if (e.check_ok == 0) {
        return e.check_message.empty()
                   ? std::string(TR("The last check or reading failed."))
                   : fmt::format("{}\n{}", TR("The last check or reading failed."), e.check_message);
    }
    return "";
}

// The search of the library: the name, the name the engine gives, its author, program and notes, and its groups.
bool libraryMatches(const EngineEntry& e, const std::string& needle) {
    if (engineMatches(e, needle)) return true;
    const auto n = lower(needle);
    return std::any_of(e.groups.begin(), e.groups.end(),
                       [&](const auto& g) { return lower(g).find(n) != std::string::npos; });
}

const char* LIBRARY_PAYLOAD     = "FCG_LIBRARY";
const char* PARTICIPANT_PAYLOAD = "FCG_PARTICIPANT";

std::vector<int> selectedIndices(const ImGuiSelectionBasicStorage& selection, int count) {
    std::vector<int> out;
    for (int i = 0; i < count; ++i) {
        if (selection.Contains(static_cast<ImGuiID>(i))) out.push_back(i);
    }
    return out;
}

void selectIndices(ImGuiSelectionBasicStorage& selection, const std::vector<int>& indices) {
    selection.Clear();
    for (const int i : indices) selection.SetItemSelected(static_cast<ImGuiID>(i), true);
}

}  // namespace

bool App::addParticipants(const std::vector<std::string>& names, int at) {
    if (!addToSetup(setup_, names, at)) return false;
    // The participants moved down by the insertion: the selection, by index, would point at others.
    participants_selected_.Clear();
    saveSetupSoon();
    return true;
}

void App::setupParticipants() {
    auto& list        = setup_.participants;
    bool changed      = false;
    const int n       = static_cast<int>(list.size());
    const bool seeded = setup_.format == TournamentFormat::Gauntlet;
    const bool teams  = setup_.format == TournamentFormat::TeamMatch;
    const int seeds   = seeded ? std::clamp(setup_.seeds, 1, std::max(1, n - 1)) : 0;
    auto& selection   = participants_selected_;
    const auto& io    = ImGui::GetIO();

    // The title, with what can be done here at the cursor.
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("Participants"));
    const auto how =
        std::string(seeded  ? TR("The first ones are the seeds, the tested engines: they play all the "
                                 "others.")
                    : teams ? TR("A1, A2… are team A, B1, B2… team B: the upper half of the list against the lower "
                                 "half, until engines are put in a team with their ⋯ or the Team A and Team B buttons "
                                 "under the list. Board by board, A1 plays B1, A2 plays B2, and so on.")
                    : setup_.format == TournamentFormat::RoundRobin ? TR("The order is the one of the pairings' table.")
                                                                    : TR("The list order is the seeding.")) +
        "\n\n" +
        TR("Drag to reorder, or Ctrl+Up and Ctrl+Down to the top or the bottom, Alt+Up and Alt+Down by "
           "one. Ctrl or Shift click selects several; Delete removes them. ⋯, or a right click, opens a "
           "participant's own settings.");
    itemHelp(how);
    ImGui::SameLine();
    ImGui::TextDisabled("%d", n);
    itemHelp(how);

    // The keys, while a participant has the keyboard: applied before the list is drawn, to the selection as it was.
    auto picked = selectedIndices(selection, n);
    if (participants_focused_ && !io.WantTextInput && !picked.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            removeParticipants(list, picked);
            selection.Clear();
            changed = true;
        } else if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))) {
            const bool top = ImGui::IsKeyPressed(ImGuiKey_UpArrow);
            selectIndices(selection, moveParticipants(list, picked, top ? 0 : n));
            changed = true;
        } else if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow) && picked.front() > 0) {
            selectIndices(selection, moveParticipants(list, picked, picked.front() - 1));
            changed = true;
        } else if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_DownArrow) && picked.back() < n - 1) {
            selectIndices(selection, moveParticipants(list, picked, picked.back() + 2));
            changed = true;
        }
    }

    const auto names = participantNames(setup_);
    const int count  = static_cast<int>(list.size());
    // A team match: each participant's team and its place in it.
    const auto team_of = teams ? setupTeams(setup_) : std::vector<int>();
    std::vector<int> board_of(team_of.size());
    int in_team[2] = {0, 0};
    for (std::size_t i = 0; i < team_of.size(); ++i) board_of[i] = in_team[team_of[i]]++;
    int drop_at = -1, drop_from = -1, library_at = -1;
    ParticipantAction action = ParticipantAction::None;
    int action_on            = -1;
    bool focused_now         = false;

    const ImVec2 list_min = ImGui::GetCursorScreenPos();
    const float list_w    = ImGui::GetContentRegionAvail().x;
    const auto flags      = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##participants", 4, flags, ImVec2(list_w, players_list_h_))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, px(34));
        ImGui::TableSetupColumn(TR("Engine"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Elo"), ImGuiTableColumnFlags_WidthFixed, px(44));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetTextLineHeight() * 1.6f);

        // Rows of a line of text, as the library's: ⋯ is a small button.
        const float line = ImGui::GetTextLineHeight();
        auto* ms =
            ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ClearOnClickVoid |
                                        ImGuiMultiSelectFlags_BoxSelect1d,
                                    selection.Size, count);
        selection.ApplyRequests(ms);
        for (int i = 0; i < count; ++i) {
            auto& p = list[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            // The place, a star for the seeds; a team match's team and board.
            ImGui::TableNextColumn();
            const bool seed = i < seeds;
            if (teams && i < static_cast<int>(team_of.size())) {
                ImGui::PushStyleColor(ImGuiCol_Text, team_of[i] == 0 ? ui::ACCENT : ui::WARN);
                ImGui::Text("%s%d", team_of[i] == 0 ? "A" : "B", board_of[i] + 1);
                ImGui::PopStyleColor();
            } else if (seed) {
                const ImVec2 at = ImGui::GetCursorScreenPos();
                ui::drawIcon(ImGui::GetWindowDrawList(), ui::Icon::Star,
                             ImVec2(at.x + line * 0.45f, at.y + line * 0.5f), line * 0.75f, ui::ACCENT);
                ImGui::Dummy(ImVec2(line, line));
                ImGui::SameLine(0, 0);
                ImGui::PushStyleColor(ImGuiCol_Text, ui::ACCENT);
                ImGui::Text("%d", i + 1);
                ImGui::PopStyleColor();
            } else {
                ImGui::TextDisabled("%d", i + 1);
            }

            // The name it plays under, what it does differently, and its problems.
            ImGui::TableNextColumn();
            ImGui::SetNextItemSelectionUserData(i);
            ImGui::Selectable(names[i].c_str(), selection.Contains(static_cast<ImGuiID>(i)),
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
            if (ImGui::IsItemFocused()) focused_now = true;
            ImGui::OpenPopupOnItemClick("participant_menu", ImGuiPopupFlags_MouseButtonRight);
            if (ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Menu, false) ||
                                           ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_F10))) {
                ImGui::OpenPopup("participant_menu");
            }
            // Dragged: the selection if it is part of it, else this one alone.
            if (ImGui::BeginDragDropSource()) {
                if (!ImGui::GetDragDropPayload()) ImGui::SetDragDropPayload(PARTICIPANT_PAYLOAD, &i, sizeof(int));
                const bool many = selection.Contains(static_cast<ImGuiID>(i)) && selection.Size > 1;
                ImGui::TextUnformatted(many ? trn("{} participant", "{} participants",
                                                  static_cast<uint64_t>(selection.Size), selection.Size)
                                                  .c_str()
                                            : names[i].c_str());
                ImGui::EndDragDropSource();
            }
            // Dropped on: before it in its upper half, after it in its lower half, a line where it goes.
            if (ImGui::BeginDragDropTarget()) {
                const ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
                const bool after = ImGui::GetMousePos().y > (r0.y + r1.y) * 0.5f;
                const float y    = after ? r1.y : r0.y;
                ImGui::GetWindowDrawList()->AddLine(ImVec2(list_min.x, y), ImVec2(list_min.x + list_w, y), ui::ACCENT,
                                                    2.0f);
                const auto accept = ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
                if (const auto* payload = ImGui::AcceptDragDropPayload(PARTICIPANT_PAYLOAD, accept)) {
                    drop_from = *static_cast<const int*>(payload->Data);
                    drop_at   = i + (after ? 1 : 0);
                }
                if (ImGui::AcceptDragDropPayload(LIBRARY_PAYLOAD, accept)) library_at = i + (after ? 1 : 0);
                ImGui::EndDragDropTarget();
            }

            const auto entry =
                std::find_if(engines_.begin(), engines_.end(), [&](const EngineEntry& e) { return e.name == p.name; });
            // An engine that runs on a machine: the machine's name after its own.
            if (entry != engines_.end() && !entry->machine.empty()) {
                const auto* m = machineById(entry->machine);
                ImGui::SameLine();
                ui::inlineIcon(ui::Icon::Server, ui::MUTED);
                ImGui::SameLine(0, 0);
                ImGui::TextDisabled("%s", m ? (m->name.empty() ? m->host : m->name).c_str() : "?");
            }
            const auto differences = participantDifferences(p, setup_);
            std::string note;
            for (const auto& d : differences) note += (note.empty() ? "" : " · ") + d;
            const bool twice = std::find(names.begin(), names.begin() + i, names[i]) != names.begin() + i;
            if (entry == engines_.end() || twice) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
                ImGui::TextUnformatted(entry == engines_.end() ? TR("(not in the engine list)")
                                                               : TR("(twice: give it another book)"));
                ImGui::PopStyleColor();
            } else if (const auto threads = threadNotice(setup_, static_cast<std::size_t>(i), engines_, topology_)) {
                // Too many threads, its own default perhaps: the summary offers to lower them.
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
                ImGui::TextUnformatted("!");
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("%s", threads->text.c_str());
            }
            if (entry != engines_.end() && !twice && !note.empty()) {
                // As much as the cell shows; all of it at the cursor.
                ImGui::SameLine();
                const ImVec2 at  = ImGui::GetCursorScreenPos();
                const float room = ImGui::GetContentRegionAvail().x;
                auto* dl         = ImGui::GetWindowDrawList();
                dl->PushClipRect(at, ImVec2(at.x + room, at.y + line), true);
                dl->AddText(at, ImGui::GetColorU32(ImGuiCol_TextDisabled), note.c_str());
                dl->PopClipRect();
                ImGui::Dummy(ImVec2(std::max(1.0f, room), line));
                itemHelp(trf("{} plays with its own settings: {}", names[i], note));
            }

            // Its reference Elo.
            ImGui::TableNextColumn();
            if (entry != engines_.end() && entry->elo > 0) {
                const auto elo = std::to_string(entry->elo);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                                     ImGui::CalcTextSize(elo.c_str()).x);
                ImGui::TextDisabled("%s", elo.c_str());
            }

            // ⋯: its own settings, marked when it has some.
            ImGui::TableNextColumn();
            if (!differences.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ui::ACCENT);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            const bool more = ImGui::Button("##more", ImVec2(line * 1.6f, line));
            ImGui::PopStyleVar();
            ui::drawIcon(ImGui::GetWindowDrawList(), ui::Icon::More,
                         ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
                                (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f),
                         line * 0.8f, ImGui::GetColorU32(ImGuiCol_Text));
            if (!differences.empty()) ImGui::PopStyleColor();
            itemHelp(TR("Its own settings: Hash, Threads, nodes, its own book, time odds."));
            const bool open_book = open_own_book_ == i;
            if (open_book) open_own_book_ = -1;
            if (more || participant_menu_ == i || open_book) {
                participant_menu_ = -1;
                ImGui::OpenPopup("participant_menu");
                // Under its ⋯ rather than at the mouse: also when the keyboard or a screenshot opened it.
                ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
            }
            bool menu_changed = false;
            const auto chosen = participantMenu(p, i, open_book, menu_changed);
            changed |= menu_changed;
            if (chosen != ParticipantAction::None) {
                action    = chosen;
                action_on = i;
            }
            ImGui::PopID();
        }
        ms = ImGui::EndMultiSelect();
        selection.ApplyRequests(ms);
        if (count == 0) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("%s", TR("No engine yet: double-click engines of the library, or drag them here."));
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTable();
    }
    participants_focused_ = focused_now;
    // The library's engines dropped below the last participant: at the end.
    if (ImGui::BeginDragDropTargetCustom(ImRect(list_min, ImVec2(list_min.x + list_w, list_min.y + players_list_h_)),
                                         ImGui::GetID("##participants_drop"))) {
        if (ImGui::AcceptDragDropPayload(LIBRARY_PAYLOAD)) library_at = count;
        if (const auto* payload = ImGui::AcceptDragDropPayload(PARTICIPANT_PAYLOAD)) {
            drop_from = *static_cast<const int*>(payload->Data);
            drop_at   = count;
        }
        ImGui::EndDragDropTarget();
    }

    // What the list asked for, now that it is drawn.
    if (drop_from >= 0) {
        const auto moved = selection.Contains(static_cast<ImGuiID>(drop_from)) ? selectedIndices(selection, count)
                                                                               : std::vector<int>{drop_from};
        selectIndices(selection, moveParticipants(list, moved, drop_at));
        changed = true;
    }
    if (library_at >= 0 && addParticipants(library_drag_, library_at)) {
        library_.selected.Clear();
        changed = true;
    }
    switch (action) {
        case ParticipantAction::Again: {
            // The same engine below it, its settings without the book, whose popup opens for the other book.
            auto copy = list[action_on];
            copy.book = {};
            list.insert(list.begin() + action_on + 1, copy);
            selectIndices(selection, {action_on + 1});
            open_own_book_ = action_on + 1;
            changed        = true;
            break;
        }
        case ParticipantAction::Top:
            selectIndices(selection, moveParticipants(list, {action_on}, 0));
            changed = true;
            break;
        case ParticipantAction::Up:
            selectIndices(selection, moveParticipants(list, {action_on}, std::max(0, action_on - 1)));
            changed = true;
            break;
        case ParticipantAction::Down:
            selectIndices(selection, moveParticipants(list, {action_on}, action_on + 2));
            changed = true;
            break;
        case ParticipantAction::Remove:
            removeParticipants(list, {action_on});
            selection.Clear();
            changed = true;
            break;
        case ParticipantAction::None:
            break;
    }

    // Below: remove the selected ones, or all.
    const auto chosen_now = selectedIndices(selection, static_cast<int>(list.size()));
    ImGui::BeginDisabled(chosen_now.empty());
    if (ImGui::SmallButton(chosen_now.size() > 1 ? trf("Remove the {}", chosen_now.size()).c_str() : TR("Remove"))) {
        removeParticipants(list, chosen_now);
        selection.Clear();
        changed = true;
    }
    ImGui::EndDisabled();
    itemHelp(TR("The participants selected (Delete)."));
    if (!list.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Clear"))) {
            list.clear();
            selection.Clear();
            changed = true;
        }
        itemHelp(TR("Removes every participant from the list."));
    }
    // A team match: the selected ones to a team, or the halves of the list again.
    if (teams && !list.empty()) {
        ImGui::SameLine(0, px(16));
        ImGui::BeginDisabled(chosen_now.empty());
        for (int team = 0; team < 2; ++team) {
            ImGui::PushID(team);
            if (ImGui::SmallButton(team == 0 ? TR("Team A") : TR("Team B"))) {
                setTeam(setup_, chosen_now, team);
                changed = true;
            }
            itemHelp(team == 0 ? TR("Put the participants selected in team A.")
                               : TR("Put the participants selected in team B."));
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::EndDisabled();
        if (ImGui::SmallButton(TR("Halves"))) {
            splitTeams(setup_);
            changed = true;
        }
        itemHelp(TR("The teams made again from the list: its upper half against its lower half."));
    }

    if (changed) saveSetupSoon();
}

App::ParticipantAction App::participantMenu(ParticipantSetup& p, int index, bool open_book, bool& changed) {
    // Under its ⋯, also when it was opened by a right click or the keyboard.
    if (!ImGui::BeginPopup("participant_menu")) return ParticipantAction::None;
    auto action = ParticipantAction::None;
    const auto entry =
        std::find_if(engines_.begin(), engines_.end(), [&](const EngineEntry& e) { return e.name == p.name; });
    ImGui::TextUnformatted(p.name.c_str());
    if (entry != engines_.end() && entry->elo > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d", entry->elo);
    }
    const float label_w = px(130), field_w = px(120);
    // A team match: its team, first.
    if (setup_.format == TournamentFormat::TeamMatch) {
        const auto teams = setupTeams(setup_);
        int team         = index >= 0 && index < static_cast<int>(teams.size()) ? teams[index] : 0;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR("Team"));
        ImGui::SameLine(label_w);
        const auto name = [](const std::string& given, const char* fallback) {
            return given.empty() ? std::string(fallback) : given;
        };
        bool moved = ImGui::RadioButton((name(setup_.team_a, TR("Team A")) + "###team_a").c_str(), &team, 0);
        ImGui::SameLine();
        moved |= ImGui::RadioButton((name(setup_.team_b, TR("Team B")) + "###team_b").c_str(), &team, 1);
        if (moved) {
            setTeam(setup_, {index}, team);
            changed = true;
        }
    }
    ImGui::TextDisabled("%s", TR("Its own settings, before the common ones:"));

    const auto field = [&](const char* label, const char* id, int& value, int min, int max, const std::string& common) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(field_w);
        if (ui::inputInt(id, &value)) {
            value   = std::clamp(value, min, max);
            changed = true;
        }
        itemHelp(common);
    };
    const auto common = [](int value, const char* unit) {
        return value > 0 ? trf("0 = the common setting: {}", fmt::format("{}{}", value, unit))
                         : std::string(TR("0 = the common setting, or the engine's own."));
    };
    field(TR("Hash (MB)"), "##hash", p.hash, 0, 1 << 20, common(setup_.common_hash, " MB"));
    field(TR("Threads"), "##threads", p.threads, 0, 1024, common(setup_.common_threads, ""));
    if (setup_.limit == LimitKind::Nodes) {
        int nodes = static_cast<int>(std::min<uint64_t>(p.nodes, INT32_MAX));
        field(TR("Nodes"), "##nodes", nodes, 0, INT32_MAX, trf("0 = the common value: {}", setup_.nodes));
        p.nodes = static_cast<uint64_t>(std::max(0, nodes));
    } else if (setup_.limit == LimitKind::Depth) {
        field(TR("Depth"), "##depth", p.depth, 0, 200, trf("0 = the common value: {}", setup_.depth));
    } else {
        field(TR("Time %"), "##time", p.time_percent, 1, 1000,
              TR("Time odds: its share of the time control, 100 = the same clock as the others."));
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Book"));
    ImGui::SameLine(label_w);
    bool once_more = false;
    changed |= ownBookButton("book", p.book, px(220), &once_more, open_book);
    if (once_more) action = ParticipantAction::Again;

    ImGui::Separator();
    if (ImGui::Selectable(TR("This engine once more, with another book"))) action = ParticipantAction::Again;
    ImGui::BeginDisabled(index == 0);
    if (ImGui::Selectable(TR("Move to the top"))) action = ParticipantAction::Top;
    if (ImGui::Selectable(TR("Move up"))) action = ParticipantAction::Up;
    ImGui::EndDisabled();
    ImGui::BeginDisabled(index + 1 >= static_cast<int>(setup_.participants.size()));
    if (ImGui::Selectable(TR("Move down"))) action = ParticipantAction::Down;
    ImGui::EndDisabled();
    if (ImGui::Selectable(TR("Remove"))) action = ParticipantAction::Remove;
    if (action != ParticipantAction::None) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return action;
}

bool App::participantPicker() {
    auto& lib          = library_;
    const bool match   = setup_.format == TournamentFormat::Match;
    const auto in_play = [&](const std::string& name) {
        if (match) return name == setup_.engine_a || name == setup_.engine_b;
        return std::any_of(setup_.participants.begin(), setup_.participants.end(),
                           [&](const auto& p) { return p.name == name; });
    };
    std::vector<std::string> add;  // the engines to add once the list is drawn

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("Library"));
    const std::string how =
        TR("Double-click an engine, or select engines and press Enter, to add them; Ctrl or "
           "Shift click selects several, Ctrl+A all those shown. Drag them to a place among the "
           "participants. Ctrl+F: the filter.");
    itemHelp(how);

    // The search, focused for a screenshot; Enter in it adds the engines selected, or the first one shown.
    // Ctrl+F on the page, or a screenshot, gives it the keyboard.
    const int open_for_shot = std::exchange(open_picker_, 0);
    if (open_for_shot == 1 || (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F) && !ImGui::GetIO().WantTextInput &&
                               !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))) {
        ImGui::SetKeyboardFocusHere();
    }
    char buffer[128];
    const auto n = std::min(lib.search.size(), sizeof(buffer) - 1);
    std::copy_n(lib.search.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool search_enter = ImGui::InputTextWithHint("##library_search", TR("Filter: name, group, notes"), buffer,
                                                       sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue);
    lib.search              = buffer;

    // A chip for each group: one at a time, clicked again to show every engine.
    std::vector<std::string> groups;
    for (const auto& e : engines_) {
        if (e.inactive) continue;
        for (const auto& g : e.groups) {
            if (std::find(groups.begin(), groups.end(), g) == groups.end()) groups.push_back(g);
        }
    }
    std::sort(groups.begin(), groups.end(), lessCaseInsensitive);
    if (!lib.group.empty() && std::find(groups.begin(), groups.end(), lib.group) == groups.end()) lib.group.clear();
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const auto& name = groups[g];
        const bool on    = name == lib.group;
        const float w    = ImGui::CalcTextSize(name.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
        if (g > 0) ui::sameLineOrWrap(w);
        ImGui::PushID(static_cast<int>(g));
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
        if (on) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        if (ImGui::SmallButton(name.c_str())) lib.group = on ? std::string() : name;
        if (on) ImGui::PopStyleColor(2);
        ImGui::PopID();
        itemHelp(on ? TR("Every engine again") : trf("Only the engines of the group {}", name));
    }

    // The engines shown, the inactive ones left out.
    std::vector<int> rows;
    for (int i = 0; i < static_cast<int>(engines_.size()); ++i) {
        const auto& e = engines_[i];
        if (e.inactive || !libraryMatches(e, lib.search)) continue;
        if (!lib.group.empty() && std::find(e.groups.begin(), e.groups.end(), lib.group) == e.groups.end()) continue;
        rows.push_back(i);
    }
    const auto selectedNames = [&](int fallback) {
        std::vector<std::string> out;
        for (const int i : rows) {
            if (lib.selected.Contains(ImHashStr(engines_[i].name.c_str())) && !in_play(engines_[i].name))
                out.push_back(engines_[i].name);
        }
        if (out.empty() && fallback >= 0 && !in_play(engines_[fallback].name)) out.push_back(engines_[fallback].name);
        return out;
    };
    // Enter, while one of the engines has the keyboard: the selection as it was before this frame's.
    if (lib.focused && !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
        add = selectedNames(-1);
    }

    // Opponents around the rating of the first participant (the tested engine of a gauntlet), selected.
    const std::string first = match                         ? setup_.engine_a
                              : setup_.participants.empty() ? std::string()
                                                            : setup_.participants.front().name;
    const auto seed =
        std::find_if(engines_.begin(), engines_.end(), [&](const EngineEntry& e) { return e.name == first; });
    const bool rated   = seed != engines_.end() && seed->elo > 0;
    const auto suggest = [&] {
        std::vector<std::string> taken;
        for (const auto& name : enginesOfSetup(setup_)) taken.push_back(name);
        const auto names = suggestOpponents(engines_, seed->elo, lib.range, lib.suggest, taken);
        lib.search.clear();
        lib.group.clear();
        lib.selected.Clear();
        for (const auto& name : names) lib.selected.SetItemSelected(ImHashStr(name.c_str()), true);
        lib.message = names.empty() ? std::string(TR("No engine that works within that range. Engines that were "
                                                     "never checked do not count: check them first (Engines, "
                                                     "More…)."))
                                    : trn("{} opponent selected, the closest first: Enter adds them.",
                                          "{} opponents selected, the closest first: Enter adds them.", names.size(),
                                          names.size());
    };
    if (open_for_shot == 2 && rated) suggest();

    std::vector<ImGuiID> ids;
    bool focused_now = false;
    const auto flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##library_rows", 3, flags, ImVec2(-FLT_MIN, players_list_h_))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, px(14));
        ImGui::TableSetupColumn(TR("Engine"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn(TR("Elo"),
                                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, px(52));
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
        for (const int i : rows) ids.push_back(ImHashStr(engines_[i].name.c_str()));
        lib.selected.UserData                = &ids;
        lib.selected.AdapterIndexToStorageId = [](ImGuiSelectionBasicStorage* self, int idx) {
            return (*static_cast<std::vector<ImGuiID>*>(self->UserData))[static_cast<std::size_t>(idx)];
        };

        auto* ms =
            ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ClearOnClickVoid |
                                        ImGuiMultiSelectFlags_BoxSelect1d,
                                    lib.selected.Size, static_cast<int>(rows.size()));
        lib.selected.ApplyRequests(ms);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        if (ms->RangeSrcItem != -1) clipper.IncludeItemByIndex(static_cast<int>(ms->RangeSrcItem));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const auto& e = engines_[rows[r]];
                const bool in = in_play(e.name);
                ImGui::PushID(rows[r]);
                ImGui::TableNextRow();

                // A mark for an engine with a problem, or one that plays already.
                ImGui::TableNextColumn();
                bool bad           = false;
                const auto problem = engineProblem(e, bad);
                if (!problem.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, bad ? ui::BAD : ui::WARN);
                    ImGui::TextUnformatted("!");
                    ImGui::PopStyleColor();
                    itemHelp(problem);
                } else if (in) {
                    ImGui::TextDisabled("✓");
                }

                ImGui::TableNextColumn();
                ImGui::SetNextItemSelectionUserData(r);
                if (in) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::Selectable(e.name.c_str(), lib.selected.Contains(ids[static_cast<std::size_t>(r)]),
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
                if (in) ImGui::PopStyleColor();
                if (ImGui::IsItemFocused()) focused_now = true;
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !in) add = {e.name};
                {
                    std::string tip =
                        in ? std::string(match ? TR("A or B of the match.") : TR("A participant already."))
                           : std::string(TR("Double-click or Enter: add it."));
                    if (!e.groups.empty()) {
                        std::string g;
                        for (const auto& x : e.groups) g += (g.empty() ? "" : ", ") + x;
                        tip += "\n" + trf("Groups: {}", g);
                    }
                    if (!problem.empty()) tip += "\n" + problem;
                    if (!e.notes.empty()) tip += "\n\n" + e.notes;
                    itemHelp(tip);
                }
                // Dragged to the participants: the selection if it is part of it, else this one.
                if (ImGui::BeginDragDropSource()) {
                    if (!ImGui::GetDragDropPayload()) {
                        library_drag_     = lib.selected.Contains(ids[static_cast<std::size_t>(r)])
                                                ? selectedNames(-1)
                                                : std::vector<std::string>{e.name};
                        const char marker = 1;
                        ImGui::SetDragDropPayload(LIBRARY_PAYLOAD, &marker, 1);
                    }
                    ImGui::TextUnformatted(
                        library_drag_.size() == 1
                            ? library_drag_.front().c_str()
                            : trn("{} engine", "{} engines", library_drag_.size(), library_drag_.size()).c_str());
                    ImGui::EndDragDropSource();
                }

                ImGui::TableNextColumn();
                if (e.elo > 0) {
                    const auto elo = std::to_string(e.elo);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                                         ImGui::CalcTextSize(elo.c_str()).x);
                    ImGui::TextDisabled("%s", elo.c_str());
                }
                ImGui::PopID();
            }
        }
        ms = ImGui::EndMultiSelect();
        lib.selected.ApplyRequests(ms);
        ImGui::EndTable();
    }
    lib.selected.UserData = nullptr;
    lib.focused           = focused_now;
    if (search_enter) {
        // The engines selected, or else the first one shown that does not play yet.
        const auto first_free =
            std::find_if(rows.begin(), rows.end(), [&](int i) { return !in_play(engines_[i].name); });
        add = selectedNames(first_free == rows.end() ? -1 : *first_free);
    }
    if (engines_.empty()) ImGui::TextDisabled("%s", TR("Add engines in the Engines window."));

    // Below: add the engines selected, or all those shown; suggest opponents.
    const auto chosen = selectedNames(-1);
    ImGui::BeginDisabled(chosen.empty());
    if (ImGui::SmallButton(chosen.size() > 1 ? trf("Add the {}", chosen.size()).c_str() : TR("Add"))) add = chosen;
    ImGui::EndDisabled();
    itemHelp(how);
    if (!match) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Add all shown"))) {
            for (const int i : rows) {
                if (!in_play(engines_[i].name)) add.push_back(engines_[i].name);
            }
        }
        itemHelp(TR("Every engine the filter and the group show: a whole group at once."));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Suggest…"))) ImGui::OpenPopup("suggest");
        itemHelp(rated           ? trf("Opponents around the reference Elo of {} ({}).", seed->name, seed->elo)
                 : first.empty() ? std::string(TR("Add the engine to test first: it is the first participant."))
                                 : trf("The first participant, {}, has no reference Elo.", first));
        if (ImGui::BeginPopup("suggest")) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("Suggest"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(px(44));
            if (ui::inputInt("##picker_count", &lib.suggest)) lib.suggest = std::clamp(lib.suggest, 1, 500);
            ImGui::SameLine();
            ImGui::TextUnformatted(TR("opponents within ±"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(px(50));
            if (ui::inputInt("##picker_range", &lib.range)) lib.range = std::clamp(lib.range, 1, 3000);
            ImGui::SameLine();
            ImGui::TextUnformatted(rated ? trf("Elo of {} ({})", seed->name, seed->elo).c_str()
                                         : TR("Elo of the first"));
            ImGui::BeginDisabled(!rated);
            if (ImGui::Button(TR("Suggest##picker"))) {
                suggest();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            itemHelp(
                TR("Selects them in the library, the closest first, among the engines "
                   "that work: Enter adds them."));
            ImGui::EndPopup();
        }
    }
    if (!lib.message.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", lib.message.c_str());
        ImGui::PopTextWrapPos();
    }

    if (add.empty()) return false;
    // A match takes them as A, then B; the participants at the end. They are no longer offered: the selection goes.
    const bool changed = addParticipants(add);
    lib.selected.Clear();
    lib.message.clear();
    return changed;
}

}  // namespace fcg
