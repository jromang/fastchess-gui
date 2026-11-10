// Broadcasting the open tournament: live files, the TLCS server and the web page.

#include <algorithm>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <engine/uci_engine.hpp>

#include "app.hpp"
#include "broadcaster.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "net.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

}  // namespace

void App::updateBroadcast() {
    BroadcastSettings settings;
    settings.files          = setup_.broadcast_files;
    settings.dir            = setup_.broadcast_dir;
    settings.tlcs           = setup_.broadcast_tlcs;
    settings.tlcs_port      = setup_.tlcs_port;
    settings.web            = setup_.broadcast_web;
    settings.web_port       = setup_.web_port;
    settings.lichess        = setup_.broadcast_lichess;
    settings.lichess_token  = lichess_token_;
    settings.lichess_round  = setup_.lichess_round;
    settings.lichess_listed = setup_.lichess_listed;
    settings.tmp_dir        = storage_.root() / "tmp";
    for (const auto& error : broadcaster_.configure(settings)) Console::instance().add(error, true);

    const auto* t   = run_.tournament();
    const auto* obs = run_.observer();
    if (t && obs) broadcaster_.update(*t, *obs, anchor_, setup_.broadcast_follow == 1 ? live_slot_ : -1);
}

void App::drawBroadcast() {
    bool changed = false;

    const auto toggle = [&](const char* id, bool& value) { changed |= ImGui::Checkbox(id, &value); };

    // Files.
    ui::beginCard("bc_files");
    ui::pageTitle(TR("Files"));
    ImGui::TextDisabled("%s", TR("Live PGN, standings and crosstable, to publish anywhere (FTP, rsync, a website)."));
    toggle(TR("Write the live files"), setup_.broadcast_files);
    ImGui::TextUnformatted(TR("live.pgn: the games in progress · standings.html · tournament.json (for scripts)"));
    ImGui::TextUnformatted(TR("Folder (empty: \"live\" in the tournament folder)"));
    {
        char buffer[1024];
        const auto n = std::min(setup_.broadcast_dir.size(), sizeof(buffer) - 1);
        std::copy_n(setup_.broadcast_dir.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##bc_dir", buffer, sizeof(buffer))) {
            setup_.broadcast_dir = buffer;
            changed              = true;
        }
    }
    if (setup_.broadcast_files && !broadcaster_.filesStatus().empty())
        ImGui::TextDisabled("%s", broadcaster_.filesStatus().c_str());
    ui::endCard();

    ImGui::Spacing();

    // TLCS.
    ui::beginCard("bc_tlcs");
    ui::pageTitle(TR("TLCS server"));
    ImGui::TextDisabled("%s", TR("For TLCV, node-tlcv (as used by ccrl.live) and Livius: one game at a time, with the "
                                 "engines' searches, and the crosstable."));
    toggle(TR("Run the TLCS server"), setup_.broadcast_tlcs);
    ImGui::SetNextItemWidth(px(120));
    ImGui::BeginDisabled(broadcaster_.tlcs().running());
    if (ImGui::InputInt(TR("UDP port"), &setup_.tlcs_port, 0, 0)) {
        setup_.tlcs_port = std::clamp(setup_.tlcs_port, 1, 65535);
        changed          = true;
    }
    ImGui::EndDisabled();
    ImGui::TextUnformatted(TR("Game shown:"));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("automatic"), &setup_.broadcast_follow, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("the one chosen in the Live tab"), &setup_.broadcast_follow, 1);
    if (!broadcaster_.tlcsError().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", broadcaster_.tlcsError().c_str());
        ImGui::PopStyleColor();
    } else if (broadcaster_.tlcs().running()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::GOOD);
        ImGui::TextUnformatted(trf("● On air · {} clients", broadcaster_.tlcs().clients()).c_str());
        ImGui::PopStyleColor();
    }
    ui::endCard();

    ImGui::Spacing();

    // Web page.
    ui::beginCard("bc_web");
    ui::pageTitle(TR("Web page"));
    ImGui::TextDisabled("%s", TR("Every game in progress, the standings and the crosstable, read-only, refreshed "
                                 "every second."));
    toggle(TR("Serve the web page"), setup_.broadcast_web);
    ImGui::SetNextItemWidth(px(120));
    ImGui::BeginDisabled(broadcaster_.web().running());
    if (ImGui::InputInt(TR("TCP port"), &setup_.web_port, 0, 0)) {
        setup_.web_port = std::clamp(setup_.web_port, 1, 65535);
        changed         = true;
    }
    ImGui::EndDisabled();
    if (!broadcaster_.webError().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", broadcaster_.webError().c_str());
        ImGui::PopStyleColor();
    } else if (broadcaster_.web().running()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::GOOD);
        ImGui::TextUnformatted(trf("● On air · {} viewers", broadcaster_.web().viewers()).c_str());
        ImGui::PopStyleColor();
        for (const auto& address : net::localAddresses()) {
            const auto url = fmt::format("http://{}:{}", address, setup_.web_port);
            if (ImGui::SmallButton(url.c_str())) SDL_OpenURL(url.c_str());
        }
        ImGui::TextDisabled("%s", TR("Reachable from the local network; open the port in your router to share it "
                                     "further."));
    }
    ui::endCard();

    ImGui::Spacing();
    drawLichess(changed);

    if (changed) saveSetupSoon();
}

void App::lichessTokenField() {
    ImGui::TextUnformatted(TR("API token (with the study:write scope)"));
    char buffer[512];
    const auto n = std::min(lichess_token_.size(), sizeof(buffer) - 1);
    std::copy_n(lichess_token_.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##lichess_token", buffer, sizeof(buffer), ImGuiInputTextFlags_Password)) {
        lichess_token_ = buffer;
        storage_.setLichessToken(lichess_token_);
    }
    if (ImGui::SmallButton(TR("Create a token on lichess…"))) {
        SDL_OpenURL("https://lichess.org/account/oauth/token/create?scopes[]=study:write&description=fastchess-gui");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("Kept in the profile only, never in a tournament's files or the log."));
}

void App::drawLichess(bool& changed) {
    ui::beginCard("bc_lichess");
    ui::pageTitle("lichess");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s",
                        TR("A lichess broadcast: its board, evaluation bar and chat, for anyone, on the internet. "
                           "The games in progress are pushed every few seconds, each finished game once with "
                           "its evaluations and clocks."));
    ImGui::PopTextWrapPos();

    const auto text = [&](const char* id, std::string& value, ImGuiInputTextFlags flags) {
        char buffer[512];
        const auto n = std::min(value.size(), sizeof(buffer) - 1);
        std::copy_n(value.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (!ImGui::InputText(id, buffer, sizeof(buffer), flags)) return false;
        value = buffer;
        return true;
    };

    lichessTokenField();

    ImGui::TextUnformatted(TR("Round (its URL or id; empty: a new broadcast named after the tournament)"));
    changed |= text("##lichess_round", setup_.lichess_round, 0);
    changed |= ImGui::Checkbox(TR("Listed among lichess' broadcasts"), &setup_.lichess_listed);
    ui::help(TR("Otherwise the broadcast is unlisted: only those who have its link see it."));

    const bool curl = curlAvailable();
    ImGui::BeginDisabled(lichess_token_.empty() || !curl);
    changed |= ImGui::Checkbox(TR("Push the games to lichess"), &setup_.broadcast_lichess);
    ImGui::EndDisabled();
    if (!curl) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", TR("curl is needed to reach lichess, and was not found."));
        ImGui::PopStyleColor();
    }

    const auto status = broadcaster_.lichess();
    if (!status.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", status.error.c_str());
        ImGui::PopStyleColor();
    }
    if (status.running) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::GOOD);
        ImGui::TextUnformatted(status.last_push.empty()
                                   ? TR("● On air")
                                   : trf("● On air · {} games · pushed at {}", status.games, status.last_push).c_str());
        ImGui::PopStyleColor();
    }
    if (!status.url.empty() && ImGui::SmallButton(status.url.c_str())) SDL_OpenURL(status.url.c_str());
    ui::endCard();
}

}  // namespace fcg
