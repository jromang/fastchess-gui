// Broadcasting the open tournament: live files, the TLCS server and the web page.

#include <algorithm>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <engine/uci_engine.hpp>

#include "app.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "net.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

int scoreCp(const fastchess::Score& s) {
    if (s.type == fastchess::ScoreType::MATE) return s.value > 0 ? 100000 - 2 * s.value : -100000 - 2 * s.value;
    return static_cast<int>(s.value);
}

}  // namespace

BroadcastSnapshot App::makeSnapshot() {
    BroadcastSnapshot s;
    const auto* t   = run_.tournament();
    const auto* obs = run_.observer();
    if (!t || !obs) return s;

    const auto& spec = t->spec();
    const bool frc   = spec.conditions.variant == fastchess::VariantType::FRC;
    s.name           = spec.name;
    s.format         = spec.format == tour::Format::Match        ? "match"
                       : spec.format == tour::Format::RoundRobin ? "round_robin"
                                                                 : "gauntlet";
    s.played         = games_.size();
    s.total          = run_.info().total_games;
    s.finished       = t->finished();
    s.table          = tour::computeTable(spec, t->results(), anchor_);

    for (int i = static_cast<int>(games_.size()) - 1; i >= 0 && s.recent.size() < 20; --i) {
        const auto& g = games_[static_cast<std::size_t>(i)];
        s.recent.push_back(fmt::format("{} - {} {}", obs->name(g.white), obs->name(g.black),
                                       g.white_result == 0  ? "1/2-1/2"
                                       : g.white_result > 0 ? "1-0"
                                                            : "0-1"));
    }

    const auto now = Clock::now();
    for (int slot = 0; slot < obs->slotCount(); ++slot) {
        refreshSlot(slot);
        const auto& v  = slot_views_[static_cast<std::size_t>(slot)];
        const auto& st = v.state;
        if (!st.has_game) continue;

        LiveGame g;
        g.slot          = slot;
        g.number        = st.game_id;
        g.white         = st.white;
        g.black         = st.black;
        g.start_fen     = st.start_fen;
        g.fen           = v.board.getFen();
        g.white_to_move = st.stm == chess::Color::WHITE;
        g.clocks        = st.clocks;
        g.active        = st.active;
        g.result        = st.result;
        for (const auto& m : st.moves) {
            g.san.push_back(m.san);
            g.uci.push_back(chess::uci::moveToUci(m.move, frc));
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.think_start).count();
        g.white_ms         = st.white_ms - (st.active && st.thinking && g.white_to_move ? elapsed : 0);
        g.black_ms         = st.black_ms - (st.active && st.thinking && !g.white_to_move ? elapsed : 0);

        for (int side = 0; side < 2; ++side) {
            auto& sr           = g.search[side];
            const auto color   = side == 0 ? chess::Color::WHITE : chess::Color::BLACK;
            const auto& line   = st.info[static_cast<std::size_t>(side)];
            const bool current = st.active && st.thinking && st.stm == color && !line.empty();
            if (current) {
                const auto info = fastchess::engine::UciEngine::parseInfo(line);
                sr.depth        = static_cast<int>(info.depth.value_or(0));
                sr.nodes        = info.nodes.value_or(0);
                sr.time_ms      = info.time.value_or(elapsed);
                sr.pv           = info.pv;
                if (info.score) sr.score_cp = scoreCp(*info.score);
                continue;
            }
            // Its last move otherwise.
            for (auto it = st.moves.rbegin(); it != st.moves.rend(); ++it) {
                if (it->mover != color || it->book) continue;
                sr.depth   = static_cast<int>(it->depth);
                sr.nodes   = it->nodes;
                sr.time_ms = it->time_ms;
                if (it->score) sr.score_cp = scoreCp(*it->score);
                break;
            }
        }
        s.games.push_back(std::move(g));
    }
    return s;
}

void App::updateBroadcast() {
    // Servers follow their switches.
    const auto sync = [](auto& server, bool wanted, int port, std::string& error) {
        if (wanted && !server.running() && error.empty()) {
            error = server.start(static_cast<uint16_t>(port));
            if (!error.empty()) Console::instance().add(error, true);
        } else if (!wanted && server.running()) {
            server.stop();
        }
        if (!wanted) error.clear();
    };
    sync(tlcs_, setup_.broadcast_tlcs, setup_.tlcs_port, tlcs_error_);
    sync(web_, setup_.broadcast_web, setup_.web_port, web_error_);

    const bool any = setup_.broadcast_files || tlcs_.running() || web_.running();
    if (!any || !run_.tournament()) return;

    // A few times per second is enough for spectators, and costs nothing to the engines.
    const auto now = std::chrono::steady_clock::now();
    if (now - last_snapshot_ < std::chrono::milliseconds(500)) return;
    last_snapshot_ = now;

    const auto snapshot = makeSnapshot();
    if (tlcs_.running()) tlcs_.publish(snapshot, setup_.broadcast_follow == 1 ? live_slot_ : -1);
    if (web_.running()) web_.publish(snapshot);

    if (setup_.broadcast_files && now - last_files_ >= std::chrono::seconds(1)) {
        last_files_      = now;
        const auto dir   = setup_.broadcast_dir.empty() ? run_.tournament()->dir() / "live"
                                                        : std::filesystem::path(setup_.broadcast_dir);
        const auto error = writeBroadcastFiles(snapshot, dir);
        char when[16];
        const auto t = std::time(nullptr);
        std::strftime(when, sizeof(when), "%H:%M:%S", std::localtime(&t));
        files_status_ = error.empty() ? trf("Written at {} in {}", when, dir.string()) : error;
    }
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
    if (setup_.broadcast_files && !files_status_.empty()) ImGui::TextDisabled("%s", files_status_.c_str());
    ui::endCard();

    ImGui::Spacing();

    // TLCS.
    ui::beginCard("bc_tlcs");
    ui::pageTitle(TR("TLCS server"));
    ImGui::TextDisabled("%s", TR("For TLCV, node-tlcv (as used by ccrl.live) and Livius: one game at a time, with the "
                                 "engines' searches, and the crosstable."));
    toggle(TR("Run the TLCS server"), setup_.broadcast_tlcs);
    ImGui::SetNextItemWidth(px(120));
    ImGui::BeginDisabled(tlcs_.running());
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
    if (!tlcs_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", tlcs_error_.c_str());
        ImGui::PopStyleColor();
    } else if (tlcs_.running()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::GOOD);
        ImGui::TextUnformatted(trf("● On air · {} clients", tlcs_.clients()).c_str());
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
    ImGui::BeginDisabled(web_.running());
    if (ImGui::InputInt(TR("TCP port"), &setup_.web_port, 0, 0)) {
        setup_.web_port = std::clamp(setup_.web_port, 1, 65535);
        changed         = true;
    }
    ImGui::EndDisabled();
    if (!web_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", web_error_.c_str());
        ImGui::PopStyleColor();
    } else if (web_.running()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::GOOD);
        ImGui::TextUnformatted(trf("● On air · {} viewers", web_.viewers()).c_str());
        ImGui::PopStyleColor();
        for (const auto& address : net::localAddresses()) {
            const auto url = fmt::format("http://{}:{}", address, setup_.web_port);
            if (ImGui::SmallButton(url.c_str())) SDL_OpenURL(url.c_str());
        }
        ImGui::TextDisabled("%s", TR("Reachable from the local network; open the port in your router to share it "
                                     "further."));
    }
    ui::endCard();

    if (changed) saveSetupSoon();
}

}  // namespace fcg
