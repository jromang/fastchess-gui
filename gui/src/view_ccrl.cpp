// For the CCRL testers: the export of a tournament's games as the lists want them (Games tab).

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

void App::openCcrlExport() {
    const auto* t = run_.tournament();
    if (!t) return;
    const auto& spec = t->spec();

    auto& e                    = ccrl_.e;
    std::tie(e.tester, e.site) = storage_.ccrlTester();
    e.date                     = today();
    e.names.clear();
    for (const auto& p : spec.participants) {
        e.names[p.id] = tour::ccrlName(p.engine.name, tour::optionValue(p.engine, "Threads", 1));
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
                               ? tour::ccrlEvent(e.names[seed.id], first)
                               : spec.name;
    }
    e.book = spec.conditions.opening.file.empty() ? std::string()
                                                  : std::filesystem::path(spec.conditions.opening.file).stem().string();
    e.egtb = egtbPieces(spec);
    ccrl_.open = true;
}

void App::drawCcrlExport() {
    const auto* t = run_.tournament();
    if (ccrl_.open) {
        ImGui::OpenPopup(TR("Export for CCRL###ccrl_export"));
        ccrl_.open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(px(720), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(TR("Export for CCRL###ccrl_export"), nullptr)) return;
    if (!t) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    auto& e = ccrl_.e;

    ImGui::TextWrapped("%s", TR("The games as CCRL testers send them: the file named after you, the date and the "
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
    editText("##ccrl_tester", e.tester, px(260));
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Site"));
    ImGui::SameLine();
    editText("##ccrl_site", e.site);
    field(TR("Date"));
    editText("##ccrl_date", e.date, px(120));
    field(TR("Event"));
    editText("##ccrl_event", e.event);
    field(TR("Hash (MB)"));
    ImGui::SetNextItemWidth(px(120));
    ImGui::InputInt("##ccrl_hash", &e.hash_mb, 0);
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Book"));
    ImGui::SameLine();
    editText("##ccrl_book", e.book, px(220));
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("EGTB pieces"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(60));
    ImGui::InputInt("##ccrl_egtb", &e.egtb, 0);
    e.hash_mb = std::max(0, e.hash_mb);
    e.egtb    = std::clamp(e.egtb, 0, 7);

    // The names, as the CCRL lists write them: "<engine> 64-bit NCPU" by default, to correct when a list spells
    // the engine otherwise.
    ImGui::SeparatorText(TR("Players in the PGN"));
    const auto& spec   = t->spec();
    const float row_h  = ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().CellPadding.y;
    const float rows_h = row_h * (static_cast<float>(spec.participants.size()) + 1) + px(2);
    if (ImGui::BeginTable("##ccrl_names", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0, std::min(px(300), rows_h)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(TR("Engine"));
        ImGui::TableSetupColumn(TR("Threads"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(TR("Name for CCRL"));
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
            editText("##ccrl_name", e.names[p.id]);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Checkbox(TR("Leave out the games lost by a crash or a stall"), &e.leave_out_crashes);
    ImGui::Checkbox(TR("Also as a zip"), &ccrl_.zip);

    const auto stem = tour::ccrlFileStem(e);
    ImGui::TextDisabled("%s", (stem + ".pgn").c_str());
    if (ccrl_.zip) ImGui::TextDisabled("%s", tour::ccrlZipName(stem).c_str());

    ImGui::Spacing();
    const bool ready = !e.tester.empty() && !e.event.empty();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button(TR("Export…"))) {
        storage_.setCcrlTester(e.tester, e.site);
        dialogs_.saveFile(Dialogs::Purpose::ExportCcrl, {{"PGN", "pgn"}},
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

void App::writeCcrlExport(const std::string& path) {
    const auto* t = run_.tournament();
    if (!t) return;
    const std::filesystem::path file(path);
    std::error_code ec;
    if (std::filesystem::equivalent(file, t->pgnPath(), ec) ||
        std::filesystem::equivalent(file, t->chessbasePgnPath(), ec)) {
        Console::instance().add(trf("{} is the tournament's own PGN file: choose another one.", path), true);
        return;
    }

    const auto out = tour::ccrlGames(t->pgnPath(), t->results(), t->spec(), ccrl_.e);
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

    if (ccrl_.zip) {
        // The zip next to the PGN, named as the testers' scripts name it, holding the PGN under its own name.
        const auto zip = file.parent_path() / tour::ccrlZipName(file.stem().string());
        std::string error;
        if (writeZip(zip, file.filename().string(), out.pgn, &error)) {
            Console::instance().add(trf("Zip written to {}", zip.string()));
        } else {
            Console::instance().add(error, true);
        }
    }
}

}  // namespace fcg
