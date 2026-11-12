#include "broadcaster.hpp"

#include <ctime>

#include <engine/uci_engine.hpp>

#include <fmt/include/fmt/format.h>

#include "i18n.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

bool sameLichess(const BroadcastSettings& a, const BroadcastSettings& b) {
    return a.lichess == b.lichess && a.lichess_token == b.lichess_token && a.lichess_round == b.lichess_round &&
           a.lichess_listed == b.lichess_listed;
}

int scoreCp(const fastchess::Score& s) {
    // Mate in N moves as +-(100000 - N), what TLCS clients read back.
    if (s.type == fastchess::ScoreType::MATE) return s.value > 0 ? 100000 - s.value : -100000 - s.value;
    return static_cast<int>(s.value);
}

}  // namespace

BroadcastInput broadcastInput(const tour::TournamentState& state, const GuiObserver& observer,
                              const std::optional<tour::Anchor>& anchor) {
    BroadcastInput in;
    in.tournament = &state;
    in.dir        = state.dir();
    in.spec       = state.spec();
    in.results    = state.results();
    in.byes       = state.byes();
    in.total      = state.totalGames().value_or(0);
    in.finished   = state.finished();
    in.anchor     = anchor;
    in.slots.resize(static_cast<std::size_t>(observer.slotCount()));
    in.ended.resize(in.slots.size());
    for (int slot = 0; slot < observer.slotCount(); ++slot) {
        observer.copySlot(slot, in.slots[static_cast<std::size_t>(slot)]);
        in.ended[static_cast<std::size_t>(slot)] = observer.endedGames(slot);
    }
    in.taken = Clock::now();
    return in;
}

BroadcastSnapshot makeSnapshot(const BroadcastInput& in) {
    BroadcastSnapshot s;
    const auto& spec   = in.spec;
    const bool frc     = spec.conditions.variant == fastchess::VariantType::FRC;
    s.name             = spec.name;
    s.format           = spec.format == tour::Format::Match        ? "match"
                         : spec.format == tour::Format::RoundRobin ? "round_robin"
                         : spec.format == tour::Format::Gauntlet   ? "gauntlet"
                         : spec.format == tour::Format::Swiss      ? "swiss"
                                                                   : "knockout";

    const auto& results = in.results;
    s.total             = in.total;
    s.finished          = in.finished;
    s.table             = tour::computeTable(spec, results, in.anchor, in.byes);

    const auto name = [&](int id) { return spec.participant(id) ? spec.participant(id)->engine.name : "?"; };
    for (auto it = results.rbegin(); it != results.rend(); ++it) {
        // The PGN file holds every game recorded, voided ones too: complete up to the end of the last one.
        s.pgn_bytes = std::max(s.pgn_bytes, it->pgn.end());
        if (it->voided) continue;
        if (s.recent.size() < 20)
            s.recent.push_back(fmt::format("{} - {} {}", name(it->white), name(it->black), it->result));
        if (s.played_games.size() < 300) {
            s.played_games.push_back({it->number() + 1, name(it->white), name(it->black), it->result});
        }
        s.played++;

        auto& st = s.stats;
        st.games++;
        st.white_wins += it->result == "1-0";
        st.black_wins += it->result == "0-1";
        st.draws += it->result == "1/2-1/2";
        st.plies += it->plies;
        st.duration_ms += it->duration_ms;
        using fastchess::MatchTermination;
        std::string kind;
        switch (it->termination) {
            case MatchTermination::ADJUDICATION:
                st.adjudications++;
                break;
            case MatchTermination::TIMEOUT:
                st.time_losses++;
                kind = "time";
                break;
            case MatchTermination::DISCONNECT:
                st.crashes++;
                kind = "crash";
                break;
            case MatchTermination::STALL:
                st.stalls++;
                kind = "stall";
                break;
            case MatchTermination::ILLEGAL_MOVE:
                st.illegal_moves++;
                kind = "illegal";
                break;
            default:
                break;
        }
        if (!kind.empty() && s.anomalies.size() < 100) {
            const auto loser = it->result == "1-0" ? name(it->black) : it->result == "0-1" ? name(it->white) : "";
            s.anomalies.push_back(
                {it->number() + 1, name(it->white), name(it->black), it->result, kind, loser, it->reason});
        }
    }
    if (s.pgn_bytes > 0) {
        s.pgn_file = in.dir / spec.pgnFile();  // TournamentState::pgnPath()
        s.pgn_name = spec.pgnFile();
    }

    const auto elo = [&](const std::string& engine) {
        for (const auto& p : spec.participants) {
            if (p.engine.name == engine) return p.elo;
        }
        return 0;
    };

    const auto now  = in.taken;
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
            LiveGame::Ply ply;
            ply.book    = m.book;
            ply.depth   = static_cast<int>(m.depth);
            ply.time_ms = m.time_ms;
            if (!m.book && m.score) {
                const int cp = scoreCp(*m.score);
                ply.score_cp = m.mover == chess::Color::WHITE ? cp : -cp;
            }
            g.plies.push_back(ply);
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

    for (std::size_t i = 0; i < in.slots.size(); ++i) {
        const int slot = static_cast<int>(i);
        const auto& st = in.slots[i];
        if (st.has_game) {
            auto g = live(slot, st);
            chess::Board board;
            board.set960(frc);
            board.setFen(st.start_fen);
            for (const auto& m : st.moves) board.makeMove<true>(m.move);
            g.fen = board.getFen();
            s.games.push_back(std::move(g));
        }
        for (const auto& ended : in.ended[i]) s.ended.push_back(live(slot, ended));
    }
    return s;
}

Broadcaster::~Broadcaster() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    tlcs_.stop();
    web_.stop();
    // The relay's last push gets a few seconds more in its own destructor.
    lichess_.stop();
}

std::vector<std::string> Broadcaster::configure(const BroadcastSettings& settings) {
    std::vector<std::string> errors;
    const auto sync = [&](auto& server, bool wanted, std::string& error, auto start) {
        if (wanted && !server.running() && error.empty()) {
            error = start();
            if (!error.empty()) errors.push_back(error);
        } else if (!wanted && server.running()) {
            server.stop();
        }
        if (!wanted) error.clear();
    };
    sync(tlcs_, settings.tlcs, tlcs_error_,
         [&] { return tlcs_.start(static_cast<uint16_t>(settings.tlcs_port), settings.tlcs_lanes); });
    sync(web_, settings.web, web_error_, [&] { return web_.start(static_cast<uint16_t>(settings.web_port)); });
    // The thread takes the other settings with the next update, or at once when lichess' changed: the relay stops
    // there, its last push made on its own thread.
    const bool lichess_changed = !sameLichess(settings, settings_);
    settings_                  = settings;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        next_settings_ = settings;
        if (lichess_changed && !thread_.joinable()) thread_ = std::thread([this] { work(); });
    }
    if (lichess_changed) wake_.notify_all();
    return errors;
}

void Broadcaster::update(const tour::TournamentState& state, const GuiObserver& observer,
                         const std::optional<tour::Anchor>& anchor, int featured) {
    if (!settings_.files && !tlcs_.running() && !web_.running() && !settings_.lichess) return;

    // A few times per second is enough for spectators, and costs nothing to the engines.
    const auto now = Clock::now();
    if (now - last_snapshot_ < std::chrono::milliseconds(500)) return;
    last_snapshot_ = now;

    Job job{broadcastInput(state, observer, anchor), featured};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job_ = std::move(job);  // replaces one the thread has not taken yet: only the latest counts
        if (!thread_.joinable()) thread_ = std::thread([this] { work(); });
    }
    wake_.notify_all();
}

std::string Broadcaster::filesStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!files_error_.empty()) return files_error_;
    return files_when_.empty() ? std::string() : trf("Written at {} in {}", files_when_, files_dir_);
}

LichessRelay::Status Broadcaster::lichess() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lichess_.status();
}

void Broadcaster::work() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        wake_.wait(lock, [&] { return quit_ || job_ || !sameLichess(next_settings_, lichess_settings_); });
        // Other lichess settings: a new start, with the next update.
        if (!sameLichess(next_settings_, lichess_settings_)) {
            lichess_settings_ = next_settings_;
            lichess_.stop();
            lichess_for_ = nullptr;
        }
        if (job_) {
            const auto job      = std::move(*job_);
            const auto settings = next_settings_;
            job_.reset();
            lock.unlock();
            publish(job, settings);
            lock.lock();
        } else if (quit_) {
            return;
        }
    }
}

void Broadcaster::publish(const Job& job, const BroadcastSettings& settings) {
    const auto snapshot = makeSnapshot(job.input);
    if (tlcs_.running()) tlcs_.publish(snapshot, job.featured);
    if (web_.running()) web_.publish(snapshot);
    if (settings.lichess) updateLichess(job.input, snapshot, settings);

    const auto now = Clock::now();
    if (settings.files && now - last_files_ >= std::chrono::seconds(1)) {
        last_files_      = now;
        const auto dir   = settings.dir.empty() ? job.input.dir / "live" : settings.dir;
        const auto error = writeBroadcastFiles(snapshot, dir);
        const auto t     = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char when[16];
        std::strftime(when, sizeof(when), "%H:%M:%S", &tm);
        std::lock_guard<std::mutex> lock(mutex_);
        files_error_ = error;
        files_when_  = when;
        files_dir_   = dir.string();
    }
}

void Broadcaster::updateLichess(const BroadcastInput& input, const BroadcastSnapshot& snapshot,
                                const BroadcastSettings& settings) {
    const auto& spec = input.spec;
    const bool frc   = spec.conditions.variant == fastchess::VariantType::FRC;
    if (lichess_for_ != input.tournament) {
        // Started once per tournament and settings; after an error it waits for them to change.
        std::lock_guard<std::mutex> lock(mutex_);
        lichess_.stop();
        lichess_for_ = input.tournament;
        if (settings.lichess_token.empty()) return;
        lichess_results_ = input.results.size();
        lichess_.start({settings.lichess_token, settings.lichess_round, settings.lichess_listed, spec.name, input.dir});
    }
    if (!lichess_.running()) return;
    const auto now = Clock::now();
    if (now - last_lichess_ < std::chrono::seconds(2)) return;
    last_lichess_ = now;

    std::vector<LichessGame> live;
    for (const auto& g : snapshot.games) {
        if (g.active) live.push_back(lichessLiveGame(g, spec.name, g.white_elo, g.black_elo, frc));
    }
    std::vector<LichessGame> finished;
    const auto& results = input.results;
    const auto pgn_file = input.dir / spec.chessbasePgnFile();  // TournamentState::chessbasePgnPath()
    for (; lichess_results_ < results.size(); ++lichess_results_) {
        const auto& r = results[lichess_results_];
        if (r.voided) continue;
        const auto* white = spec.participant(r.white);
        const auto* black = spec.participant(r.black);
        const auto pgn    = tour::readSpan(pgn_file, r.pgn_chessbase);
        if (!white || !black || !pgn) continue;
        finished.push_back(lichessFinishedGame(*pgn, spec.name, r.encounter + 1, white->engine.name, black->engine.name,
                                               white->elo, black->elo, r.result, frc));
    }
    lichess_.offer(std::move(live), std::move(finished));
}

void Broadcaster::stop() {
    // The last update published first.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    tlcs_.stop();
    web_.stop();
    lichess_.stop();
    lichess_.wait();
    lichess_for_ = nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = false;
}

}  // namespace fcg
