#include "live.hpp"

#include "cpu_time.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <fstream>

#include <core/config/config.hpp>

namespace fcg {

namespace {

constexpr int colorIndex(chess::Color c) { return c == chess::Color::WHITE ? 0 : 1; }

int8_t resultForA(const tour::GameResult& result, int id_a) {
    const double score = result.scoreOf(id_a);
    return score == 1.0 ? 1 : score == 0.0 ? -1 : 0;
}

}  // namespace

GuiObserver::GuiObserver(int slots, const tour::TournamentState& state)
    : engine_a_(state.spec().participants.at(0).engine.name),
      engine_b_(state.spec().participants.at(1).engine.name),
      id_a_(state.spec().participants.at(0).id),
      id_b_(state.spec().participants.at(1).id),
      pgn_fastchess_(state.pgnPath()),
      pgn_chessbase_(state.chessbasePgnPath()) {
    const auto& spec = state.spec();
    match_           = spec.format == tour::Format::Match;
    for (const auto& p : spec.participants) {
        index_[p.id] = static_cast<uint16_t>(names_.size());
        names_.push_back(p.engine.name);
    }
    totals_.resize(names_.size());

    slots_.reserve(static_cast<std::size_t>(std::max(1, slots)));
    for (int i = 0; i < std::max(1, slots); ++i) slots_.push_back(std::make_unique<Slot>());

    // A resumed match: its games and statistics so far, as if they had just been played. The search statistics
    // come from the move comments of the PGN (nps only when it was written with nps=).
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::ifstream pgn(pgn_fastchess_, std::ios::binary);
    std::string text;
    for (const auto& result : state.results()) {
        if (result.voided) continue;
        text.resize(result.pgn.length);
        if (!pgn.seekg(static_cast<std::streamoff>(result.pgn.offset)) || !pgn.read(text.data(), result.pgn.length)) {
            pgn.clear();
            text.clear();
        }
        const auto record = makeRecord(result, text);
        pending_games_.push_back(record);
        countLoss(result);

        forEachMoveComment(text, [&](bool white, const ReplayMove& move) {
            if (!move.book) {
                addMove(totals_[white ? record.white : record.black], move.depth, move.time_ms, move.nps,
                        move.decision_ms);
            }
        });
    }
    if (match_) pending_stats_ = state.pairHistory(id_a_, id_b_);
}

void GuiObserver::updateParticipants(const tour::TournamentSpec& spec) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    for (const auto& p : spec.participants) {
        // An id seen before can come back with another engine: an added engine undone, then another one added.
        if (const auto it = index_.find(p.id); it != index_.end()) {
            if (names_[it->second] != p.engine.name && it->second < totals_.size()) totals_[it->second] = {};
            names_[it->second] = p.engine.name;
            continue;
        }
        index_[p.id] = static_cast<uint16_t>(names_.size());
        names_.push_back(p.engine.name);
    }
    totals_.resize(names_.size());
}

std::vector<GameRecord> GuiObserver::records(const std::vector<tour::GameResult>& results) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::vector<GameRecord> out;
    shown_.clear();
    for (const auto& r : results) {
        if (r.voided) continue;
        out.push_back(makeRecord(r));
        shown_.insert(out.back().game_id);
    }
    return out;
}

const std::string& GuiObserver::name(std::size_t index) const {
    static const std::string unknown = "?";
    return index < names_.size() ? names_[index] : unknown;
}

void GuiObserver::onGameStart(const fastchess::observer::GameStart& info) {
    auto* s = slot(info.slot);
    if (!s) return;

    // Book moves are converted outside the lock.
    chess::Board board;
    board.set960(fastchess::config::TournamentConfig &&
                 fastchess::config::TournamentConfig->variant == fastchess::VariantType::FRC);
    board.setFen(info.start_fen);

    std::vector<LiveMove> moves;
    moves.reserve(info.book_moves.size() + 160);
    for (const auto& uci : info.book_moves) {
        LiveMove m;
        m.move  = chess::uci::uciToMove(board, uci);
        m.san   = chess::uci::moveToSan(board, m.move);
        m.mover = board.sideToMove();
        m.book  = true;
        board.makeMove<true>(m.move);
        moves.push_back(std::move(m));
    }

    std::lock_guard<std::mutex> lock(s->mutex);
    auto& st = s->state;
    if (st.has_game && !st.result.empty()) {
        s->ended.push_back(st);
        if (s->ended.size() > 8) s->ended.pop_front();
    }

    s->board = board;

    st.active    = true;
    st.has_game  = true;
    st.game_id   = info.game_id;
    st.round_id  = info.round_id;
    st.white     = info.white;
    st.black     = info.black;
    st.start_fen = info.start_fen;
    st.moves     = std::move(moves);
    st.stm       = board.sideToMove();
    st.thinking  = false;
    st.white_ms  = info.white_time;
    st.black_ms  = info.black_time;
    st.clocks    = info.white_time > 0 || info.black_time > 0;
    st.info      = {};
    st.result.clear();
    st.reason.clear();
    st.think_start = Clock::now();
    st.version++;

    s->processes   = {};
    s->cpu_at_go   = {-1, -1};
    s->participant = {participantIndex(info.white), participantIndex(info.black)};
}

CpuUse cpuUse(const EngineTotals& t) {
    // 20 s of each, so that the start of the engine (a network to load) does not count for much.
    constexpr uint64_t ENOUGH_MS = 20'000;
    CpuUse use;
    use.measured = t.own_wall_ms >= ENOUGH_MS && t.wait_wall_ms >= ENOUGH_MS;
    if (t.own_wall_ms) use.thinking = static_cast<double>(t.own_cpu_ms) / static_cast<double>(t.own_wall_ms);
    if (t.wait_wall_ms) use.waiting = static_cast<double>(t.wait_cpu_ms) / static_cast<double>(t.wait_wall_ms);
    return use;
}

std::string cpuProblem(const EngineTotals& t, int threads) {
    const auto use = cpuUse(t);
    if (!use.measured) return {};
    if (use.thinking > threads * 1.3 + 0.3) {
        return trf("uses {:.1f} cores while it thinks, with {} thread(s)", use.thinking, threads);
    }
    if (use.waiting > 0.2) return trf("uses {:.0f} % of a core while its opponent thinks", 100 * use.waiting);
    return {};
}

int GuiObserver::participantIndex(const std::string& name) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == name) return static_cast<int>(i);
    }
    return -1;
}

void GuiObserver::onEngineProcesses(int slot_index, int64_t white_pid, int64_t black_pid) {
    auto* s = slot(slot_index);
    if (!s) return;
    auto white = processTree(white_pid), black = processTree(black_pid);
    std::lock_guard<std::mutex> lock(s->mutex);
    s->processes = {std::move(white), std::move(black)};
}

void GuiObserver::onThinking(int slot_index, chess::Color stm, int64_t time_left_ms) {
    auto* s = slot(slot_index);
    if (!s) return;

    std::array<std::vector<int64_t>, 2> processes;
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        processes = s->processes;
    }
    const std::array<int64_t, 2> cpu{cpuTimeMs(processes[0]), cpuTimeMs(processes[1])};

    std::lock_guard<std::mutex> lock(s->mutex);
    s->cpu_at_go = cpu;
    s->go_at     = Clock::now();
    auto& st     = s->state;

    st.stm                                                   = stm;
    st.thinking                                              = true;
    st.think_start                                           = Clock::now();
    (stm == chess::Color::WHITE ? st.white_ms : st.black_ms) = time_left_ms;
    st.info[colorIndex(stm)].clear();
    st.version++;
}

void GuiObserver::onEngineLine(int slot_index, chess::Color side, std::string_view line) {
    // Hot path: engines can print thousands of lines per second. Keep only scored info lines.
    if (line.size() < 12 || line.compare(0, 5, "info ") != 0) return;
    if (line.compare(5, 7, "string ") == 0) return;
    if (line.find(" score ") == std::string_view::npos) return;

    auto* s = slot(slot_index);
    if (!s) return;

    std::lock_guard<std::mutex> lock(s->mutex);
    s->state.info[colorIndex(side)].assign(line.data(), line.size());
    s->state.version++;
}

void GuiObserver::onMove(int slot_index, chess::Color mover, const fastchess::MoveData& data, const std::string& san,
                         int64_t white_time_left, int64_t black_time_left) {
    auto* s = slot(slot_index);
    if (!s) return;

    LiveMove m;
    m.san      = san;
    m.mover    = mover;
    m.score    = data.score;
    m.wdl      = data.wdl;
    m.depth    = data.depth;
    m.seldepth = data.seldepth;
    m.time_ms  = data.elapsed_millis;
    m.nodes    = data.nodes;
    m.nps      = data.nps;
    m.tbhits   = data.tbhits;
    m.pv       = data.pv;

    m.decision_ms = data.decision_millis;

    // What the engines used while the mover thought.
    std::array<std::vector<int64_t>, 2> processes;
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        processes = s->processes;
    }
    const std::array<int64_t, 2> cpu{cpuTimeMs(processes[0]), cpuTimeMs(processes[1])};

    std::lock_guard<std::mutex> lock(s->mutex);
    auto& st = s->state;

    if (s->cpu_at_go[0] >= 0 && s->cpu_at_go[1] >= 0 && cpu[0] >= 0 && cpu[1] >= 0) {
        const auto wall = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - s->go_at).count());
        const int me = colorIndex(mover), other = 1 - me;
        std::lock_guard<std::mutex> data_lock(data_mutex_);
        const auto add = [&](int side, bool own) {
            const int p = s->participant[side];
            if (p < 0 || p >= static_cast<int>(totals_.size())) return;
            const auto used = static_cast<uint64_t>(std::max<int64_t>(0, cpu[side] - s->cpu_at_go[side]));
            auto& t         = totals_[p];
            (own ? t.own_cpu_ms : t.wait_cpu_ms) += used;
            (own ? t.own_wall_ms : t.wait_wall_ms) += wall;
        };
        add(me, true);
        add(other, false);
    }
    s->cpu_at_go = {-1, -1};

    m.move = chess::uci::uciToMove(s->board, data.move);
    s->board.makeMove<true>(m.move);

    st.moves.push_back(std::move(m));
    st.stm      = ~mover;
    st.thinking = false;
    if (st.clocks) {
        st.white_ms = white_time_left;
        st.black_ms = black_time_left;
    }
    st.version++;
}

// The reasons fastchess gives for the end of a game, translated where they are shown: TR(reasons_[i]).
[[maybe_unused]] const char* const GAME_END_REASONS[] = {
    N_("White mates"),
    N_("Black mates"),
    N_("White resigns"),
    N_("Black resigns"),
    N_("White makes an illegal move"),
    N_("Black makes an illegal move"),
    N_("White disconnects"),
    N_("Black disconnects"),
    N_("White's connection stalls"),
    N_("Black's connection stalls"),
    N_("White wins by adjudication"),
    N_("Black wins by adjudication"),
    N_("White wins by adjudication: SyzygyTB"),
    N_("Black wins by adjudication: SyzygyTB"),
    N_("Draw by adjudication"),
    N_("Draw by adjudication: SyzygyTB"),
    N_("Draw by 3-fold repetition"),
    N_("Draw by fifty moves rule"),
    N_("Draw by insufficient mating material"),
    N_("Draw by stalemate"),
};

uint16_t GuiObserver::internReason(const std::string& reason) {
    if (auto it = reason_index_.find(reason); it != reason_index_.end()) return it->second;
    if (reasons_.size() >= 65535) return 0;

    const auto index = static_cast<uint16_t>(reasons_.size());
    reasons_.push_back(reason);
    reason_index_.emplace(reason, index);
    return index;
}

void GuiObserver::onGameEnd(int slot_index, std::size_t, std::size_t, const fastchess::MatchData& data, bool counted) {
    auto* s = slot(slot_index);
    if (!s) return;

    std::lock_guard<std::mutex> lock(s->mutex);
    auto& st    = s->state;
    st.active   = false;
    st.thinking = false;
    st.result   = counted ? fastchess::pgn::PgnBuilder::getResultFromWhiteMatch(data.players.white) : "*";
    st.reason   = counted ? data.reason : N_("Game interrupted");
    st.version++;
}

const GuiObserver::GameExtras& GuiObserver::extrasOf(const tour::GameResult& result, std::string_view pgn) {
    if (const auto it = extras_.find(result.pgn.offset); it != extras_.end()) return it->second;

    std::string text;
    if (pgn.empty()) {
        text = tour::readSpan(pgn_fastchess_, result.pgn).value_or("");
        pgn  = text;
    }
    GameExtras extras;
    OpeningName opening{pgnTag(pgn, "ECO"), pgnTag(pgn, "Opening")};
    if (!opening.eco.empty() || !opening.name.empty()) {
        const auto key = opening.eco + "|" + opening.name;
        if (const auto it = opening_index_.find(key); it != opening_index_.end()) {
            extras.opening = it->second;
        } else if (openings_.size() < 65535) {
            extras.opening = static_cast<uint16_t>(openings_.size());
            openings_.push_back(std::move(opening));
            opening_index_.emplace(key, extras.opening);
        }
    }
    extras.facts = evalFacts(pgn, resultForA(result, result.white));
    return extras_.emplace(result.pgn.offset, extras).first->second;
}

GameRecord GuiObserver::makeRecord(const tour::GameResult& result, std::string_view pgn) {
    GameRecord record;
    record.game_id      = result.number() + 1;
    record.round_id     = result.encounter + 1;
    record.white        = index_.count(result.white) ? index_.at(result.white) : 0;
    record.black        = index_.count(result.black) ? index_.at(result.black) : 0;
    record.white_result = resultForA(result, result.white);
    record.a_white      = result.white == id_a_;
    record.a_result     = resultForA(result, id_a_);
    record.termination  = static_cast<uint8_t>(result.termination);
    record.reason       = internReason(result.reason);
    const auto& extras  = extrasOf(result, pgn);
    record.opening      = extras.opening;
    record.facts        = extras.facts;
    record.plies        = static_cast<uint16_t>(std::min<uint32_t>(result.plies, 65535));
    record.pv_warnings  = static_cast<uint16_t>(std::min<std::size_t>(result.pv_warnings.size(), 65535));
    record.pv_ply =
        result.pv_warnings.empty() ? -1 : static_cast<int16_t>(std::min(result.pv_warnings[0].first, 32767u));
    record.duration_ms    = result.duration_ms;
    record.fastchess_span = result.pgn;
    record.chessbase_span = result.pgn_chessbase;
    return record;
}

void GuiObserver::countLoss(const tour::GameResult& result) {
    if (result.result == "1/2-1/2") return;
    const int loser = result.result == "1-0" ? result.black : result.white;
    const auto it   = index_.find(loser);
    if (it == index_.end()) return;
    auto& t = totals_[it->second];

    switch (result.termination) {
        case fastchess::MatchTermination::TIMEOUT:
            t.timeouts++;
            break;
        case fastchess::MatchTermination::DISCONNECT:
        case fastchess::MatchTermination::STALL:
            t.crashes++;
            break;
        case fastchess::MatchTermination::ILLEGAL_MOVE:
            t.illegal++;
            break;
        default:
            break;
    }
}

void GuiObserver::onRecorded(const tour::GameResult& result, const fastchess::MatchData& data,
                             const std::optional<fastchess::Stats>& pair) {
    std::lock_guard<std::mutex> lock(data_mutex_);

    const auto record = makeRecord(result);
    countLoss(result);

    // Per engine search statistics, from the moves each side actually played.
    chess::Board board;
    board.set960(data.variant == fastchess::VariantType::FRC);
    board.setFen(data.fen);
    const bool white_first = board.sideToMove() == chess::Color::WHITE;

    for (std::size_t i = 0; i < data.moves.size(); ++i) {
        const auto& mv = data.moves[i];
        if (mv.book || !mv.legal) continue;

        const bool white_moved = (i % 2 == 0) == white_first;
        addMove(totals_[white_moved ? record.white : record.black], mv.depth, mv.elapsed_millis,
                static_cast<int64_t>(mv.nps), mv.decision_millis);
    }

    pending_games_.push_back(record);

    // `pair` is from the lower id's point of view; statistics are shown from A's.
    if (pair && match_) pending_stats_.push_back(id_a_ < id_b_ ? *pair : ~*pair);
}

void GuiObserver::addMove(EngineTotals& t, int64_t depth, int64_t time_ms, int64_t nps,
                          std::optional<int64_t> decision_ms) {
    t.moves++;
    if (decision_ms) {
        t.decision_ms += static_cast<uint64_t>(std::max<int64_t>(0, *decision_ms));
        t.decision_samples++;
    }
    t.depth_sum += static_cast<uint64_t>(std::max<int64_t>(0, depth));
    t.time_ms += static_cast<uint64_t>(std::max<int64_t>(0, time_ms));
    if (nps > 0) {
        t.nps_sum += static_cast<double>(nps);
        t.nps_samples++;
    }
}

std::vector<SlotState> GuiObserver::endedGames(int i) const {
    if (i < 0 || i >= static_cast<int>(slots_.size())) return {};
    std::lock_guard<std::mutex> lock(slots_[i]->mutex);
    return {slots_[i]->ended.begin(), slots_[i]->ended.end()};
}

bool GuiObserver::copySlot(int i, SlotState& out) const {
    if (i < 0 || i >= static_cast<int>(slots_.size())) return false;

    const auto& s = *slots_[i];
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.state.version == out.version && out.version != 0) return false;

    out = s.state;
    return true;
}

bool GuiObserver::slotActive(int i) const {
    if (i < 0 || i >= static_cast<int>(slots_.size())) return false;
    std::lock_guard<std::mutex> lock(slots_[i]->mutex);
    return slots_[i]->state.active;
}

void GuiObserver::drain(std::vector<GameRecord>& games, std::vector<fastchess::Stats>& stats) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    for (const auto& g : pending_games_) {
        if (shown_.insert(g.game_id).second) games.push_back(g);
    }
    stats.insert(stats.end(), pending_stats_.begin(), pending_stats_.end());
    pending_games_.clear();
    pending_stats_.clear();
}

std::vector<std::string> GuiObserver::reasons() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return reasons_;
}

std::vector<OpeningName> GuiObserver::openings() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return openings_;
}

std::vector<EngineTotals> GuiObserver::totals() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return totals_;
}

}  // namespace fcg
