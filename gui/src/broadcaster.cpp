#include "broadcaster.hpp"

#include <ctime>

#include <engine/uci_engine.hpp>

#include <fmt/include/fmt/format.h>

#include "i18n.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

int scoreCp(const fastchess::Score& s) {
    // Mate in N moves as +-(100000 - N), what TLCS clients read back.
    if (s.type == fastchess::ScoreType::MATE) return s.value > 0 ? 100000 - s.value : -100000 - s.value;
    return static_cast<int>(s.value);
}

}  // namespace

BroadcastSnapshot makeSnapshot(const tour::TournamentState& state, const GuiObserver& observer,
                               const std::optional<tour::Anchor>& anchor) {
    BroadcastSnapshot s;
    const auto& spec   = state.spec();
    const bool frc     = spec.conditions.variant == fastchess::VariantType::FRC;
    s.name             = spec.name;
    s.format           = spec.format == tour::Format::Match        ? "match"
                         : spec.format == tour::Format::RoundRobin ? "round_robin"
                         : spec.format == tour::Format::Gauntlet   ? "gauntlet"
                         : spec.format == tour::Format::Swiss      ? "swiss"
                                                                   : "knockout";
    const auto results = state.results();
    s.total            = state.totalGames().value_or(0);
    s.finished         = state.finished();
    s.table            = tour::computeTable(spec, results, anchor, state.byes());

    const auto name = [&](int id) { return spec.participant(id) ? spec.participant(id)->engine.name : "?"; };
    for (auto it = results.rbegin(); it != results.rend(); ++it) {
        if (it->voided) continue;
        if (s.recent.size() < 20)
            s.recent.push_back(fmt::format("{} - {} {}", name(it->white), name(it->black), it->result));
        s.played++;
    }

    const auto elo = [&](const std::string& engine) {
        for (const auto& p : spec.participants) {
            if (p.engine.name == engine) return p.elo;
        }
        return 0;
    };

    const auto now  = Clock::now();
    const auto live = [&](int slot, const SlotState& st) {
        LiveGame g;
        g.slot          = slot;
        g.number        = st.game_id;
        g.white         = st.white;
        g.black         = st.black;
        g.white_elo     = elo(st.white);
        g.black_elo     = elo(st.black);
        g.start_fen     = st.start_fen;
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
        return g;
    };

    for (int slot = 0; slot < observer.slotCount(); ++slot) {
        SlotState st;
        observer.copySlot(slot, st);
        if (st.has_game) {
            auto g = live(slot, st);
            chess::Board board;
            board.set960(frc);
            board.setFen(st.start_fen);
            for (const auto& m : st.moves) board.makeMove<true>(m.move);
            g.fen = board.getFen();
            s.games.push_back(std::move(g));
        }
        for (const auto& ended : observer.endedGames(slot)) s.ended.push_back(live(slot, ended));
    }
    return s;
}

std::vector<std::string> Broadcaster::configure(const BroadcastSettings& settings) {
    std::vector<std::string> errors;
    const auto sync = [&](auto& server, bool wanted, int port, std::string& error) {
        if (wanted && !server.running() && error.empty()) {
            error = server.start(static_cast<uint16_t>(port));
            if (!error.empty()) errors.push_back(error);
        } else if (!wanted && server.running()) {
            server.stop();
        }
        if (!wanted) error.clear();
    };
    sync(tlcs_, settings.tlcs, settings.tlcs_port, tlcs_error_);
    sync(web_, settings.web, settings.web_port, web_error_);
    settings_ = settings;
    return errors;
}

void Broadcaster::update(const tour::TournamentState& state, const GuiObserver& observer,
                         const std::optional<tour::Anchor>& anchor, int featured) {
    if (!settings_.files && !tlcs_.running() && !web_.running()) return;

    // A few times per second is enough for spectators, and costs nothing to the engines.
    const auto now = Clock::now();
    if (now - last_snapshot_ < std::chrono::milliseconds(500)) return;
    last_snapshot_ = now;

    const auto snapshot = makeSnapshot(state, observer, anchor);
    if (tlcs_.running()) tlcs_.publish(snapshot, featured);
    if (web_.running()) web_.publish(snapshot);

    if (settings_.files && now - last_files_ >= std::chrono::seconds(1)) {
        last_files_      = now;
        const auto dir   = settings_.dir.empty() ? state.dir() / "live" : settings_.dir;
        const auto error = writeBroadcastFiles(snapshot, dir);
        char when[16];
        const auto t = std::time(nullptr);
        std::strftime(when, sizeof(when), "%H:%M:%S", std::localtime(&t));
        files_status_ = error.empty() ? trf("Written at {} in {}", when, dir.string()) : error;
    }
}

void Broadcaster::stop() {
    tlcs_.stop();
    web_.stop();
}

}  // namespace fcg
