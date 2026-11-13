#include "book_adapter.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <fmt/include/fmt/format.h>

#ifdef _WIN64
#    include <engine/process/process_win.hpp>
#else
#    include <engine/process/process_posix.hpp>
#endif
#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

namespace fcg::tour {

namespace {

std::vector<std::string> split(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        const auto b = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i > b) out.emplace_back(s.substr(b, i - b));
    }
    return out;
}

// An argument of a command line, as fastchess' process classes and the C runtime of Windows split them.
std::string inQuotes(const std::string& s) { return "\"" + s + "\""; }

// splitmix64: spreads the bits of the seed, the game and the position over the generator's seed.
uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// A random number of this process: the processes of an engine started with the same seed (concurrent games,
// restarts) draw other book moves. std::random_device alone is not enough with some MinGW runtimes, where it is a
// fixed sequence: the clock, the process and where the stack lies (address space randomisation) are mixed in.
uint64_t processEntropy() {
    uint64_t x = 0;
    try {
        std::random_device device;
        x = (static_cast<uint64_t>(device()) << 32) ^ device();
    } catch (const std::exception&) {
    }
    const int on_stack = 0;
#ifdef _WIN32
    const uint64_t pid = GetCurrentProcessId();
#else
    const uint64_t pid = static_cast<uint64_t>(getpid());
#endif
    x = mix(x ^ static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    x = mix(x ^ pid ^ (static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(&on_stack)) << 16));
    return x;
}

}  // namespace

fastchess::EngineConfiguration throughBookAdapter(fastchess::EngineConfiguration engine, const std::string& book,
                                                  int depth, uint64_t seed) {
    if (book_adapter.empty()) throw std::runtime_error("an engine's own book needs fastchess-gui's book adapter");

    // The engine behind it: a WinBoard engine through the WinBoard adapter, which is fastchess-gui too.
    auto child = inQuotes(engine.cmd) + (engine.args.empty() ? "" : " " + engine.args);
    if (engine.protocol == "xboard") child = inQuotes(book_adapter) + " --wb2uci " + child;

    engine.args =
        fmt::format("--book-adapter --book {} --depth {} --seed {}{} -- {}", inQuotes(book), std::max(0, depth), seed,
                    engine.variant == fastchess::VariantType::FRC ? " --chess960" : "", child);
    engine.cmd      = book_adapter;
    engine.protocol = "uci";
    return engine;
}

void markOwnBookMoves(fastchess::MatchData& data, chess::Color side, PolyglotBook& book, int depth, bool chess960) {
    chess::Board board;
    board.set960(chess960);
    if (!board.setFen(data.fen)) return;
    for (std::size_t ply = 0; ply < data.moves.size(); ++ply) {
        auto& move   = data.moves[ply];
        const auto m = chess::uci::uciToMove(board, move.move);
        if (!move.legal || m == chess::Move::NO_MOVE) return;  // an illegal move ends the game
        if (board.sideToMove() == side && !move.book && withinBookDepth(ply, depth) && !book.moves(board).empty()) {
            move.book = true;
        }
        board.makeMove<true>(m);
    }
}

// ---------------------------------------------------------------------------
// The translation

BookAdapter::BookAdapter(Sink to_gui, Sink to_engine, std::unique_ptr<PolyglotBook> book, int depth, uint64_t seed,
                         bool chess960)
    : to_gui_(std::move(to_gui)),
      to_engine_(std::move(to_engine)),
      book_(std::move(book)),
      depth_(depth),
      seed_(seed),
      chess960_(chess960) {}

void BookAdapter::fromGui(const std::string& line) {
    const auto tokens = split(line);
    if (tokens.empty()) return;
    const auto& command = tokens[0];

    if (command == "go") {
        if (!searching_ && answerFromBook(tokens)) return;  // the engine never sees it
        searching_ = true;
    } else if (command == "stop" || command == "ponderhit") {
        // Only about a search the engine was given: after a book move it has none, and a stop could make some
        // engines answer with a move nobody asked for.
        if (!searching_) return;
    } else if (command == "position") {
        position(tokens);
    } else if (command == "ucinewgame") {
        ++games_;
        reseed_ = true;
    } else if (command == "setoption" && line.find("UCI_Chess960") != std::string::npos) {
        chess960_ = line.find("true") != std::string::npos;
    } else if (command == "quit") {
        quitting_ = true;
    }
    to_engine_(line);
}

void BookAdapter::fromEngine(const std::string& line) {
    if (line.rfind("bestmove", 0) == 0) searching_ = false;
    to_gui_(line);
}

void BookAdapter::position(const std::vector<std::string>& tokens) {
    known_ = false;
    std::string fen(chess::constants::STARTPOS);
    std::size_t i = 1;
    if (i < tokens.size() && tokens[i] == "startpos") {
        ++i;
    } else if (i < tokens.size() && tokens[i] == "fen") {
        fen.clear();
        for (++i; i < tokens.size() && tokens[i] != "moves"; ++i) fen += (fen.empty() ? "" : " ") + tokens[i];
    } else {
        return;
    }

    board_.set960(chess960_);
    if (!board_.setFen(fen)) return;

    // A new game: the choices it gets depend on the seed (with the process' random number in it), on how many games
    // came before it in this process and on where it starts, so that two games from the same opening get their own
    // lines.
    if (reseed_) {
        rng_.seed(mix(seed_ ^ mix(games_ ^ mix(board_.hash()))));
        reseed_ = false;
    }

    ply_ = 0;
    if (i < tokens.size() && tokens[i] == "moves") {
        for (++i; i < tokens.size(); ++i, ++ply_) {
            const auto move = chess::uci::uciToMove(board_, tokens[i]);
            if (move == chess::Move::NO_MOVE) return;
            board_.makeMove<true>(move);
        }
    }
    known_ = true;
}

bool BookAdapter::answerFromBook(const std::vector<std::string>& tokens) {
    if (!known_ || !book_ || !withinBookDepth(ply_, depth_)) return false;
    // A ponder search, or one until stop: the engine's own business.
    for (const auto& t : tokens) {
        if (t == "ponder" || t == "infinite") return false;
    }
    const auto move = book_->pick(board_, rng_);
    if (move == chess::Move::NO_MOVE) return false;
    to_gui_("bestmove " + chess::uci::moveToUci(move, chess960_));
    return true;
}

// ---------------------------------------------------------------------------
// The --book-adapter mode

int runBookAdapter(const std::vector<std::string>& args) {
    std::string book;
    int depth     = 0;
    uint64_t seed = 0;
    bool chess960 = false;
    std::size_t i = 0;
    for (; i < args.size() && args[i] != "--"; ++i) {
        const bool more = i + 1 < args.size();
        try {
            if (args[i] == "--book" && more) {
                book = args[++i];
            } else if (args[i] == "--depth" && more) {
                depth = std::stoi(args[++i]);
            } else if (args[i] == "--seed" && more) {
                seed = std::stoull(args[++i]);
            } else if (args[i] == "--chess960") {
                chess960 = true;
            }
        } catch (const std::exception&) {
        }
    }
    if (book.empty() || i + 1 >= args.size()) {
        std::fprintf(stderr,
                     "usage: fastchess-gui --book-adapter --book FILE [--depth PLIES] [--seed N] [--chess960] -- "
                     "ENGINE [ARGS...]\n");
        return 2;
    }

#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);  // a write to an engine that just died
#endif

    // A book that cannot be read stops the engine, rather than letting it play its book moves itself: the games
    // would not be those the tournament stands for. fastchess reports the engine's failure to start.
    std::unique_ptr<PolyglotBook> polyglot;
    try {
        polyglot = std::make_unique<PolyglotBook>(book);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }

    const auto& cmd = args[i + 1];
    std::string engine_args;
    for (auto k = i + 2; k < args.size(); ++k) {
        const bool quote = args[k].empty() || args[k].find_first_of(" \t") != std::string::npos;
        engine_args += (k > i + 2 ? " " : "") + (quote ? inQuotes(args[k]) : args[k]);
    }

    std::mutex mutex;
    const auto to_gui = [&](const std::string& line) {
        std::fputs((line + "\n").c_str(), stdout);
        std::fflush(stdout);
    };

    fastchess::engine::process::Process process;
    process.setRealtimeLogging(false);
    BookAdapter adapter(
        to_gui, [&](const std::string& line) { (void)process.writeInput(line + "\n"); }, std::move(polyglot), depth,
        mix(seed) ^ processEntropy(), chess960);

    process.setLineCallback([&](const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex);
        adapter.fromEngine(line);
    });
#ifdef _WIN32
    // Windows would mix the engine's error output with its output: it is read apart and passed on as ours, which
    // the GUI keeps in the engine log. Elsewhere the engine writes it straight to ours.
    fastchess::engine::process::capture_stderr = true;
    process.setErrorLineCallback([&](const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex);
        std::fputs((line + "\n").c_str(), stderr);
        std::fflush(stderr);
    });
#endif
    // The engine runs in our working directory, which fastchess set to the engine's.
    if (auto started = process.init("", cmd, engine_args, "book-adapter"); !started) {
        std::fprintf(stderr, "cannot start %s: %s\n", cmd.c_str(), started.message.c_str());
        return 1;
    }

    // The engine's output; when it ends, so does the adapter, and the GUI sees the engine stop.
    std::thread([&] {
        std::vector<fastchess::engine::process::Line> lines;
        while (true) {
            // "bestmove" only bounds how many lines accumulate; each line goes through the callback.
            const auto r = process.readOutput(lines, "bestmove", std::chrono::milliseconds(0));
            if (r.code == fastchess::engine::process::Status::ERR) break;
        }
        bool quitting = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            quitting = adapter.quitting();
        }
        std::fflush(stdout);
        std::_Exit(quitting ? 0 : 1);
    }).detach();

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::lock_guard<std::mutex> lock(mutex);
        adapter.fromGui(line);
        if (adapter.quitting()) break;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!adapter.quitting()) adapter.fromGui("quit");
    }

    // A moment for the engine to quit, then it is killed.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < until && process.alive()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    process.forceKill();
    std::fflush(stdout);
    std::_Exit(0);
}

}  // namespace fcg::tour
