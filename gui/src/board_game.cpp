#include "board_game.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <deque>

#include <engine/uci_engine.hpp>
#include <tour/engine_text.hpp>
#include <tour/pgn_file.hpp>

#include <fmt/include/fmt/format.h>

#include "board_text.hpp"
#include "engines.hpp"
#include "i18n.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

// An engine playing a game, spoken to in UCI (a WinBoard engine through the adapter), from the game's thread.
class Player {
   public:
    Player(const EngineEntry& entry, const std::atomic<bool>& stop) : entry_(entry), stop_(stop) {}

    // Started, given the library's option values (and Chess960 when asked), and ready. False with the reason.
    bool start(bool chess960, std::string& error) {
        std::string why;
        if (!process_.start(engineLaunch(entry_), why)) {
            error = trf("Cannot start {}: {}", entry_.name, toUtf8(why));
            return false;
        }
        const auto timeout = entry_.start_timeout > 0
                                 ? std::chrono::milliseconds(entry_.start_timeout * 1000LL)
                                 : std::chrono::duration_cast<std::chrono::milliseconds>(ENGINE_STARTUP_TIME);
        (void)process_.send("uci");
        std::vector<UciOptionInfo> options;
        if (!waitFor("uciok", Clock::now() + timeout, [&](const std::string& line) {
                if (auto o = parseUciOption(line)) options.push_back(std::move(*o));
            })) {
            error = trf("{} does not answer uci: is it a UCI engine?", entry_.name);
            return false;
        }
        for (const auto& [name, value] : tour::forEngine(entry_.options)) {
            const auto it = std::find_if(options.begin(), options.end(), [&](const auto& o) { return o.name == name; });
            if (it == options.end() || it->type == "button") continue;
            (void)process_.send("setoption name " + name + " value " + value);
        }
        if (chess960 &&
            std::any_of(options.begin(), options.end(), [](const auto& o) { return o.name == "UCI_Chess960"; }))
            (void)process_.send("setoption name UCI_Chess960 value true");
        if (!ready(timeout)) {
            error = trf("{} does not answer isready.", entry_.name);
            return false;
        }
        return true;
    }

    bool newGame() {
        (void)process_.send("ucinewgame");
        return ready(std::chrono::seconds(60));
    }

    // Its move for the position, within `budget` (its clock and the margin): the move in UCI, or nullopt (out of
    // time, stopped, or the engine ended). `info` gets each info line with a score.
    std::optional<std::string> go(const std::string& position, const std::string& go, std::chrono::milliseconds budget,
                                  const std::function<void(const std::string&)>& info) {
        (void)process_.send(position);
        (void)process_.send(go);
        std::optional<std::string> move;
        const bool got = waitFor(
            "bestmove", Clock::now() + budget,
            [&](const std::string& line) {
                if (line.rfind("info", 0) == 0 && line.find(" score ") != std::string::npos) info(line);
            },
            &move);
        if (!got) return std::nullopt;
        return move;
    }

    [[nodiscard]] bool alive() const { return process_.running(); }
    void quit() { process_.stop(std::chrono::milliseconds(500)); }

   private:
    bool ready(std::chrono::milliseconds timeout) {
        (void)process_.send("isready");
        return waitFor("readyok", Clock::now() + timeout, [](const std::string&) {});
    }

    // Reads its lines until one starting with `word`, passing the others to `each`; `move` gets the word's first
    // argument (bestmove e2e4). False on the deadline, a stop or the engine's end.
    template <typename F>
    bool waitFor(std::string_view word, Clock::time_point deadline, F each,
                 std::optional<std::string>* move = nullptr) {
        while (true) {
            for (auto& line : process_.take()) {
                if (line.kind != EngineProcess::Line::Kind::Out) continue;
                const auto& text = line.text;
                if (text.rfind(word, 0) == 0 && (text.size() == word.size() || text[word.size()] == ' ')) {
                    if (move) {
                        auto rest = text.substr(std::min(text.size(), word.size() + 1));
                        *move     = rest.substr(0, rest.find(' '));
                    }
                    return true;
                }
                each(text);
            }
            if (stop_ || !process_.running() || Clock::now() >= deadline) return false;
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            process_.wait(std::clamp(left, std::chrono::milliseconds(1), std::chrono::milliseconds(50)));
        }
    }

    const EngineEntry& entry_;
    const std::atomic<bool>& stop_;
    EngineProcess process_;
};

std::string today() {
    const auto now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[16];
    std::strftime(text, sizeof(text), "%Y.%m.%d", &local);
    return text;
}

// The PGN TimeControl of a clock: "60+0.6".
std::string pgnTimeControl(const GameClock& c) { return fmt::format("{:g}+{:g}", c.base_s, c.inc_s); }

}  // namespace

std::string clockText(const GameClock& c) {
    if (c.base_s >= 60 && std::fmod(c.base_s, 60.0) == 0) return fmt::format("{:g}+{:g}", c.base_s / 60, c.inc_s);
    const auto whole = static_cast<int>(c.base_s);
    return fmt::format("{}:{:02}+{:g}", whole / 60, whole % 60, c.inc_s);
}

template <typename F>
void BoardMatch::update(F f) {
    std::lock_guard lock(mutex_);
    f(state_);
    ++state_.version;
}

bool BoardMatch::start(const EngineEntry& first, const EngineEntry& second, const BoardMatchSetup& setup,
                       const std::filesystem::path& pgn, std::string& error) {
    stop();
    for (const auto* e : {&first, &second}) {
        std::error_code ec;
        if (e->machine.empty() && !std::filesystem::is_regular_file(e->cmd, ec)) {
            error = trf("File not found: {}", e->cmd);
            return false;
        }
    }
    {
        std::lock_guard lock(mutex_);
        const auto version = state_.version;
        state_             = State{};
        state_.version     = version + 1;
        state_.games       = setup.pair ? 2 : 1;
        state_.running     = true;
        state_.pgn         = pgn;
    }
    stop_ = false;
    claim_.set(std::max(first.threads(), second.threads()));
    thread_ = std::thread([this, first, second, setup, pgn] { run(first, second, setup, pgn); });
    return true;
}

void BoardMatch::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    claim_.set(0);
}

bool BoardMatch::state(State& out, uint64_t& version) const {
    std::lock_guard lock(mutex_);
    if (state_.version == version) return false;
    out     = state_;
    version = state_.version;
    return true;
}

bool BoardMatch::running() const {
    std::lock_guard lock(mutex_);
    return state_.running;
}

void BoardMatch::run(EngineEntry first, EngineEntry second, BoardMatchSetup setup, std::filesystem::path pgn) {
    Player one(first, stop_), two(second, stop_);
    std::string error;
    if (!one.start(setup.chess960, error) || !two.start(setup.chess960, error)) {
        update([&](State& s) {
            s.error   = stop_ ? std::string() : error;
            s.running = false;
        });
        one.quit();
        two.quit();
        return;
    }

    const int games = setup.pair ? 2 : 1;
    for (int g = 0; g < games && !stop_; ++g) {
        // The first engine has White in the first game, Black in the second.
        Player& white             = g == 0 ? one : two;
        Player& black             = g == 0 ? two : one;
        const auto& white_name    = g == 0 ? first.name : second.name;
        const auto& black_name    = g == 0 ? second.name : first.name;
        const GameClock clocks[2] = {g == 0 ? setup.first_clock : setup.second_clock,
                                     g == 0 ? setup.second_clock : setup.first_clock};

        ReplayGame game;
        game.start_fen = setup.fen;
        game.headers   = {{"Event", "fastchess-gui board game"}, {"Site", "?"},         {"Date", today()},
                          {"Round", std::to_string(g + 1)},      {"White", white_name}, {"Black", black_name}};
        if (pgnTimeControl(clocks[0]) == pgnTimeControl(clocks[1])) {
            game.headers.emplace_back("TimeControl", pgnTimeControl(clocks[0]));
        } else {
            game.headers.emplace_back("WhiteTimeControl", pgnTimeControl(clocks[0]));
            game.headers.emplace_back("BlackTimeControl", pgnTimeControl(clocks[1]));
        }
        std::array<int64_t, 2> clock_ms     = {static_cast<int64_t>(clocks[0].base_s * 1000),
                                               static_cast<int64_t>(clocks[1].base_s * 1000)};
        const std::array<int64_t, 2> inc_ms = {static_cast<int64_t>(clocks[0].inc_s * 1000),
                                               static_cast<int64_t>(clocks[1].inc_s * 1000)};
        update([&](State& s) {
            s.game     = g;
            s.current  = game;
            s.clock_ms = clock_ms;
            s.thinking.reset();
            s.info.clear();
        });

        std::string result = "*", reason;
        if (!white.newGame() || !black.newGame()) {
            reason = TR("an engine does not answer isready");
            stop_  = true;
        }

        chess::Board board;
        board.set960(setup.chess960);
        board.setFen(setup.fen);
        std::vector<std::string> moves;
        while (!stop_) {
            const auto [why, outcome] = board.isGameOver();
            if (outcome != chess::GameResult::NONE) {
                const bool mated = why == chess::GameResultReason::CHECKMATE;
                result           = mated ? (board.sideToMove() == chess::Color::WHITE ? "0-1" : "1-0") : "1/2-1/2";
                reason           = mated                                                   ? TR("checkmate")
                                   : why == chess::GameResultReason::STALEMATE             ? TR("stalemate")
                                   : why == chess::GameResultReason::INSUFFICIENT_MATERIAL ? TR("insufficient material")
                                   : why == chess::GameResultReason::FIFTY_MOVE_RULE       ? TR("50-move rule")
                                                                                           : TR("threefold repetition");
                break;
            }
            if (moves.size() >= 1000) {
                result = "1/2-1/2";
                reason = TR("game too long");
                break;
            }
            const auto stm       = board.sideToMove();
            const int side       = stm == chess::Color::WHITE ? 0 : 1;
            Player& player       = side == 0 ? white : black;
            const auto& name     = side == 0 ? white_name : black_name;
            std::string position = "position fen " + setup.fen;
            if (!moves.empty()) {
                position += " moves";
                for (const auto& m : moves) position += " " + m;
            }
            const auto go    = fmt::format("go wtime {} btime {} winc {} binc {}", std::max<int64_t>(1, clock_ms[0]),
                                           std::max<int64_t>(1, clock_ms[1]), inc_ms[0], inc_ms[1]);
            const auto start = Clock::now();
            update([&](State& s) {
                s.thinking    = stm;
                s.think_start = start;
                s.info.clear();
            });
            std::string last_info;
            const auto move  = player.go(position, go, std::chrono::milliseconds(clock_ms[side]) + CLOCK_MARGIN,
                                         [&](const std::string& line) {
                                            last_info = line;
                                            update([&](State& s) { s.info = line; });
                                         });
            const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
            if (stop_) break;
            const char* winner = side == 0 ? "0-1" : "1-0";
            if (!move) {
                result = winner;
                reason = player.alive() ? trf("{} loses on time", name) : trf("{}'s engine stopped", name);
                break;
            }
            if (spent > clock_ms[side] + CLOCK_MARGIN.count()) {
                result = winner;
                reason = trf("{} loses on time", name);
                break;
            }
            const auto parsed = chess::uci::uciToMove(board, *move);
            chess::Movelist legal;
            chess::movegen::legalmoves(legal, board);
            if (parsed == chess::Move::NO_MOVE || std::find(legal.begin(), legal.end(), parsed) == legal.end()) {
                result = winner;
                reason = trf("illegal move {} by {}", *move, name);
                break;
            }
            clock_ms[side] = clock_ms[side] - spent + inc_ms[side];

            ReplayMove played;
            played.move    = parsed;
            played.mover   = stm;
            played.san     = chess::uci::moveToSan(board, parsed);
            played.time_ms = spent;
            if (!last_info.empty()) {
                const auto info = fastchess::engine::UciEngine::parseInfo(last_info);
                if (info.score) played.score = *info.score;
                played.depth = static_cast<int>(info.depth.value_or(0));
                if (info.nodes) played.nodes = *info.nodes;
                if (info.wdl) played.wdl = *info.wdl;
            }
            moves.push_back(chess::uci::moveToUci(parsed, setup.chess960));
            board.makeMove<true>(parsed);
            game.moves.push_back(played);
            update([&](State& s) {
                s.current  = game;
                s.clock_ms = clock_ms;
                s.thinking.reset();
            });
        }

        game.result        = result;
        game.final_comment = stop_ && reason.empty() ? std::string(TR("stopped")) : reason;
        if (!game.final_comment.empty()) game.headers.emplace_back("Termination", game.final_comment);
        std::string written;
        try {
            tour::appendShared(pgn, gamePgn(game, setup.chess960));
        } catch (const std::exception& e) {
            written = trf("Cannot write {}: {}", pgn.string(), e.what());
        }
        update([&](State& s) {
            s.current = game;
            s.finished.push_back(game);
            s.thinking.reset();
            if (!written.empty()) s.error = written;
        });
    }
    one.quit();
    two.quit();
    update([&](State& s) { s.running = false; });
}

void BoardMatch::reap() {
    if (running() || !thread_.joinable()) return;
    thread_.join();
    claim_.set(0);
}

}  // namespace fcg
