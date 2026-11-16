#include "review_runner.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

#include <chess.hpp>
#include <tour/engine_text.hpp>

#include "engines.hpp"
#include "i18n.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

chess::Board boardOf(const AnalysisPosition& position) {
    chess::Board board;
    board.set960(position.chess960);
    board.setFen(position.fen);
    for (const auto& m : position.moves) board.makeMove(chess::uci::uciToMove(board, m));
    return board;
}

}  // namespace

int reviewPasses(int64_t ceiling_ms) {
    if (ceiling_ms <= 0) return 13;
    int n = 0;
    while (n < 13 && passTime(n) <= ceiling_ms) ++n;
    return std::max(1, n);
}

std::vector<AnalysisPosition> pvPositions(const AnalysisPosition& position, const std::string& pv, std::size_t max) {
    std::vector<AnalysisPosition> out;
    std::vector<std::string> moves;
    std::istringstream in(pv);
    for (std::string m; in >> m && moves.size() < max + 1;) moves.push_back(m);
    if (moves.size() < 2) return out;
    auto board = boardOf(position);
    auto at    = position;
    for (std::size_t k = 0; k + 1 < moves.size() && out.size() < max; ++k) {
        const auto move = chess::uci::uciToMove(board, moves[k]);
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        if (std::find(legal.begin(), legal.end(), move) == legal.end()) break;
        board.makeMove(move);
        at.moves.push_back(moves[k]);
        out.push_back(at);
        if (board.isGameOver().second != chess::GameResult::NONE) break;
    }
    return out;
}

void ReviewRunner::start(Setup setup) {
    stop();
    stop_       = false;
    paused_     = false;
    pass_limit_ = 1 << 20;
    first_   = -1;
    ceiling_ = setup.ceiling_ms;
    std::vector<AnalysisPosition> positions;
    {
        AnalysisPosition position;
        position.chess960 = setup.chess960;
        chess::Board board;
        board.set960(setup.chess960);
        if (!setup.game.start_fen.empty()) board.setFen(setup.game.start_fen);
        position.fen = board.getFen();
        positions.push_back(position);
        for (const auto& m : setup.game.moves) {
            position.moves.push_back(chess::uci::moveToUci(m.move, setup.chess960));
            positions.push_back(position);
        }
    }
    {
        std::lock_guard lock(mutex_);
        const auto n = positions.size();
        state_       = {};
        state_.version++;
        state_.running        = true;
        state_.pass           = setup.resume.passes;
        state_.settled        = setup.resume;
        state_.settled.engine = setup.engine.name;
        state_.live           = state_.settled;
        state_.live.positions.resize(n);
        state_.level.assign(n, state_.pass - 1);
        state_.done.assign(n, false);
        offers_.clear();
    }
    thread_ = std::thread([this, setup = std::move(setup), positions = std::move(positions)]() mutable {
        run(std::move(setup), std::move(positions));
    });
}

void ReviewRunner::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    claim_.set(0);
    std::lock_guard lock(mutex_);
    if (state_.running) {
        state_.running = false;
        state_.version++;
    }
}

void ReviewRunner::offer(int ply, PositionEval value) {
    std::lock_guard lock(mutex_);
    std::erase_if(offers_, [&](const auto& o) { return o.first == ply; });
    offers_.emplace_back(ply, std::move(value));
}

bool ReviewRunner::state(State& out, uint64_t& version) const {
    std::lock_guard lock(mutex_);
    if (state_.version == version) return false;
    out     = state_;
    version = state_.version;
    return true;
}

bool ReviewRunner::running() const {
    std::lock_guard lock(mutex_);
    return state_.running;
}

template <typename F>
void ReviewRunner::update(F f) {
    std::lock_guard lock(mutex_);
    f(state_);
    state_.version++;
}

void ReviewRunner::run(Setup setup, std::vector<AnalysisPosition> positions) {
    const auto fail = [&](std::string message) {
        claim_.set(0);
        update([&](State& s) {
            s.error   = std::move(message);
            s.running = false;
        });
    };
    EngineProcess process;
    std::string why;
    if (!process.start(engineLaunch(setup.engine), why)) {
        fail(trf("Cannot start {}: {}", setup.engine.name, toUtf8(why)));
        return;
    }
    // Reads the engine's lines until one starting with `word`, passing the others to `each`. False on the deadline, a
    // stop asked (when `stoppable`) or the engine's end.
    const auto wait_for = [&](std::string_view word, Clock::time_point deadline, const auto& each, std::string* rest) {
        while (true) {
            for (auto& line : process.take()) {
                if (line.kind != EngineProcess::Line::Kind::Out) continue;
                const auto& text = line.text;
                if (text.rfind(word, 0) == 0 && (text.size() == word.size() || text[word.size()] == ' ')) {
                    if (rest) *rest = text.substr(std::min(text.size(), word.size() + 1));
                    return true;
                }
                each(text);
            }
            if (!process.running() || Clock::now() >= deadline) return false;
            process.wait(std::chrono::milliseconds(20));
        }
    };
    const auto startup = setup.engine.start_timeout > 0
                             ? std::chrono::milliseconds(setup.engine.start_timeout * 1000LL)
                             : std::chrono::duration_cast<std::chrono::milliseconds>(ENGINE_STARTUP_TIME);
    std::vector<UciOptionInfo> options;
    (void)process.send("uci");
    if (!wait_for(
            "uciok", Clock::now() + startup,
            [&](const std::string& line) {
                if (auto o = parseUciOption(line)) options.push_back(std::move(*o));
            },
            nullptr)) {
        fail(trf("{} does not answer uci: is it a UCI engine?", setup.engine.name));
        return;
    }
    const auto has = [&](const std::string& name) {
        return std::any_of(options.begin(), options.end(), [&](const auto& o) { return o.name == name; });
    };
    const auto set = [&](const std::string& name, const std::string& value) {
        if (has(name)) (void)process.send("setoption name " + name + " value " + value);
    };
    // The library's values, as a game sends them, buttons aside; then the analysis's own.
    for (const auto& [name, value] : tour::forEngine(setup.engine.options)) {
        if (name == "Threads" || name == "Hash" || name == "MultiPV" || name == "UCI_AnalyseMode") continue;
        const auto it = std::find_if(options.begin(), options.end(), [&](const auto& o) { return o.name == name; });
        if (it == options.end() || it->type == "button") continue;
        set(name, value);
    }
    set("UCI_ShowWDL", "true");
    set("UCI_AnalyseMode", "true");
    set("Threads", std::to_string(std::max(1, setup.settings.threads)));
    if (setup.settings.hash > 0) set("Hash", std::to_string(setup.settings.hash));
    set("MultiPV", "1");
    if (setup.chess960) set("UCI_Chess960", "true");
    const auto ready = [&] {
        (void)process.send("isready");
        return wait_for("readyok", Clock::now() + startup, [](const std::string&) {}, nullptr);
    };
    (void)process.send("ucinewgame");
    if (!ready()) {
        fail(trf("{} does not answer isready.", setup.engine.name));
        return;
    }
    const int threads = has("Threads") ? std::max(1, setup.settings.threads) : 1;

    // One search: false when stopped, paused (the search not finished) or the engine failed.
    const auto search = [&](const AnalysisPosition& position, int64_t ms, PositionEval* out) {
        (void)process.send(positionCommand(position));
        (void)process.send(goCommand(SearchLimit{std::max<int64_t>(1, ms), 0}));
        claim_.set(threads);
        std::vector<AnalysisLine> lines;
        AnalysisLine exact;  // the last line without a bound: a fail high or low has a PV of one move
        bool has_exact = false;
        std::string rest;
        const auto deadline = Clock::now() + std::chrono::milliseconds(ms) + std::chrono::seconds(5);
        bool interrupted    = false;
        bool got            = false;
        while (true) {
            got = wait_for(
                "bestmove", std::min(deadline, Clock::now() + std::chrono::milliseconds(50)),
                [&](const std::string& line) {
                    if (takeInfoLine(lines, line, 1) && !lines.empty() && !lines.front().lowerbound &&
                        !lines.front().upperbound && !lines.front().pv.empty()) {
                        exact     = lines.front();
                        has_exact = true;
                    }
                },
                &rest);
            if (got || !process.running()) break;
            if ((stop_ || paused_) && !interrupted) {
                (void)process.send("stop");
                interrupted = true;
            }
            if (Clock::now() >= deadline) {
                if (!interrupted) {
                    (void)process.send("stop");
                    interrupted = true;
                    continue;
                }
                break;
            }
        }
        claim_.set(0);
        if (!got) {
            fail(trf("{} did not give its move in time.", setup.engine.name));
            return false;
        }
        if (interrupted && (stop_ || paused_)) return false;
        if (out) {
            *out         = {};
            out->best    = rest.substr(0, rest.find(' '));
            out->time_ms = ms;
            if (has_exact || !lines.empty()) {
                const auto& line = has_exact ? exact : lines.front();
                out->score       = line.score;
                out->depth       = static_cast<int>(line.depth);
                out->wdl         = line.wdl;
                for (const auto& m : line.pv) out->pv += (out->pv.empty() ? "" : " ") + m;
            }
        }
        return true;
    };

    const int n = static_cast<int>(positions.size());
    std::vector<bool> over(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        over[static_cast<std::size_t>(i)] = endScore(boardOf(positions[static_cast<std::size_t>(i)])).has_value();

    while (!stop_) {
        // What the board's analysis searched for longer, taken also while it pauses the analysis.
        int pass = 0;
        std::vector<bool> done;
        std::string previous_pv;
        int next = -1;
        {
            std::lock_guard lock(mutex_);
            pass = state_.pass;
            for (auto& [ply, value] : offers_) {
                if (ply < 0 || ply >= n) continue;
                auto& live = state_.live.positions[static_cast<std::size_t>(ply)];
                if (value.time_ms <= live.time_ms) continue;
                int level = 0;
                while (level < 12 && passTime(level + 1) <= value.time_ms) ++level;
                live = std::move(value);
                state_.level[static_cast<std::size_t>(ply)] =
                    std::max(state_.level[static_cast<std::size_t>(ply)], level);
                if (live.time_ms >= passTime(pass)) state_.done[static_cast<std::size_t>(ply)] = true;
                state_.version++;
            }
            offers_.clear();
        }
        // Paused while the board's analysis has the machine, or until the other analyses caught up.
        if (paused_ || pass > pass_limit_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }
        {
            std::lock_guard lock(mutex_);
            // The positions the game ended in need no engine.
            for (int i = 0; i < n; ++i) {
                if (!over[static_cast<std::size_t>(i)] || state_.done[static_cast<std::size_t>(i)]) continue;
                state_.live.positions[static_cast<std::size_t>(i)] = {
                    endScore(boardOf(positions[static_cast<std::size_t>(i)])), "", 0, passTime(pass), "", std::nullopt};
                state_.level[static_cast<std::size_t>(i)] = pass;
                state_.done[static_cast<std::size_t>(i)]  = true;
            }
            // The board's position first, then the review's order.
            const int first = first_;
            if (first >= 0 && first < n && !state_.done[static_cast<std::size_t>(first)]) {
                next = first;
            } else {
                for (int k = 0; k < n && next < 0; ++k) {
                    const int p = REVIEW_BACKWARD ? n - 1 - k : k;
                    if (!state_.done[static_cast<std::size_t>(p)]) next = p;
                }
            }
            if (next >= 0) previous_pv = state_.live.positions[static_cast<std::size_t>(next)].pv;
        }

        if (next < 0) {
            // The pass is complete: it is what is kept and judged; the next one, unless it was the last.
            bool last = false;
            update([&](State& s) {
                const auto before     = s.settled.passes > 0 ? reviewMoves(setup.game, setup.chess960, s.settled)
                                                             : std::vector<MoveReview>{};
                s.settled.positions   = s.live.positions;
                s.settled.movetime_ms = passTime(s.pass);
                s.settled.depth       = 0;
                s.settled.passes      = s.pass + 1;
                s.settled.previous.clear();
                for (const auto& m : before) s.settled.previous.push_back(static_cast<int8_t>(m.judgement));
                s.pass++;
                s.done.assign(static_cast<std::size_t>(n), false);
                last = s.pass >= reviewPasses(ceiling_) || s.pass >= 13;
                if (last) s.running = false;
            });
            if (last) break;
            continue;
        }

        // The principal variation of the previous pass, from its end back to the position; then the position.
        const int64_t total = passTime(pass);
        int64_t main        = total;
        bool ok             = true;
        if (REVIEW_PV_REPLAY && pass > 0 && !previous_pv.empty()) {
            const auto line = pvPositions(positions[static_cast<std::size_t>(next)], previous_pv, 8);
            if (!line.empty()) {
                const int64_t share = std::max<int64_t>(5, total / 4 / static_cast<int64_t>(line.size()));
                for (auto it = line.rbegin(); it != line.rend() && ok; ++it) ok = search(*it, share, nullptr);
                main = std::max<int64_t>(FIRST_PASS_MS, total - share * static_cast<int64_t>(line.size()));
            }
        }
        PositionEval value;
        if (ok) ok = search(positions[static_cast<std::size_t>(next)], main, &value);
        if (!ok) {
            std::lock_guard lock(mutex_);
            if (!state_.error.empty()) break;  // the engine failed
            continue;                          // stopped or paused: searched again later
        }
        value.time_ms = total;
        update([&](State& s) {
            if (s.pass != pass) return;
            s.live.positions[static_cast<std::size_t>(next)] = std::move(value);
            s.level[static_cast<std::size_t>(next)]          = pass;
            s.done[static_cast<std::size_t>(next)]           = true;
        });
    }
    claim_.set(0);
    process.stop(std::chrono::milliseconds(500));
    update([](State& s) { s.running = false; });
}

}  // namespace fcg
