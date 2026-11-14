// Broadcasting the open tournament: live files, the TLCS server and the web page.

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <engine/uci_engine.hpp>

#include "app.hpp"
#include "broadcaster.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "net.hpp"
#include "qr.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// A QR code `side` pixels wide, its quiet zone included: dark modules on white whatever the theme, as phones read
// them best. True when clicked.
bool drawQr(const QrCode& qr, float side) {
    const auto at   = ImGui::GetCursorScreenPos();
    const bool hit  = ImGui::InvisibleButton("##qr", ImVec2(side, side));
    auto* draw      = ImGui::GetWindowDrawList();
    const int cells = qr.size + 8;  // four modules of quiet zone each side
    const float m   = std::floor(side / static_cast<float>(cells));
    const float pad = std::floor((side - m * static_cast<float>(qr.size)) / 2);
    draw->AddRectFilled(at, ImVec2(at.x + side, at.y + side), IM_COL32_WHITE);
    for (int y = 0; y < qr.size; ++y) {
        for (int x = 0; x < qr.size; ++x) {
            if (!qr.at(x, y)) continue;
            const ImVec2 a(at.x + pad + static_cast<float>(x) * m, at.y + pad + static_cast<float>(y) * m);
            draw->AddRectFilled(a, ImVec2(a.x + m, a.y + m), IM_COL32_BLACK);
        }
    }
    return hit;
}

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
    settings.chat_link      = setup_.web_chat_link;
    settings.discord_id     = setup_.web_discord_id;
    settings.lichess        = setup_.broadcast_lichess;
    settings.lichess_token  = lichess_token_;
    settings.lichess_round  = setup_.lichess_round;
    settings.lichess_listed = setup_.lichess_listed;
    // The tunnel stops with the tournament; another server needs a host.
    settings.tunnel =
        setup_.broadcast_tunnel && run_.running() && (setup_.tunnel_service == 0 || !setup_.tunnel_host.empty());
    settings.tunnel_to = tunnelSettings(setup_, storage_.root() / "known_hosts.json");
    // What is being typed is taken once the field is left: not a folder per keystroke, nor a lichess broadcast for
    // each letter of a round.
    if (broadcast_editing_) {
        const auto& applied    = broadcaster_.settings();
        settings.dir           = applied.dir;
        settings.lichess_token = applied.lichess_token;
        settings.lichess_round = applied.lichess_round;
        settings.tunnel_to     = applied.tunnel_to;
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
    ImGui::TextDisabled("%s", TR("For TLCV, node-tlcv and Livius: one game at a time, with the "
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
    drawWebChat(changed);
    drawTunnel(changed);
    ui::endCard();

    ImGui::Spacing();
    drawLichess(changed);

    if (changed) saveSetupSoon();
}

void App::drawWebChat(bool& changed) {
    ImGui::Spacing();
    // A field, its label on the right. True while it is being typed in.
    const auto field = [&](const char* label, std::string& value, float width) {
        char buffer[2100];
        const auto n = std::min(value.size(), sizeof(buffer) - 1);
        std::copy_n(value.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(width);
        if (ImGui::InputText(label, buffer, sizeof(buffer))) {
            value   = buffer;
            changed = true;
        }
        return ImGui::IsItemActive();
    };
    // What a field holds told wrong once it is left, not at each keystroke; the page leaves it out.
    const auto wrong = [](const std::string& value, bool typing, bool valid, const char* text) {
        if (typing || valid || value.find_first_not_of(" \t") == std::string::npos) return;
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    };

    bool typing = field(TR("Chat link##web_chat"), setup_.web_chat_link, px(360));
    ui::help(TR("A Chat button in the page's header opens this address in a new tab: a Discord invite, a Twitch "
                "channel, a forum thread. An https link only. While the games are pushed to lichess, the page also "
                "links to the lichess round, which has its own chat."));
    wrong(setup_.web_chat_link, typing, !webChatLink(setup_.web_chat_link).empty(), TR("Not an https link."));

    typing = field(TR("Discord server ID##web_discord"), setup_.web_discord_id, px(220));
    ui::help(TR("The page shows Discord's widget of this server, in its light or dark theme: its name, the members "
                "online and a Join button, not the messages. The server's owner switches the widget on in Server "
                "Settings, Widget, which also shows the server ID. Discord then sees the visits of the page's "
                "viewers."));
    wrong(setup_.web_discord_id, typing, !webDiscordId(setup_.web_discord_id).empty(),
          TR("A Discord server ID is 17 to 20 digits."));
}

void App::drawTunnel(bool& changed) {
    ImGui::Spacing();
    changed |= ImGui::Checkbox(TR("Publish on the internet"), &setup_.broadcast_tunnel);
    ui::help(TR("While a tournament runs, an SSH tunnel built into fastchess-gui gives the page a public https "
                "address, without opening a port in the router or installing anything. Only the page is reachable "
                "that way, read-only."));
    // Changed only while it is not connected; after an error, to mend it (it starts again once a field is left).
    const auto status = broadcaster_.tunnel();
    const bool locked = status.state == TunnelState::Connecting || status.state == TunnelState::OnAir;

    ImGui::BeginDisabled(locked);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Service:"));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("localhost.run", &setup_.tunnel_service, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Another SSH server"), &setup_.tunnel_service, 1);
    if (setup_.tunnel_service == 1) {
        const auto field = [&](const char* id, std::string& value, float width) {
            char buffer[256];
            const auto n = std::min(value.size(), sizeof(buffer) - 1);
            std::copy_n(value.data(), n, buffer);
            buffer[n] = 0;
            ImGui::SetNextItemWidth(width);
            if (ImGui::InputText(id, buffer, sizeof(buffer))) {
                value   = buffer;
                changed = true;
            }
            broadcast_editing_ |= ImGui::IsItemActive();
        };
        const auto port = [&](const char* label, int& value, int low) {
            ImGui::SetNextItemWidth(px(110));
            if (ui::inputInt(label, &value)) {
                value   = std::clamp(value, low, 65535);
                changed = true;
            }
            broadcast_editing_ |= ImGui::IsItemActive();
        };
        field(TR("Host##tunnel_host"), setup_.tunnel_host, px(260));
        ImGui::SameLine(0, px(16));
        port(TR("Port##tunnel_port"), setup_.tunnel_port, 1);
        field(TR("User##tunnel_user"), setup_.tunnel_user, px(260));
        ImGui::SameLine(0, px(16));
        port(TR("Remote port##tunnel_remote"), setup_.tunnel_remote_port, 0);
        ui::help(TR("The port the server opens for the page, as ssh -R REMOTE_PORT:localhost:PORT user@host would "
                    "ask (0: the server chooses). Authentication \"none\" only: a service that lets anyone in, or a "
                    "server of one's own that lets the user in without a key. Its host key is trusted the first time "
                    "and must stay the same."));
    }
    ImGui::EndDisabled();
    if (setup_.tunnel_service == 0) {
        ImGui::TextDisabled("%s", TR("Free, without an account: its addresses change now and then, and its speed is "
                                     "limited."));
    }

    if (!setup_.broadcast_tunnel) return;
    const auto line = [](ImU32 colour, const std::string& text) {
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    };
    if (!setup_.broadcast_web) {
        ImGui::TextDisabled("%s", TR("It needs the web page served."));
        return;
    }
    if (setup_.tunnel_service == 1 && setup_.tunnel_host.empty()) {
        ImGui::TextDisabled("%s", TR("Give the server's host."));
        return;
    }
    switch (status.state) {
        case TunnelState::Off:
            ImGui::TextDisabled("%s", TR("Starts with the tournament."));
            return;
        case TunnelState::Connecting:
            line(ui::MUTED, status.error.empty() ? std::string(TR("● Connecting…"))
                                                 : trf("● Connecting again… ({})", status.error));
            break;
        case TunnelState::OnAir:
            line(ui::GOOD, trn("● On air on the internet · {} connection", "● On air on the internet · {} connections",
                               static_cast<uint64_t>(status.connections), status.connections));
            break;
        case TunnelState::Reconnecting:
            line(ui::WARN, trf("● Reconnecting in a moment: {}", status.error));
            break;
        case TunnelState::Failed:
            line(ui::BAD, status.error);
            if (!status.new_key.empty() && ImGui::SmallButton(TR("Trust the new key"))) broadcaster_.trustTunnelKey();
            return;
    }
    // The address only while on air: once the connection is lost, the service may give it to someone else.
    if (status.state != TunnelState::OnAir || status.url.empty()) return;

    // The address, to copy, to open, and as a QR code for a phone.
    ImGui::TextUnformatted(status.url.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Copy"))) ImGui::SetClipboardText(status.url.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Open"))) SDL_OpenURL(status.url.c_str());
    if (status.url != tunnel_qr_text_) {
        tunnel_qr_text_ = status.url;
        tunnel_qr_      = makeQr(status.url);
    }
    if (tunnel_qr_.size > 0) {
        if (drawQr(tunnel_qr_, px(132))) ImGui::OpenPopup("##tunnel_qr");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Click to enlarge"));
        if (ImGui::BeginPopup("##tunnel_qr")) {
            drawQr(tunnel_qr_, px(360));
            ImGui::TextDisabled("%s", status.url.c_str());
            ImGui::EndPopup();
        }
    }
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
