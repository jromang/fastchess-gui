// For the rating list testers: the export of a tournament's games as the lists want them (Games tab).

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include <tour/files.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"
#include "zip.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool editText(const char* id, std::string& value, float width = -FLT_MIN) {
    char buffer[1024];
    const auto n = std::min(value.size(), sizeof(buffer) - 1);
    std::copy_n(value.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(id, buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

std::string today() {
    const auto now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char text[16];
    std::strftime(text, sizeof(text), "%Y-%m-%d", &tm);
    return text;
}

// The pieces of the tablebases the engines play with: those of an engine's SyzygyPath, else those of the
// adjudication; 0 without.
int egtbPieces(const tour::TournamentSpec& spec) {
    fastchess::config::TbAdjudication tb;
    for (const auto& p : spec.participants) {
        for (const auto& [name, value] : p.engine.options) {
            if (name == "SyzygyPath" && !value.empty() && value != "<empty>") tb.syzygy_dirs = value;
        }
        if (!tb.syzygy_dirs.empty()) break;
    }
    if (tb.syzygy_dirs.empty() && spec.conditions.tb_adjudication.enabled) {
        tb.syzygy_dirs = spec.conditions.tb_adjudication.syzygy_dirs;
    }
    if (tb.syzygy_dirs.empty()) return 0;
    tb.enabled    = true;
    tb.max_pieces = 0;
    return tablebasePieces(tb);
}

}  // namespace

void App::openSubmissionExport() {
    const auto* t = run_.tournament();
    if (!t) return;
    const auto& spec = t->spec();

    auto& e                    = submission_.e;
    std::tie(e.tester, e.site) = storage_.submissionTester();
    e.date                     = today();
    e.names.clear();
    for (const auto& p : spec.participants) {
        e.names[p.id] = tour::listName(p.engine.name, tour::optionValue(p.engine, "Threads", 1));
    }

    // The seed (a gauntlet's) or the first engine gives the hash; a gauntlet is named after its seed and the day it
    // started, as the testers do, any other tournament after itself.
    const auto results = t->results();
    int64_t first      = 0;
    for (const auto& r : results) {
        const int64_t start = r.finished_at - r.duration_ms;
        if (!r.voided && (first == 0 || start < first)) first = start;
    }
    if (!spec.participants.empty()) {
        const auto& seed = spec.participants.front();
        e.hash_mb        = tour::optionValue(seed.engine, "Hash", 0);
        e.event          = spec.format == tour::Format::Gauntlet && spec.seeds == 1 && first > 0
                               ? tour::submissionEvent(e.names[seed.id], first)
                               : spec.name;
    }
    e.book = spec.conditions.opening.file.empty() ? std::string()
                                                  : std::filesystem::path(spec.conditions.opening.file).stem().string();
    e.egtb = egtbPieces(spec);
    submission_.open = true;
}

void App::drawSubmissionExport() {
    const auto* t = run_.tournament();
    if (submission_.open) {
        ImGui::OpenPopup(TR("Export for a rating list###submission_export"));
        submission_.open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(px(720), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(TR("Export for a rating list###submission_export"), nullptr)) return;
    if (!t) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    auto& e = submission_.e;

    ImGui::TextWrapped("%s",
                       TR("The games as rating list testers send them: the file named after you, the date and the "
                          "conditions, the players as the lists write them, the rounds numbered again, the "
                          "voided games and the duplicates left out."));
    ImGui::Spacing();

    const float label = px(150);
    const auto field  = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(label);
    };
    field(TR("Tester"));
    editText("##submission_tester", e.tester, px(260));
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Site"));
    ImGui::SameLine();
    editText("##submission_site", e.site);
    field(TR("Date"));
    editText("##submission_date", e.date, px(120));
    field(TR("Event"));
    editText("##submission_event", e.event);
    field(TR("Hash (MB)"));
    ImGui::SetNextItemWidth(px(120));
    ImGui::InputInt("##submission_hash", &e.hash_mb, 0);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Book"));
    ImGui::SameLine();
    editText("##submission_book", e.book, px(220));
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("EGTB pieces"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(60));
    ImGui::InputInt("##submission_egtb", &e.egtb, 0);
    e.hash_mb = std::max(0, e.hash_mb);
    e.egtb    = std::clamp(e.egtb, 0, 7);

    // The names, as the lists write them: "<engine> 64-bit NCPU" by default, to correct when a list spells
    // the engine otherwise.
    ImGui::SeparatorText(TR("Players in the PGN"));
    const auto& spec   = t->spec();
    const float row_h  = ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().CellPadding.y;
    const float rows_h = row_h * (static_cast<float>(spec.participants.size()) + 1) + px(2);
    if (ImGui::BeginTable("##submission_names", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0, std::min(px(300), rows_h)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(TR("Engine"));
        ImGui::TableSetupColumn(TR("Threads"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(TR("Name on the list"));
        ImGui::TableHeadersRow();
        for (const auto& p : spec.participants) {
            ImGui::PushID(p.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(p.engine.name.c_str());
            if (!p.active) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", TR("(withdrawn)"));
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d", tour::optionValue(p.engine, "Threads", 1));
            ImGui::TableNextColumn();
            editText("##submission_name", e.names[p.id]);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Checkbox(TR("Leave out the games lost by a crash or a stall"), &e.leave_out_crashes);
    ImGui::Checkbox(TR("Also as a zip"), &submission_.zip);

    const auto stem = tour::submissionFileStem(e);
    ImGui::TextDisabled("%s", (stem + ".pgn").c_str());
    if (submission_.zip) ImGui::TextDisabled("%s", tour::submissionZipName(stem).c_str());

    ImGui::Spacing();
    const bool ready = !e.tester.empty() && !e.event.empty();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button(TR("Export…"))) {
        storage_.setSubmissionTester(e.tester, e.site);
        dialogs_.saveFile(Dialogs::Purpose::ExportSubmission, {{"PGN", "pgn"}},
                          (t->dir().parent_path() / (tour::fileNameOf(stem) + ".pgn")).string());
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    if (!ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", TR("Give the tester's name and the event."));
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::writeSubmissionExport(const std::string& path) {
    const auto* t = run_.tournament();
    if (!t) return;
    const std::filesystem::path file(path);
    std::error_code ec;
    if (std::filesystem::equivalent(file, t->pgnPath(), ec) ||
        std::filesystem::equivalent(file, t->chessbasePgnPath(), ec)) {
        Console::instance().add(trf("{} is the tournament's own PGN file: choose another one.", path), true);
        return;
    }

    // A PGN named x.zip would be overwritten by its own zip.
    const auto zip = tour::submissionZipPath(file);
    if (submission_.zip && zip.empty()) {
        Console::instance().add(trf("{} would be both the PGN file and its zip: choose a name ending in .pgn.", path),
                                true);
        return;
    }

    const auto out = tour::submissionGames(t->pgnPath(), t->results(), t->spec(), submission_.e);
    {
        std::ofstream pgn(file, std::ios::binary | std::ios::trunc);
        pgn << out.pgn;
        pgn.close();
        if (!pgn) {
            Console::instance().add(trf("Cannot write {}", path), true);
            return;
        }
    }

    std::string message = trn("{} game exported to {}", "{} games exported to {}", out.games, out.games, path);
    const auto left_out = out.voided + out.duplicates + out.crashes;
    if (left_out > 0) {
        message +=
            " " + trf("(left out: {} voided, {} duplicates, {} crashes)", out.voided, out.duplicates, out.crashes);
    }
    if (out.unreadable > 0) {
        message +=
            " " + trn("{} game could not be read.", "{} games could not be read.", out.unreadable, out.unreadable);
    }
    Console::instance().add(message, out.unreadable > 0);

    if (submission_.zip) {
        // The zip next to the PGN, named as the testers' scripts name it, holding the PGN under its own name.
        std::string error;
        if (writeZip(zip, file.filename().string(), out.pgn, &error)) {
            Console::instance().add(trf("Zip written to {}", zip.string()));
        } else {
            Console::instance().add(error, true);
        }
    }
}

void App::drawSubmissionChecklist() {
    const auto* t = run_.tournament();
    if (!t || !t->finished()) return;
    if (submission_facts_for_ != t->version()) {
        submission_facts_     = tour::submissionFacts(t->spec(), t->results(), t->totalGames());
        submission_facts_for_ = t->version();
        // The tablebase directory is read here, not every frame: its files are listed.
        const auto it         = std::find_if(submission_facts_.engines.begin(), submission_facts_.engines.end(),
                                             [](const auto& e) { return !e.syzygy.empty(); });
        submission_tb_pieces_ = 0;
        if (it != submission_facts_.engines.end()) {
            fastchess::config::TbAdjudication tb;
            tb.enabled            = true;
            tb.syzygy_dirs        = it->syzygy;
            submission_tb_pieces_ = tablebasePieces(tb);
        }
    }
    const auto& f    = submission_facts_;
    const auto& spec = t->spec();
    const auto name  = [&](int id) {
        const auto* p = spec.participant(id);
        return p ? p->engine.name : std::string("?");
    };

    enum class Status { Ok, Info, Warn, Fail };
    // A line: its mark, what is checked, and what was found.
    const auto line = [&](Status status, const char* label, const std::string& detail) {
        static constexpr const char* MARKS[] = {"✓", "i", "!", "×"};
        const ImU32 colors[]                 = {ui::GOOD, ui::MUTED, ui::WARN, ui::BAD};
        const float x                        = ImGui::GetCursorPosX();
        ImGui::PushStyleColor(ImGuiCol_Text, colors[static_cast<int>(status)]);
        ImGui::TextUnformatted(MARKS[static_cast<int>(status)]);
        ImGui::PopStyleColor();
        ImGui::SameLine(x + px(20));
        ImGui::TextUnformatted(label);
        if (!detail.empty()) {
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", detail.c_str());
            ImGui::PopTextWrapPos();
        }
    };
    // Buttons that open games in the Games tab, by number, the first ones of a list.
    const auto links = [&](const char* id, const std::vector<uint64_t>& numbers) {
        constexpr std::size_t SHOWN = 12;
        ImGui::PushID(id);
        ImGui::Indent(px(20));
        for (std::size_t i = 0; i < numbers.size() && i < SHOWN; ++i) {
            if (i > 0) ImGui::SameLine();
            const uint64_t game_id = numbers[i] + 1;
            if (ImGui::SmallButton(fmt::format("#{}##{}", game_id, i).c_str())) {
                const auto it = std::find_if(games_.begin(), games_.end(),
                                             [&](const GameRecord& g) { return g.game_id == game_id; });
                if (it != games_.end()) {
                    selectGame(static_cast<int>(it - games_.begin()));
                    tab_request_ = Tab::Games;
                }
            }
        }
        if (numbers.size() > SHOWN) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", trf("and {} more", numbers.size() - SHOWN).c_str());
        }
        ImGui::Unindent(px(20));
        ImGui::PopID();
    };

    ImGui::Spacing();
    ui::beginCard("submission_card");
    ImGui::TextUnformatted(TR("Submission checklist"));
    ui::help(
        TR("What a tester checks before sending the games to a rating list. Export for a rating list… in the Games tab "
           "leaves the voided games and the duplicates out."));

    // Games and colours.
    const uint64_t planned = f.planned.value_or(f.played);
    line(f.played >= planned ? Status::Ok : Status::Fail, TR("All games played"), trf("{} / {}", f.played, planned));
    if (f.unpaired.empty()) {
        line(Status::Ok, TR("Every pair with both colours"), "");
    } else {
        std::string pairs;
        for (std::size_t i = 0; i < f.unpaired.size() && i < 4; ++i) {
            pairs += (i ? ", " : "") + name(f.unpaired[i].first) + " – " + name(f.unpaired[i].second);
        }
        if (f.unpaired.size() > 4) pairs += ", …";
        line(Status::Fail, TR("Every pair with both colours"),
             trn("{} pair without its game with colours reversed: {}",
                 "{} pairs without their game with colours reversed: {}", f.unpaired.size(), f.unpaired.size(), pairs));
    }
    std::string duplicates =
        f.duplicates == 0 ? std::string(TR("none"))
                          : trn("{} game recorded twice: the export keeps the first",
                                "{} games recorded twice: the export keeps the first", f.duplicates, f.duplicates);
    if (f.voided > 0) {
        duplicates += " · " + trn("{} voided game, left out", "{} voided games, left out", f.voided, f.voided);
    }
    line(f.duplicates == 0 ? Status::Ok : Status::Warn, TR("No duplicate games"), duplicates);

    // How games ended badly, with the games.
    if (f.crashes.empty() && f.timeouts.empty() && f.illegal.empty()) {
        line(Status::Ok, TR("No crashes, time losses or illegal moves"), "");
    } else {
        line(Status::Warn, TR("No crashes, time losses or illegal moves"),
             TR("to mention in the post, or to test the engine again:"));
        if (!f.crashes.empty()) {
            ImGui::Indent(px(20));
            ImGui::TextUnformatted(
                trn("{} crash or stall", "{} crashes or stalls", f.crashes.size(), f.crashes.size()).c_str());
            ImGui::Unindent(px(20));
            links("crashes", f.crashes);
        }
        if (!f.timeouts.empty()) {
            ImGui::Indent(px(20));
            ImGui::TextUnformatted(
                trn("{} loss on time", "{} losses on time", f.timeouts.size(), f.timeouts.size()).c_str());
            ImGui::Unindent(px(20));
            links("timeouts", f.timeouts);
        }
        if (!f.illegal.empty()) {
            ImGui::Indent(px(20));
            ImGui::TextUnformatted(
                trn("{} illegal move", "{} illegal moves", f.illegal.size(), f.illegal.size()).c_str());
            ImGui::Unindent(px(20));
            links("illegal", f.illegal);
        }
    }

    // The conditions. The hash a list wants depends on the list and the threads: the values are shown, not judged.
    std::string hash;
    const bool same_hash = std::all_of(f.engines.begin(), f.engines.end(), [&](const auto& e) {
        return e.hash == f.engines.front().hash && e.threads == f.engines.front().threads;
    });
    const auto hash_of   = [](const tour::SubmissionFacts::Engine& e) {
        const auto mb = e.hash > 0 ? fmt::format("{} MB", e.hash) : std::string(TR("its default"));
        return trn("{}, {} thread", "{}, {} threads", e.threads, mb, e.threads);
    };
    if (!f.engines.empty() && same_hash) {
        hash = trf("{} for every engine", hash_of(f.engines.front()));
    } else {
        for (const auto& e : f.engines) hash += (hash.empty() ? "" : " · ") + e.name + ": " + hash_of(e);
    }
    line(Status::Info, TR("Hash"), hash);

    std::string ponder;
    for (const auto& e : f.engines) {
        if (e.ponder) ponder += (ponder.empty() ? "" : ", ") + e.name;
    }
    line(ponder.empty() ? Status::Ok : Status::Fail, TR("Ponder off"),
         ponder.empty() ? std::string() : trf("Ponder is on for {}", ponder));

    line(f.book.empty() ? Status::Warn : Status::Ok, TR("Opening book"),
         f.book.empty() ? std::string(TR("none: every game starts from the initial position")) : f.book);

    std::string tablebases;
    std::size_t with = 0;
    for (const auto& e : f.engines) with += e.syzygy.empty() ? 0 : 1;
    if (with == 0) {
        tablebases = TR("no engine has a SyzygyPath");
    } else {
        const int pieces = submission_tb_pieces_;
        tablebases       = with == f.engines.size()
                               ? trf("{}-man Syzygy for every engine", pieces)
                               : trf("{}-man Syzygy for {} of {} engines", pieces, with, f.engines.size());
    }
    if (!f.adjudication_tb.empty()) tablebases += " · " + std::string(TR("adjudication with tablebases"));
    line(Status::Info, TR("Tablebases"), tablebases);

    ui::endCard();
}

}  // namespace fcg
