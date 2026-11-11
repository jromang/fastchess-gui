// The schedule of a round robin or a gauntlet, and the changes that can be made while it runs.

#include <algorithm>
#include <map>

#include <imgui.h>

#include <tour/by_opening.hpp>

#include "app.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

const char* statusText(tour::GameStatus status) {
    switch (status) {
        case tour::GameStatus::Done:
            return "✓";
        case tour::GameStatus::Running:
            return "●";
        case tour::GameStatus::Dropped:
            return "—";
        default:
            return "○";
    }
}

ImU32 statusColor(tour::GameStatus status) {
    switch (status) {
        case tour::GameStatus::Done:
            return ui::GOOD;
        case tour::GameStatus::Running:
            return ui::ACCENT;
        default:
            return ui::MUTED;
    }
}

}  // namespace

void App::applyTournamentEdit(const tour::Edit& edit) {
    const auto error = run_.applyEdit(edit);
    edit_error_      = !error.empty();
    if (edit_error_) {
        edit_message_ = error;
        return;
    }
    edit_message_ = TR("Done: the tournament goes on with the change.");
    Console::instance().add(TR("The tournament was changed."));
    refreshAfterEdit();
}

void App::undoTournamentEdit() {
    const auto error = run_.undoEdit();
    edit_error_      = !error.empty();
    edit_message_    = edit_error_ ? error : TR("The change was undone.");
    if (edit_error_) return;
    Console::instance().add(TR("The last change of the tournament was undone."));
    refreshAfterEdit();
}

void App::refreshAfterEdit() {
    // Voided games leave the views; everything is shown again from the tournament's results.
    const auto* t = run_.tournament();
    if (auto* obs = run_.observer(); obs && t) {
        games_    = obs->records(t->results());
        reasons_  = obs->reasons();
        openings_ = obs->openings();
    }
    filtered_for_      = ~std::size_t(0);
    table_for_         = ~std::size_t(0);
    selected_game_     = -1;
    tournaments_dirty_ = true;
    if (!run_.running() && t) {
        const auto summary = tour::summarize(t->dir());
        if (summary && !summary->finished) {
            unfinished_ = summary;
        } else if (unfinished_ && unfinished_->dir == t->dir()) {
            unfinished_.reset();
        }
    }
}

void App::drawSchedule() {
    const auto* t = run_.tournament();
    if (!t) return;

    const float panel = std::max(px(340), ImGui::GetContentRegionAvail().x * 0.34f);
    ImGui::BeginChild("##schedule_list", ImVec2(ImGui::GetContentRegionAvail().x - panel - px(16), 0));

    const auto rows  = t->scheduleRows();
    const auto* obs  = run_.observer();
    const auto& spec = t->spec();
    const auto name  = [&](int id) -> std::string {
        const auto* p = spec.participant(id);
        return p ? p->engine.name : "?";
    };

    // The openings the encounters started with, named once; with one opening per round, a round's for the encounters
    // of it still to play.
    if (schedule_openings_for_ != t) {
        schedule_openings_for_ = t;
        schedule_openings_.clear();
    }
    const bool frc = spec.conditions.variant == fastchess::VariantType::FRC;
    for (const auto& [encounter, opening] : t->encounterOpenings()) {
        if (schedule_openings_.count(encounter)) continue;
        const auto c      = tour::classify(opening, frc);
        std::string label = c.eco.empty() ? "" : c.eco + " " + c.name;
        if (label.empty()) {
            for (const auto& m : c.moves) label += (label.empty() ? "" : " ") + m;
        }
        schedule_openings_[encounter] = label.empty() ? TR("initial position") : label;
    }
    std::map<std::pair<uint32_t, uint32_t>, std::string> round_opening;
    if (spec.opening_per_round) {
        for (const auto& row : rows) {
            if (const auto it = schedule_openings_.find(row.encounter.index); it != schedule_openings_.end()) {
                round_opening.emplace(std::pair{row.encounter.cycle, row.encounter.round}, it->second);
            }
        }
    }

    // Results of the games, by game number, to show them in the list.
    std::map<uint64_t, std::string> result_of;
    for (const auto& r : t->results()) {
        if (!r.voided) result_of[r.number()] = r.result == "1/2-1/2" ? "½-½" : r.result;
    }

    // Counts by status for the header.
    std::size_t done = 0, running = 0, pending = 0;
    for (const auto& row : rows) {
        for (const auto status : row.status) {
            done += status == tour::GameStatus::Done;
            running += status == tour::GameStatus::Running;
            pending += status == tour::GameStatus::Pending;
        }
    }
    ImGui::TextDisabled("%s", trf("{} played · {} running · {} to play", done, running, pending).c_str());

    // Grouped by cycle; a Swiss or a knockout, by round.
    const bool by_round = tour::pairedAsItGoes(spec.format);
    const auto group_of = [&](const tour::ScheduleRow& r) { return by_round ? r.encounter.round : r.encounter.cycle; };
    uint32_t cycle      = 0;
    bool open           = false;
    // A cycle can come in two blocks: an engine added during cycle 2 catches up on cycles 1 and 2 after it.
    std::map<uint32_t, int> blocks;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        if (i == 0 || group_of(row) != group_of(rows[i - 1])) {
            if (open) ImGui::TreePop();
            cycle = group_of(row);

            // The block's progress in its header; the one being played is open.
            std::size_t c_done = 0, c_total = 0;
            bool c_running = false;
            for (std::size_t j = i; j < rows.size() && group_of(rows[j]) == cycle; ++j) {
                for (const auto status : rows[j].status) {
                    c_total += status != tour::GameStatus::Dropped;
                    c_done += status == tour::GameStatus::Done;
                    c_running |= status == tour::GameStatus::Running;
                }
            }
            const auto label = (by_round ? trf("Round {} · {} / {} games", cycle, c_done, c_total)
                                         : trf("Cycle {} · {} / {} games", cycle, c_done, c_total)) +
                               fmt::format("###cycle{}-{}", cycle, blocks[cycle]++);
            ImGui::SetNextItemOpen(c_running || (c_done > 0 && c_done < c_total), ImGuiCond_Appearing);
            open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
        }
        if (!open) continue;

        ImGui::PushID(static_cast<int>(i));
        ImGui::TextDisabled("%s",
                            (by_round ? trf("G{}", row.encounter.cycle) : trf("R{}", row.encounter.round)).c_str());
        ImGui::SameLine(px(60));
        ImGui::TextUnformatted(fmt::format("{} – {}", name(row.encounter.first), name(row.encounter.second)).c_str());
        for (int g = 0; g < 2; ++g) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0 + px(380) + g * px(90));
            const auto n = uint64_t{row.encounter.index} * 2 + g;
            ImGui::PushStyleColor(ImGuiCol_Text, statusColor(row.status[g]));
            const auto it = result_of.find(n);
            ImGui::TextUnformatted(it != result_of.end()
                                       ? fmt::format("{} {}", statusText(row.status[g]), it->second).c_str()
                                       : statusText(row.status[g]));
            ImGui::PopStyleColor();
            if (row.status[g] == tour::GameStatus::Done &&
                ImGui::BeginPopupContextItem(fmt::format("g{}", g).c_str())) {
                if (ImGui::MenuItem(TR("Play this game again"))) {
                    tour::Edit e;
                    e.kind  = tour::Edit::Kind::VoidGames;
                    e.games = {{row.encounter.index, g}};
                    e.why   = TR("replay requested");
                    applyTournamentEdit(e);
                }
                if (ImGui::MenuItem(TR("Void this game (no replay)"))) {
                    tour::Edit e;
                    e.kind   = tour::Edit::Kind::VoidGames;
                    e.games  = {{row.encounter.index, g}};
                    e.replay = false;
                    e.why    = TR("voided");
                    applyTournamentEdit(e);
                }
                ImGui::EndPopup();
            }
            if (row.status[g] == tour::GameStatus::Done && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", TR("Right click: play again or void this game."));
            }
        }
        const std::string* opening = nullptr;
        if (const auto it = schedule_openings_.find(row.encounter.index); it != schedule_openings_.end()) {
            opening = &it->second;
        } else if (const auto r = round_opening.find({row.encounter.cycle, row.encounter.round});
                   r != round_opening.end()) {
            opening = &r->second;
        }
        if (opening) {
            ImGui::SameLine(px(380) + 2 * px(90) + px(10));
            ImGui::TextDisabled("%s", opening->c_str());
        }
        ImGui::PopID();
    }
    if (open) ImGui::TreePop();
    (void)obs;
    ImGui::EndChild();

    ImGui::SameLine(0, px(16));
    ImGui::BeginChild("##edit_panel", ImVec2(0, 0));
    drawEditPanel();
    ImGui::EndChild();
}

void App::drawEditPanel() {
    const auto* t = run_.tournament();
    if (!t) return;
    const auto& spec = t->spec();

    ui::beginCard("edit_card");
    ui::pageTitle(TR("Change the tournament"));
    ImGui::TextDisabled("%s", TR("Without stopping it: running games go on, nothing played is lost."));
    ImGui::Spacing();

    const char* kinds[] = {TR("Add an engine"), TR("Replace an engine"), TR("Withdraw an engine"), TR("Cycles"),
                           TR("Play games again")};
    // The participants and rounds of a Swiss or a knockout follow from the results: only games can be replayed.
    if (tour::pairedAsItGoes(spec.format)) {
        edit_kind_ = static_cast<int>(tour::Edit::Kind::VoidGames);
        ImGui::TextUnformatted(kinds[edit_kind_]);
    } else {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##edit_kind", &edit_kind_, kinds, IM_ARRAYSIZE(kinds))) edit_message_.clear();
    }

    tour::Edit edit;
    edit.kind  = static_cast<tour::Edit::Kind>(edit_kind_);
    bool ready = true;

    // An engine of the list, as a participant.
    const auto engineChoice = [&](const char* label) {
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##edit_engine", edit_engine_.empty() ? TR("(choose)") : edit_engine_.c_str())) {
            for (const auto& e : engines_) {
                if (ImGui::Selectable(e.name.c_str(), e.name == edit_engine_)) edit_engine_ = e.name;
            }
            ImGui::EndCombo();
        }
        const auto it = std::find_if(engines_.begin(), engines_.end(),
                                     [&](const EngineEntry& e) { return e.name == edit_engine_; });
        if (it == engines_.end()) {
            ready = false;
            return;
        }
        edit.engine = participantConfig(*it, spec);
        edit.elo    = it->elo;
    };
    // An active participant.
    const auto participantChoice = [&](const char* label) {
        ImGui::TextUnformatted(label);
        const auto* current = spec.participant(edit_participant_);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##edit_participant",
                              current && current->active ? current->engine.name.c_str() : TR("(choose)"))) {
            for (const auto& p : spec.participants) {
                if (!p.active) continue;
                if (ImGui::Selectable(p.engine.name.c_str(), p.id == edit_participant_)) edit_participant_ = p.id;
            }
            ImGui::EndCombo();
        }
        if (!current || !current->active) ready = false;
        edit.participant = edit_participant_;
    };

    switch (edit.kind) {
        case tour::Edit::Kind::AddParticipant:
            engineChoice(TR("Engine to add"));
            ImGui::Checkbox(TR("Catch up the cycles already started"), &edit_catch_up_);
            ui::help(TR("Otherwise it joins from the next cycle and plays fewer games than the others."));
            edit.catch_up = edit_catch_up_;
            break;
        case tour::Edit::Kind::ReplaceParticipant:
            participantChoice(TR("Engine to replace"));
            engineChoice(TR("Replaced by"));
            ImGui::TextDisabled("%s", TR("Its games stop counting and are played again by the new engine."));
            break;
        case tour::Edit::Kind::Withdraw:
            participantChoice(TR("Engine to withdraw"));
            ImGui::Checkbox(TR("Its games played so far stop counting"), &edit_void_played_);
            edit.void_played = edit_void_played_;
            break;
        case tour::Edit::Kind::SetCycles:
            if (edit_cycles_ <= 0) edit_cycles_ = spec.cycles;
            ImGui::TextUnformatted(TR("Number of cycles"));
            ImGui::SetNextItemWidth(px(120));
            if (ImGui::InputInt("##edit_cycles", &edit_cycles_)) edit_cycles_ = std::clamp(edit_cycles_, 1, 10000);
            ImGui::TextDisabled("%s", trf("Now {}. Only cycles not started can be removed.", spec.cycles).c_str());
            edit.cycles = edit_cycles_;
            break;
        case tour::Edit::Kind::VoidGames: {
            ImGui::RadioButton(TR("Losses on time and crashes"), &edit_replay_filter_, 0);
            ImGui::RadioButton(TR("Every game of an engine"), &edit_replay_filter_, 1);
            if (edit_replay_filter_ == 1) participantChoice(TR("Engine"));
            for (const auto& r : t->results()) {
                if (r.voided) continue;
                using T = fastchess::MatchTermination;
                const bool pick =
                    edit_replay_filter_ == 0
                        ? r.termination == T::TIMEOUT || r.termination == T::DISCONNECT || r.termination == T::STALL
                        : r.white == edit_participant_ || r.black == edit_participant_;
                if (pick) edit.games.emplace_back(r.encounter, r.game);
            }
            edit.why = edit_replay_filter_ == 0 ? TR("loss on time or crash") : TR("replay requested");
            ImGui::TextDisabled("%s", trn("{} game", "{} games", edit.games.size(), edit.games.size()).c_str());
            if (edit.games.empty()) ready = false;
            break;
        }
    }

    // What it would change, computed by making the change on a copy.
    ImGui::Spacing();
    if (ready) {
        const auto impact = t->preview(edit);
        if (!impact.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
            ImGui::TextWrapped("%s", impact.error.c_str());
            ImGui::PopStyleColor();
            ready = false;
        } else {
            ui::beginCard("impact_card");
            ImGui::TextDisabled("%s", TR("Impact"));
            const auto delta = static_cast<int64_t>(impact.games_after) - static_cast<int64_t>(impact.games_before);
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
            ImGui::TextUnformatted(
                delta == 0 ? TR("Same number of games")
                           : trf("{:+} games ({} → {})", delta, impact.games_before, impact.games_after).c_str());
            ImGui::PopFont();
            if (impact.replayed)
                ImGui::TextUnformatted(trn("{} played game is played again", "{} played games are played again",
                                           impact.replayed, impact.replayed)
                                           .c_str());
            if (impact.voided)
                ImGui::TextUnformatted(
                    trn("{} played game stops counting", "{} played games stop counting", impact.voided, impact.voided)
                        .c_str());
            if (games_per_hour_ > 0 && delta + static_cast<int64_t>(impact.replayed) != 0) {
                const double hours = (delta + static_cast<double>(impact.replayed)) / games_per_hour_;
                ImGui::TextDisabled(
                    "%s",
                    trf("About {} {}", hours >= 0 ? "+" : "−", ui::formatDuration(std::abs(hours) * 3600.0)).c_str());
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ui::GOOD);
            ImGui::TextWrapped("%s",
                               TR("✓ No played game is lost: voided games stay in the PGN files and the journal."));
            ImGui::PopStyleColor();
            ui::endCard();
        }
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button(TR("Apply"), ImVec2(-FLT_MIN, 0))) applyTournamentEdit(edit);
    ImGui::EndDisabled();

    if (!edit_message_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, edit_error_ ? ui::BAD : ui::GOOD);
        ImGui::TextWrapped("%s", edit_message_.c_str());
        ImGui::PopStyleColor();
    }
    if (t->canUndo()) {
        if (ImGui::Button(TR("Undo the last change"))) undoTournamentEdit();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("until the next game starts"));
    }
    if (!run_.running()) {
        ImGui::TextDisabled("%s", TR("The tournament is not running: resume it to play the new games."));
    }
    ui::endCard();
}

}  // namespace fcg
