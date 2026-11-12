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
    settings.tlcs_lanes     = setup_.tlcs_lanes;
    settings.web            = setup_.broadcast_web;
    settings.web_port       = setup_.web_port;
    settings.lichess        = setup_.broadcast_lichess;
    settings.lichess_token  = lichess_token_;
    settings.lichess_round  = setup_.lichess_round;
    settings.lichess_listed = setup_.lichess_listed;
    // What is being typed is taken once the field is left: not a folder per keystroke, nor a lichess broadcast for
    // each letter of a round.
    if (broadcast_editing_) {
        const auto& applied    = broadcaster_.settings();
        settings.dir           = applied.dir;
        settings.lichess_token = applied.lichess_token;
        settings.lichess_round = applied.lichess_round;
    }
    // The token is saved at the same moment: settings.json is not rewritten for each character, and the profile and
    // the lichess session have the same token.
    if (!broadcast_editing_) saveLichessToken();
    broadcast_editing_ = false;  // the fields drawn in this frame say again
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
        broadcast_editing_ |= ImGui::IsItemActive();
    }
    if (setup_.broadcast_files && !broadcaster_.filesStatus().empty())
        ImGui::TextDisabled("%s", broadcaster_.filesStatus().c_str());
    ui::endCard();

    ImGui::Spacing();

    // TLCS.
    ui::beginCard("bc_tlcs");
    ui::pageTitle(TR("TLCS server"));
    ImGui::TextDisabled("%s", TR("For TLCV, node-tlcv (as used by ccrl.live) and Livius: one game at a time, with the "
                                 "engines' searches, and the crosstable after every game."));
    toggle(TR("Run the TLCS server"), setup_.broadcast_tlcs);
    ImGui::SetNextItemWidth(px(120));
    ImGui::BeginDisabled(broadcaster_.tlcs().running());
    if (ui::inputInt(TR("UDP port"), &setup_.tlcs_port)) {
        setup_.tlcs_port = std::clamp(setup_.tlcs_port, 1, 65535);
        changed          = true;
    }
    ImGui::SameLine(0, px(24));
    toggle(TR("One port per game"), setup_.tlcs_lanes);
    ImGui::EndDisabled();
    ui::help(
        TR("Every concurrent game on a port of its own: game #1 of the Live tab on the UDP port, #2 on the next "
           "one, and so on, so that every game can be followed. Each port is opened when its game starts."));
    ImGui::BeginDisabled(setup_.tlcs_lanes);
    ImGui::TextUnformatted(TR("Game shown:"));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("automatic"), &setup_.broadcast_follow, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("the one chosen in the Live tab"), &setup_.broadcast_follow, 1);
    ImGui::EndDisabled();
    if (!broadcaster_.tlcsError().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", broadcaster_.tlcsError().c_str());
        ImGui::PopStyleColor();
    } else if (broadcaster_.tlcs().running()) {
        const auto& tlcs = broadcaster_.tlcs();
        const auto ports = tlcs.ports();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::GOOD);
        if (setup_.tlcs_lanes && ports.size() > 1) {
            ImGui::TextUnformatted(
                trf("● On air · {} clients · UDP ports {} to {}", tlcs.clients(), ports.front(), ports.back()).c_str());
        } else {
            ImGui::TextUnformatted(trf("● On air · {} clients", tlcs.clients()).c_str());
        }
        ImGui::PopStyleColor();
        if (const auto error = tlcs.laneError(); !error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
            ImGui::TextWrapped("%s", trf("A game has no port: {}", error).c_str());
            ImGui::PopStyleColor();
        }
    }
    ui::endCard();

    ImGui::Spacing();

    // Web page.
    ui::beginCard("bc_web");
    ui::pageTitle(TR("Web page"));
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", TR("Every game in progress, the one followed with its evaluation bar, arrows and charts; "
                                 "the standings, the crosstable, the statistics and anomalies, and the games' PGN "
                                 "file to download. Read-only, refreshed every second."));
    ImGui::PopTextWrapPos();
    toggle(TR("Serve the web page"), setup_.broadcast_web);
    ImGui::SetNextItemWidth(px(120));
    ImGui::BeginDisabled(broadcaster_.web().running());
    if (ui::inputInt(TR("TCP port"), &setup_.web_port)) {
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

void App::saveLichessToken() {
    if (lichess_token_ == lichess_token_saved_) return;
    storage_.setLichessToken(lichess_token_);
    lichess_token_saved_ = lichess_token_;
}

void App::lichessTokenField() {
    ImGui::TextUnformatted(TR("API token (with the study:write scope)"));
    char buffer[512];
    const auto n = std::min(lichess_token_.size(), sizeof(buffer) - 1);
    std::copy_n(lichess_token_.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    // Saved once the field is left (updateBroadcast), or on the way out.
    if (ImGui::InputText("##lichess_token", buffer, sizeof(buffer), ImGuiInputTextFlags_Password)) {
        lichess_token_ = buffer;
    }
    broadcast_editing_ |= ImGui::IsItemActive();
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
        const bool edited = ImGui::InputText(id, buffer, sizeof(buffer), flags);
        broadcast_editing_ |= ImGui::IsItemActive();
        if (!edited) return false;
        value = buffer;
        return true;
    };

    lichessTokenField();

    ImGui::TextUnformatted(TR("Round (its URL or id; empty: a new broadcast named after the tournament)"));
    changed |= text("##lichess_round", setup_.lichess_round, 0);
    changed |= ImGui::Checkbox(TR("Listed among lichess' broadcasts"), &setup_.lichess_listed);
    ui::help(TR("Otherwise the broadcast is unlisted: only those who have its link see it."));

    ImGui::BeginDisabled(lichess_token_.empty());
    changed |= ImGui::Checkbox(TR("Push the games to lichess"), &setup_.broadcast_lichess);
    ImGui::EndDisabled();

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
