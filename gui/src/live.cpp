#include "live.hpp"

#include "cpu_time.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>

#include <core/config/config.hpp>
#include <tour/files.hpp>

namespace fcg {

namespace {

constexpr int colorIndex(chess::Color c) { return c == chess::Color::WHITE ? 0 : 1; }

int8_t resultForA(const tour::GameResult& result, int id_a) {
    const double score = result.scoreOf(id_a);
    return score == 1.0 ? 1 : score == 0.0 ? -1 : 0;
}

// What the engines of the game that just ended on this tournament thread used: gameEnd() keeps it for recorded(),
// which the runner's callbacks call next on the same thread when it records the game.
struct EndedCpu {
    const void* meter = nullptr;
    GameCpu cpu;
};
thread_local std::optional<EndedCpu> t_ended_cpu;

// The size of a file, 0 when there is none.
uint64_t knownSize(const std::filesystem::path& file) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    return ec ? 0 : size;
}

}  // namespace

std::optional<bool> seedHasBlack(const tour::TournamentSpec& spec, const std::string& white, const std::string& black) {
    if (spec.format != tour::Format::Gauntlet) return std::nullopt;
    const auto seed = [&](const std::string& name) {
        const auto n =
            std::min<std::size_t>(static_cast<std::size_t>(std::max(1, spec.seeds)), spec.participants.size());
        for (std::size_t i = 0; i < n; ++i) {
            // A replaced seed's games are played by its replacement, and so on.
            const auto* p = &spec.participants[i];
            for (int hops = 0; p && hops <= static_cast<int>(spec.participants.size()); ++hops) {
                if (p->engine.name == name) return true;
                p = p->replaced_by ? spec.participant(*p->replaced_by) : nullptr;
            }
        }
        return false;
    };
    const bool w = seed(white), b = seed(black);
    if (w == b) return std::nullopt;
    return b;
}

std::string gameCpuLine(const GameCpuLine& line) {
    const auto& c = line.cpu;
    return fmt::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\n", line.offset, line.finished_at, c.own_cpu_ms[0],
                       c.own_wall_ms[0], c.wait_cpu_ms[0], c.wait_wall_ms[0], c.own_cpu_ms[1], c.own_wall_ms[1],
                       c.wait_cpu_ms[1], c.wait_wall_ms[1]);
}

std::optional<GameCpuLine> parseGameCpuLine(std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    std::array<uint64_t, 10> v{};
    const char* at        = line.data();
    const char* const end = line.data() + line.size();
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0 && (at == end || *at++ != '\t')) return std::nullopt;
        const auto [next, error] = std::from_chars(at, end, v[i]);
        if (error != std::errc()) return std::nullopt;
        at = next;
    }
    if (at != end) return std::nullopt;

    GameCpuLine out;
    out.offset      = v[0];
    out.finished_at = static_cast<int64_t>(v[1]);
    for (std::size_t side = 0; side < 2; ++side) {
        out.cpu.own_cpu_ms[side]   = v[2 + side * 4];
        out.cpu.own_wall_ms[side]  = v[3 + side * 4];
        out.cpu.wait_cpu_ms[side]  = v[4 + side * 4];
        out.cpu.wait_wall_ms[side] = v[5 + side * 4];
    }
    return out;
}

CpuMeter::CpuMeter(int slots, std::filesystem::path file) : path_(std::move(file)) {
    slots_.reserve(static_cast<std::size_t>(std::max(1, slots)));
    for (int i = 0; i < std::max(1, slots); ++i) slots_.push_back(std::make_unique<Slot>());
}

CpuMeter::~CpuMeter() {
    if (file_) std::fclose(file_);
}

void CpuMeter::gameStart(int slot_index) {
    auto* s = slot(slot_index);
    if (!s) return;
    std::lock_guard<std::mutex> lock(s->mutex);
    s->processes = {};
    s->cpu_at_go = {-1, -1};
    s->game      = {};
}

void CpuMeter::engineProcesses(int slot_index, int64_t white_pid, int64_t black_pid) {
    auto* s = slot(slot_index);
    if (!s) return;
    auto white = processTree(white_pid), black = processTree(black_pid);
    std::lock_guard<std::mutex> lock(s->mutex);
    s->processes = {std::move(white), std::move(black)};
}

void CpuMeter::thinking(int slot_index) {
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
}

std::optional<CpuMeter::Used> CpuMeter::moved(int slot_index, chess::Color mover) {
    auto* s = slot(slot_index);
    if (!s) return std::nullopt;
    std::array<std::vector<int64_t>, 2> processes;
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        processes = s->processes;
    }
    const std::array<int64_t, 2> cpu{cpuTimeMs(processes[0]), cpuTimeMs(processes[1])};

    std::lock_guard<std::mutex> lock(s->mutex);
    const auto at_go = s->cpu_at_go;
    s->cpu_at_go     = {-1, -1};
    if (at_go[0] < 0 || at_go[1] < 0 || cpu[0] < 0 || cpu[1] < 0) return std::nullopt;

    Used used;
    used.wall_ms =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - s->go_at).count());
    for (int side = 0; side < 2; ++side) {
        used.cpu_ms[side] = static_cast<uint64_t>(std::max<int64_t>(0, cpu[side] - at_go[side]));
        const bool own    = side == colorIndex(mover);
        auto& g           = s->game;
        (own ? g.own_cpu_ms : g.wait_cpu_ms)[side] += used.cpu_ms[side];
        (own ? g.own_wall_ms : g.wait_wall_ms)[side] += used.wall_ms;
    }
    return used;
}

void CpuMeter::gameEnd(int slot_index, bool counted) {
    auto* s = slot(slot_index);
    if (!s || !counted) {
        t_ended_cpu.reset();
        return;
    }
    std::lock_guard<std::mutex> lock(s->mutex);
    t_ended_cpu = EndedCpu{this, s->game};
}

void CpuMeter::recorded(const tour::GameResult& result) {
    if (!t_ended_cpu || t_ended_cpu->meter != this) return;
    const auto cpu = t_ended_cpu->cpu;
    t_ended_cpu.reset();
    if (cpu.own_wall_ms[0] + cpu.own_wall_ms[1] == 0) return;  // not measured

    std::lock_guard<std::mutex> lock(file_mutex_);
    if (!file_ && !failed_) {
        // A last line cut short by a crash is ended first, so that the next one is not glued to it.
        bool cut_short = false;
        {
            std::ifstream in(path_, std::ios::binary | std::ios::ate);
            if (in && in.tellg() > 0) {
                in.seekg(-1, std::ios::end);
                cut_short = in.get() != '\n';
            }
        }
        file_   = tour::openFile(path_, "ab");
        failed_ = file_ == nullptr;
        if (file_ && cut_short) std::fputc('\n', file_);
    }
    if (!file_) return;
    // Written right after the game and synced, which costs little once per game: after a crash or a power cut, the
    // games recorded have their processor use, but for a last one cut short.
    const auto line = gameCpuLine({result.pgn.offset, result.finished_at, cpu});
    std::fwrite(line.data(), 1, line.size(), file_);
    (void)tour::syncFile(file_);
}

GuiObserver::GuiObserver(int slots, const tour::TournamentState& state)
    : engine_a_(state.spec().participants.at(0).engine.name),
      engine_b_(state.spec().participants.at(1).engine.name),
      id_a_(state.spec().participants.at(0).id),
      id_b_(state.spec().participants.at(1).id),
      pgn_fastchess_(state.pgnPath()),
      pgn_chessbase_(state.chessbasePgnPath()),
      cpu_path_(state.dir() / "processor.tsv"),
      cpu_known_(knownSize(cpu_path_)),
      cpu_(slots, cpu_path_) {
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
    // come from the move comments of the PGN (nps only when it was written with nps=), the processor use from
    // processor.tsv.
    std::vector<tour::GameResult> played;
    for (auto& result : state.results()) {
        if (!result.voided) played.push_back(std::move(result));
    }
    if (match_) pending_stats_ = state.pairHistory(id_a_, id_b_);
    if (!played.empty()) {
        loading_ = true;
        to_load_ = std::move(played);
        loader_  = std::thread([this] { loadPlayed(); });
    }
}

GuiObserver::~GuiObserver() {
    stop_loading_ = true;
    if (loader_.joinable()) loader_.join();
}

void GuiObserver::loadPlayed() {
    // What each game's PGN tells, read and parsed without the lock, then handed over by batches: the games show up
    // as they are read, and the tournament threads never wait long for the lock.
    constexpr std::size_t BATCH = 256;
    std::vector<tour::GameResult> results;
    uint64_t generation = 0;
    const auto takeWork = [&] {  // data_mutex_ held
        results.swap(to_load_);
        to_load_.clear();
        generation = generation_;
    };
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        takeWork();
    }

    // The processor use of the games played before the tournament was opened, by PGN offset: the lines that were
    // in processor.tsv then (the games played since are counted as they are played). Read by the first loader; the
    // lines of the games it did not count are kept for the next ones.
    if (!cpu_read_) {
        cpu_read_ = true;
        std::ifstream in(cpu_path_, std::ios::binary);
        std::string line;
        for (uint64_t read = 0; read < cpu_known_ && std::getline(in, line);) {
            read += line.size() + 1;
            if (read > cpu_known_ || in.eof()) break;  // a last line without its end: cut short
            if (const auto parsed = parseGameCpuLine(line)) cpu_lines_.push_back(*parsed);
        }
        std::sort(cpu_lines_.begin(), cpu_lines_.end(),
                  [](const GameCpuLine& a, const GameCpuLine& b) { return a.offset < b.offset; });
    }
    const auto cpuOf = [&](const tour::GameResult& result) -> const GameCpu* {
        const auto it = std::lower_bound(cpu_lines_.begin(), cpu_lines_.end(), result.pgn.offset,
                                         [](const GameCpuLine& l, uint64_t offset) { return l.offset < offset; });
        if (it == cpu_lines_.end() || it->offset != result.pgn.offset) return nullptr;
        return it->finished_at == result.finished_at ? &it->cpu : nullptr;
    };

    std::ifstream pgn(pgn_fastchess_, std::ios::binary);
    std::string text;
    std::vector<LoadedGame> parsed;
    for (std::size_t first = 0; !stop_loading_;) {
        if (first == results.size()) {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (generation_ != generation) {
                takeWork();
                first = 0;
                continue;
            }
            break;
        }
        const auto last = std::min(results.size(), first + BATCH);
        parsed.assign(last - first, {});
        for (std::size_t i = first; i < last; ++i) {
            const auto& result = results[i];
            text.resize(result.pgn.length);
            if (!pgn.seekg(static_cast<std::streamoff>(result.pgn.offset)) ||
                !pgn.read(text.data(), result.pgn.length)) {
                pgn.clear();
                text.clear();
            }
            auto& p   = parsed[i - first];
            p.opening = {pgnTag(text, "ECO"), pgnTag(text, "Opening")};
            p.facts   = evalFacts(text, resultForA(result, result.white));
            forEachMoveComment(text, [&](bool white, const ReplayMove& move) {
                if (!move.book) {
                    addMove(p.side[white ? 0 : 1], move.depth, move.time_ms, move.nps, move.nodes, move.decision_ms);
                }
            });
            if (const auto* cpu = cpuOf(result)) {
                for (std::size_t side = 0; side < 2; ++side) {
                    p.side[side].own_cpu_ms   = cpu->own_cpu_ms[side];
                    p.side[side].own_wall_ms  = cpu->own_wall_ms[side];
                    p.side[side].wait_cpu_ms  = cpu->wait_cpu_ms[side];
                    p.side[side].wait_wall_ms = cpu->wait_wall_ms[side];
                }
            }
        }

        std::lock_guard<std::mutex> lock(data_mutex_);
        // records() started over meanwhile: what was read is dropped, and the loader goes on with its games.
        if (generation_ != generation) {
            takeWork();
            first = 0;
            continue;
        }
        for (std::size_t i = first; i < last; ++i) pending_games_.push_back(addLoaded(results[i], parsed[i - first]));
        first = last;
    }

    std::lock_guard<std::mutex> lock(data_mutex_);
    // The lines of the games counted now are not needed any more. Those of the others (voided) are kept: a game
    // voided while the games were read comes back when the edit is undone, and is read then.
    cpu_lines_.erase(std::remove_if(cpu_lines_.begin(), cpu_lines_.end(),
                                    [&](const GameCpuLine& l) { return extras_.count(l.offset) > 0; }),
                     cpu_lines_.end());
    cpu_lines_.shrink_to_fit();
    pending_games_.insert(pending_games_.end(), arrived_.begin(), arrived_.end());
    arrived_.clear();
    loading_ = false;
}

GameRecord GuiObserver::addLoaded(const tour::GameResult& result, LoadedGame& p) {
    // A game read for the first time is counted; one already known (recorded meanwhile, or read before an edit)
    // was counted then.
    if (!extras_.count(result.pgn.offset)) {
        GameExtras extras;
        if (!p.opening.eco.empty() || !p.opening.name.empty()) {
            const auto key = p.opening.eco + "|" + p.opening.name;
            if (const auto it = opening_index_.find(key); it != opening_index_.end()) {
                extras.opening = it->second;
            } else if (openings_.size() < 65535) {
                extras.opening = static_cast<uint16_t>(openings_.size());
                openings_.push_back(std::move(p.opening));
                opening_index_.emplace(key, extras.opening);
            }
        }
        extras.facts = p.facts;
        extras_.emplace(result.pgn.offset, extras);
        countLoss(result);
        const auto record = makeRecord(result);
        for (int side = 0; side < 2; ++side) {
            const auto index = side == 0 ? record.white : record.black;
            if (index >= totals_.size()) continue;
            auto& t         = totals_[index];
            const auto& add = p.side[static_cast<std::size_t>(side)];
            t.moves += add.moves;
            t.depth_sum += add.depth_sum;
            t.nps_sum += add.nps_sum;
            t.nps_samples += add.nps_samples;
            t.nodes_sum += add.nodes_sum;
            t.nodes_samples += add.nodes_samples;
            t.time_ms += add.time_ms;
            t.decision_ms += add.decision_ms;
            t.decision_samples += add.decision_samples;
            t.own_cpu_ms += add.own_cpu_ms;
            t.own_wall_ms += add.own_wall_ms;
            t.wait_cpu_ms += add.wait_cpu_ms;
            t.wait_wall_ms += add.wait_wall_ms;
        }
        return record;
    }
    return makeRecord(result);
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

    // The games of `results` not drained yet are handed out below, as they are now (voided or not); those recorded
    // since `results` was taken stay to come through drain().
    std::unordered_set<uint64_t> known;  // by PGN offset
    for (const auto& r : results) known.insert(r.pgn.offset);
    const auto inResults = [&](const GameRecord& g) { return known.count(g.fastchess_span.offset) > 0; };
    pending_games_.erase(std::remove_if(pending_games_.begin(), pending_games_.end(), inResults), pending_games_.end());
    arrived_.erase(std::remove_if(arrived_.begin(), arrived_.end(), inResults), arrived_.end());

    // The games read already, up to the first one that is not: the PGN of the others is read by the loader rather
    // than here on the GUI thread, and they follow in their order.
    std::vector<GameRecord> out;
    std::vector<tour::GameResult> rest;
    shown_.clear();
    for (const auto& r : results) {
        if (r.voided) continue;
        if (!rest.empty() || !extras_.count(r.pgn.offset)) {
            rest.push_back(r);
            continue;
        }
        out.push_back(makeRecord(r));
        shown_.insert(out.back().game_id);
    }

    // A loader still at work drops what it read and takes the new list, possibly empty.
    if (loading_ || !rest.empty()) {
        to_load_ = std::move(rest);
        ++generation_;
        if (!loading_) {
            if (loader_.joinable()) loader_.join();  // done: loading_ was cleared as its last step
            loading_ = true;
            loader_  = std::thread([this] { loadPlayed(); });
        }
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

    s->participant = {participantIndex(info.white), participantIndex(info.black)};
    cpu_.gameStart(info.slot);
}

std::string pvToSan(chess::Board board, const std::vector<std::string>& pv, std::size_t max_moves) {
    std::string out;
    for (std::size_t i = 0; i < pv.size() && i < max_moves; ++i) {
        const auto move = chess::uci::uciToMove(board, pv[i]);

        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        if (move == chess::Move::NO_MOVE || std::find(legal.begin(), legal.end(), move) == legal.end()) break;

        if (!out.empty()) out += ' ';
        if (board.sideToMove() == chess::Color::WHITE) {
            out += fmt::format("{}.", board.fullMoveNumber());
        } else if (i == 0) {
            out += fmt::format("{}...", board.fullMoveNumber());
        }
        out += chess::uci::moveToSan(board, move);
        board.makeMove<true>(move);
    }
    if (pv.size() > max_moves) out += " …";
    return out;
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
    cpu_.engineProcesses(slot_index, white_pid, black_pid);
}

void GuiObserver::onThinking(int slot_index, chess::Color stm, int64_t time_left_ms) {
    auto* s = slot(slot_index);
    if (!s) return;
    cpu_.thinking(slot_index);

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
    const auto used = cpu_.moved(slot_index, mover);

    std::lock_guard<std::mutex> lock(s->mutex);
    auto& st = s->state;

    if (used) {
        std::lock_guard<std::mutex> data_lock(data_mutex_);
        for (int side = 0; side < 2; ++side) {
            const int p = s->participant[side];
            if (p < 0 || p >= static_cast<int>(totals_.size())) continue;
            const bool own = side == colorIndex(mover);
            auto& t        = totals_[p];
            (own ? t.own_cpu_ms : t.wait_cpu_ms) += used->cpu_ms[side];
            (own ? t.own_wall_ms : t.wait_wall_ms) += used->wall_ms;
        }
    }

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
    cpu_.gameEnd(slot_index, counted);
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
    cpu_.recorded(result);

    std::lock_guard<std::mutex> lock(data_mutex_);

    // The game is in the tournament's results before the observer is told: when the lists started over meanwhile
    // (records(), after an edit), the loader may have read and counted it already. A game known is not counted again.
    const bool counted = extras_.count(result.pgn.offset) > 0;
    const auto record  = makeRecord(result);
    if (!counted) {
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
                    static_cast<int64_t>(mv.nps), mv.nodes, mv.decision_millis);
        }
    }

    // After the games already played, while they are still being read.
    (loading_ ? arrived_ : pending_games_).push_back(record);

    // `pair` is from the lower id's point of view; statistics are shown from A's.
    if (pair && match_) pending_stats_.push_back(id_a_ < id_b_ ? *pair : ~*pair);
}

void GuiObserver::addMove(EngineTotals& t, int64_t depth, int64_t time_ms, int64_t nps, uint64_t nodes,
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
    if (nodes > 0) {
        t.nodes_sum += nodes;
        t.nodes_samples++;
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

bool GuiObserver::loading() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return loading_;
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

std::vector<std::pair<std::string, EngineTotals>> GuiObserver::namedTotals() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::vector<std::pair<std::string, EngineTotals>> out;
    for (std::size_t i = 0; i < names_.size() && i < totals_.size(); ++i) out.emplace_back(names_[i], totals_[i]);
    return out;
}

}  // namespace fcg
