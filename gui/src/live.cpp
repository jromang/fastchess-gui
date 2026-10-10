#include "live.hpp"

#include <algorithm>

#include <core/config/config.hpp>

namespace fcg {

namespace {

constexpr int colorIndex(chess::Color c) { return c == chess::Color::WHITE ? 0 : 1; }

int8_t resultForA(const fastchess::MatchData& data, bool a_white) {
    const auto white = data.players.white.result;
    const auto black = data.players.black.result;

    if (white == chess::GameResult::DRAW || black == chess::GameResult::DRAW) return 0;
    const bool white_won = white == chess::GameResult::WIN || black == chess::GameResult::LOSE;
    const bool black_won = black == chess::GameResult::WIN || white == chess::GameResult::LOSE;
    if (white_won) return a_white ? 1 : -1;
    if (black_won) return a_white ? -1 : 1;
    return 0;
}

}  // namespace

GuiObserver::GuiObserver(int slots, std::string engine_a, std::string engine_b, const std::filesystem::path& run_dir)
    : engine_a_(std::move(engine_a)),
      engine_b_(std::move(engine_b)),
      pgn_fastchess_(run_dir / "games.pgn"),
      pgn_chessbase_(run_dir / "games-chessbase.pgn") {
    slots_.reserve(static_cast<std::size_t>(std::max(1, slots)));
    for (int i = 0; i < std::max(1, slots); ++i) slots_.push_back(std::make_unique<Slot>());
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

void GuiObserver::onGameEnd(int slot_index, std::size_t game_id, std::size_t round_id, const fastchess::MatchData& data,
                            bool counted) {
    const auto result = fastchess::pgn::PgnBuilder::getResultFromWhiteMatch(data.players.white);

    int64_t duration_ms = 0;
    if (auto* s = slot(slot_index)) {
        std::lock_guard<std::mutex> lock(s->mutex);
        auto& st    = s->state;
        st.active   = false;
        st.thinking = false;
        st.result   = counted ? result : "*";
        st.reason   = counted ? data.reason : "Game interrupted";
        st.version++;
    }

    if (!counted || !fastchess::config::TournamentConfig) return;

    const auto& pgn_config = fastchess::config::TournamentConfig->pgn;
    const auto round       = round_id + 1;

    const auto pgn_fc =
        fastchess::pgn::PgnBuilder(pgn_config, data, round, fastchess::pgn::CommentStyle::Fastchess).get();
    const auto pgn_cb =
        fastchess::pgn::PgnBuilder(pgn_config, data, round, fastchess::pgn::CommentStyle::ChessBase).get();

    GameRecord record;
    record.game_id        = game_id;
    record.round_id       = static_cast<uint32_t>(round);
    record.a_white        = data.players.white.config.name == engine_a_;
    record.a_result       = resultForA(data, record.a_white);
    record.termination    = static_cast<uint8_t>(data.termination);
    record.plies          = static_cast<uint16_t>(std::min<std::size_t>(data.moves.size(), 65535));
    record.fastchess_span = pgn_fastchess_.append(pgn_fc);
    record.chessbase_span = pgn_chessbase_.append(pgn_cb);

    // "HH:MM:SS" from fastchess.
    {
        int h = 0, m = 0, sec = 0;
        if (std::sscanf(data.duration.c_str(), "%d:%d:%d", &h, &m, &sec) == 3) {
            duration_ms = (static_cast<int64_t>(h) * 3600 + m * 60 + sec) * 1000;
        }
        record.duration_ms = static_cast<uint32_t>(std::clamp<int64_t>(duration_ms, 0, UINT32_MAX));
    }

    std::lock_guard<std::mutex> lock(data_mutex_);
    record.reason = internReason(data.reason);

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

    if (const auto loser = data.getLosingPlayer()) {
        auto& t = totals_[loser->config.name == engine_a_ ? 0 : 1];
        switch (data.termination) {
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

    pending_games_.push_back(record);
}

void GuiObserver::onResult(const fastchess::Stats& h2h, const std::string& first, const std::string&) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    pending_stats_.push_back(first == engine_a_ ? h2h : ~h2h);
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
