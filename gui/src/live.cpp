#include "live.hpp"

#include <algorithm>

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
    slots_.reserve(static_cast<std::size_t>(std::max(1, slots)));
    for (int i = 0; i < std::max(1, slots); ++i) slots_.push_back(std::make_unique<Slot>());

    // A resumed match: its games and statistics so far, as if they had just been played.
    std::lock_guard<std::mutex> lock(data_mutex_);
    for (const auto& result : state.results()) {
        pending_games_.push_back(makeRecord(result));
        countLoss(result);
    }
    pending_stats_ = state.pairHistory(id_a_, id_b_);
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
}

void GuiObserver::onThinking(int slot_index, chess::Color stm, int64_t time_left_ms) {
    auto* s = slot(slot_index);
    if (!s) return;

    std::lock_guard<std::mutex> lock(s->mutex);
    auto& st = s->state;

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
    m.depth    = data.depth;
    m.seldepth = data.seldepth;
    m.time_ms  = data.elapsed_millis;
    m.nodes    = data.nodes;
    m.nps      = data.nps;

    std::lock_guard<std::mutex> lock(s->mutex);
    auto& st = s->state;

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
    st.reason   = counted ? data.reason : "Game interrupted";
    st.version++;
}

GameRecord GuiObserver::makeRecord(const tour::GameResult& result) {
    GameRecord record;
    record.game_id        = result.number() + 1;
    record.round_id       = result.encounter + 1;
    record.a_white        = result.white == id_a_;
    record.a_result       = resultForA(result, id_a_);
    record.termination    = static_cast<uint8_t>(result.termination);
    record.reason         = internReason(result.reason);
    record.plies          = static_cast<uint16_t>(std::min<uint32_t>(result.plies, 65535));
    record.duration_ms    = result.duration_ms;
    record.fastchess_span = result.pgn;
    record.chessbase_span = result.pgn_chessbase;
    return record;
}

void GuiObserver::countLoss(const tour::GameResult& result) {
    if (result.result == "1/2-1/2") return;
    const int loser = result.result == "1-0" ? result.black : result.white;
    auto& t         = totals_[loser == id_a_ ? 0 : 1];

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
        const bool by_a        = white_moved == record.a_white;
        auto& t                = totals_[by_a ? 0 : 1];

        t.moves++;
        t.depth_sum += static_cast<uint64_t>(std::max<int64_t>(0, mv.depth));
        t.time_ms += static_cast<uint64_t>(std::max<int64_t>(0, mv.elapsed_millis));
        if (mv.nps > 0) {
            t.nps_sum += static_cast<double>(mv.nps);
            t.nps_samples++;
        }
    }

    pending_games_.push_back(record);

    // `pair` is from the lower id's point of view; statistics are shown from A's.
    if (pair) pending_stats_.push_back(id_a_ < id_b_ ? *pair : ~*pair);
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
    games.insert(games.end(), pending_games_.begin(), pending_games_.end());
    stats.insert(stats.end(), pending_stats_.begin(), pending_stats_.end());
    pending_games_.clear();
    pending_stats_.clear();
}

std::vector<std::string> GuiObserver::reasons() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return reasons_;
}

std::array<EngineTotals, 2> GuiObserver::totals() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return totals_;
}

}  // namespace fcg
