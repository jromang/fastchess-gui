#include "notice.hpp"

#include <imgui.h>

#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

Fix make(Fix::Kind kind, std::string label, std::string tooltip, std::string engine = {}, int value = 0,
         int index = -1) {
    Fix f;
    f.kind    = kind;
    f.label   = std::move(label);
    f.tooltip = std::move(tooltip);
    f.engine  = std::move(engine);
    f.value   = value;
    f.index   = index;
    return f;
}

}  // namespace

Fix openEngineFix(const std::string& engine) {
    return make(Fix::Kind::OpenEngine, TR("Open the engine"),
                trf("{} on the Engines page: its program and options", engine), engine);
}

Fix locateProgramFix(const std::string& engine) {
    return make(Fix::Kind::LocateProgram, TR("Locate…"), trf("Choose the program of {}", engine), engine);
}

Fix locateDirFix(const std::string& engine) {
    return make(Fix::Kind::LocateDir, TR("Locate…"), trf("Choose the working directory of {}", engine), engine);
}

Fix locateBookFix() { return make(Fix::Kind::LocateBook, TR("Locate…"), TR("Choose the opening book")); }

Fix locateTablebasesFix() {
    return make(Fix::Kind::LocateTablebases, TR("Locate…"),
                TR("Choose the tablebase folder of this tournament (the adjudication's, and the engines' when it "
                   "gives them its tablebases)"));
}

Fix lowerEngineThreadsFix(const std::string& engine, int threads) {
    return make(Fix::Kind::LowerEngineThreads, trf("Lower Threads to {}", threads),
                trf("Sets the Threads option of {} to {} in the engine library: every tournament plays it so, unless "
                    "its setup gives other Threads.",
                    engine, threads),
                engine, threads);
}

Fix lowerParticipantThreadsFix(const std::string& engine, int index, int threads) {
    return make(Fix::Kind::LowerParticipantThreads, trf("Lower Threads to {}", threads),
                trf("Sets the Threads of {} to {} in this tournament's participants.", engine, threads), engine,
                threads, index);
}

Fix lowerCommonThreadsFix(int threads) {
    return make(Fix::Kind::LowerCommonThreads, trf("Lower Threads to {}", threads),
                trf("Sets the Threads of this tournament's common settings to {}.", threads), {}, threads);
}

Fix lowerTablebasePiecesFix(int pieces) {
    return make(Fix::Kind::LowerTablebasePieces, trn("Lower to {} piece", "Lower to {} pieces", pieces, pieces),
                TR("The tablebase adjudication's Max pieces, in this tournament's Adjudication section."), {}, pieces);
}

Fix openLogFix() {
    return make(Fix::Kind::OpenLog, TR("Open the log"),
                TR("The Log page: fastchess' output and the program's messages"));
}

Fix openFolderFix(const std::string& path) {
    auto f = make(Fix::Kind::OpenFolder, TR("Open the folder"), path);
    f.path = path;
    return f;
}

Fix changeTournamentsFolderFix() {
    return make(Fix::Kind::ChangeTournamentsFolder, TR("Change the tournaments folder…"),
                TR("Where new tournaments are written, as in Settings. Those played so far stay where they are: Open "
                   "a folder… opens them."));
}

Fix copyToMachineFix(const std::string& engine, const std::string& machine_id, const std::string& machine_name) {
    auto f = make(Fix::Kind::CopyToMachine, trf("Copy {} to {}…", engine, machine_name),
                  trf("Settings > Machines: copies this computer's program of {} to {}, sets its path there and checks "
                      "it.",
                      engine, machine_name),
                  engine);
    f.path = machine_id;
    return f;
}

const char* noticeTopic(const Notice& notice) {
    if (notice.topic) return notice.topic;
    const char* topic = nullptr;
    for (const auto& f : notice.fixes) {
        switch (f.kind) {
            case Fix::Kind::OpenEngine:
            case Fix::Kind::LocateProgram:
            case Fix::Kind::LocateDir:
            case Fix::Kind::CopyToMachine:
                if (!topic) topic = guide::topics::TroubleEngine;
                break;
            case Fix::Kind::LowerEngineThreads:
            case Fix::Kind::LowerParticipantThreads:
            case Fix::Kind::LowerCommonThreads:
            case Fix::Kind::LocateTablebases:
            case Fix::Kind::LowerTablebasePieces:
                return guide::topics::TroubleGames;
            case Fix::Kind::LocateBook:
                if (!topic) topic = guide::topics::Openings;
                break;
            case Fix::Kind::ChangeTournamentsFolder:
                if (!topic) topic = guide::topics::Home;
                break;
            case Fix::Kind::OpenLog:
                if (!topic) topic = guide::topics::Troubleshooting;
                break;
            case Fix::Kind::OpenFolder:
                break;
        }
    }
    return topic;
}

const Fix* drawNotice(const Notice& notice, const char* id) {
    const bool ok    = notice.level == Notice::Level::Ok;
    const bool error = notice.level == Notice::Level::Error;
    ImGui::PushStyleColor(ImGuiCol_Text, ok ? ui::GOOD : error ? ui::BAD : ui::WARN);
    ImGui::TextUnformatted(ok ? "✓" : "!");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    const float text_x = ImGui::GetCursorPosX();
    if (error) ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
    ImGui::TextWrapped("%s", notice.text.c_str());
    if (error) ImGui::PopStyleColor();
    if (!notice.detail.empty() && ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
        ImGui::TextUnformatted(notice.detail.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    const char* topic = noticeTopic(notice);
    if (notice.fixes.empty() && !topic) return nullptr;

    // The fixes under the text, from where it starts, then the `?` of the guide's section that explains it.
    const Fix* clicked = nullptr;
    ImGui::PushID(id);
    ImGui::SetCursorPosX(text_x);
    for (std::size_t i = 0; i < notice.fixes.size(); ++i) {
        const auto& fix = notice.fixes[i];
        if (i > 0) ui::sameLineOrWrap(fix.label.c_str());
        if (ImGui::GetCursorPosX() < text_x) ImGui::SetCursorPosX(text_x);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(fix.label.c_str())) clicked = &fix;
        ImGui::PopID();
        if (!fix.tooltip.empty()) ImGui::SetItemTooltip("%s", fix.tooltip.c_str());
    }
    if (topic) {
        if (!notice.fixes.empty()) ImGui::SameLine();
        ui::guideLink("notice_guide", topic);
    }
    ImGui::PopID();
    return clicked;
}

bool drawNotices(const std::vector<Notice>& notices, const char* id, Fix& clicked) {
    bool any = false;
    ImGui::PushID(id);
    for (std::size_t i = 0; i < notices.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (const auto* fix = drawNotice(notices[i], "notice"); fix && !any) {
            clicked = *fix;
            any     = true;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    return any;
}

}  // namespace fcg
