// fastchess' mock engine (app/tests/mock/engine/random_mover.cpp), with its score before the pv: after "pv" every
// word is a move, so fastchess found no score and warned at every move of every test game.
//
// With FCG_MOCK_STDERR=TEXT it writes "TEXT <move>" on its error output at each move (followed by
// FCG_MOCK_STDERR_PAD dots, when set), and with FCG_MOCK_STDERR_FLOOD=N, N lines of 1000 characters there before its
// uciok: from 70 on, more than a pipe holds. With FCG_MOCK_STDERR_CRASH=N, at its first "go" it writes a line on its
// output, then at once N lines of 1000 characters on its error output ("crash report I/N"), and ends.
//
// With FCG_MOCK_SLOW_UCIOK=MS it waits MS milliseconds before its uciok, and with FCG_MOCK_SLOW_READY=MS before
// each readyok: an engine slow to load its network or its hash table. With FCG_MOCK_MOVE_MS=MS it thinks MS
// milliseconds over each move, after its info line: games that last, for screenshots of the live view.
//
// With FCG_MOCK_AFTER_BESTMOVE=TEXT it writes its info line, its bestmove, "info string TEXT" and a readyok (as for
// an isready received during its search) in one write: what a reader that stops at the bestmove would lose.
//
// With FCG_MOCK_SEARCH=1 it writes two info lines as a search would, with a seldepth, nodes, nps, a time and a PV of
// two moves, the first line on another move: every chart of the Games tab has something to draw.
//
// With FCG_MOCK_ANALYSIS=1 it also has the options Threads, MultiPV and UCI_ShowWDL, and searches as an analysis
// engine does: "go infinite" writes info lines for depths 1 to 8, one per MultiPV line (the first legal moves in
// order, the best first), with a WDL when asked, and gives its move only at "stop"; "go movetime" and "go depth" do
// the same, then give it at once.

#include <chrono>
#include <cstdlib>
#include <random>
#include <thread>

#include <chess.hpp>

using namespace chess;

std::mt19937 gen;

bool analysis_mode() { return std::getenv("FCG_MOCK_ANALYSIS") != nullptr; }
int multipv     = 1;
bool show_wdl   = false;
bool searching  = false;  // an infinite search, waiting for "stop"
Move infinite_best;

void initialize_rng(int argc, char *argv[]) {
    if (argc > 1) {
        std::cout << "Using provided seed: " << argv[1] << std::endl;
        unsigned int seed = std::stoi(argv[1]);
        gen.seed(seed);
    } else {
        std::random_device rd;
        auto seed = rd();
        gen.seed(seed);
        std::cout << "Using random seed: " << seed << std::endl;
    }
}

int random_number(int min, int max) {
    std::uniform_int_distribution<> dis(min, max);
    return dis(gen);
}

Move random_move(const Board &board) {
    auto moves = Movelist();
    movegen::legalmoves(moves, board);

    return moves[random_number(0, moves.size() - 1)];
}

void uci_line(Board &board, const std::string &line) {
    auto tokens = utils::splitString(line, ' ');

    if (tokens.empty())
        return;
    else if (tokens[0] == "position") {
        // parse position
        if (tokens[1] == "startpos") {
            board = Board();
            if (tokens.size() > 2 && tokens[2] == "moves") {
                for (int i = 3; i < tokens.size(); i++) {
                    board.makeMove(uci::uciToMove(board, std::string(tokens[i])));
                }
            }
        } else if (tokens[1] == "fen") {
            std::string fen = "";
            // parse until "moves"
            int i = 2;
            for (; i < tokens.size(); i++) {
                if (tokens[i] == "moves") {
                    i++;
                    break;
                }
                fen += std::string(tokens[i]) + " ";
            }

            board.setFen(fen);

            for (; i < tokens.size(); i++) {
                board.makeMove(uci::uciToMove(board, std::string(tokens[i])));
            }
        }
    } else if (tokens[0] == "uci") {
        if (const char *flood = std::getenv("FCG_MOCK_STDERR_FLOOD")) {
            for (int i = 0, n = std::atoi(flood); i < n; ++i) std::cerr << "flood " << std::string(1000, '.') << '\n';
            std::cerr << std::flush;
        }
        std::cout << "id name random_move" << std::endl;
        std::cout << "id author fastchess" << std::endl;
        std::cout << "option name Hash type spin default 16 min 1 max 33554432" << std::endl;
        if (analysis_mode()) {
            std::cout << "option name Threads type spin default 1 min 1 max 1024" << std::endl;
            std::cout << "option name MultiPV type spin default 1 min 1 max 256" << std::endl;
            std::cout << "option name UCI_ShowWDL type check default false" << std::endl;
        }
        if (const char *slow = std::getenv("FCG_MOCK_SLOW_UCIOK"))
            std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(slow)));
        std::cout << "uciok" << std::endl;
    } else if (tokens[0] == "isready") {
        if (const char *slow = std::getenv("FCG_MOCK_SLOW_READY"))
            std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(slow)));
        std::cout << "readyok" << std::endl;
    } else if (tokens[0] == "ucinewgame") {
        board = Board();
    } else if (tokens[0] == "setoption" && tokens.size() >= 5 && tokens[1] == "name") {
        if (tokens[2] == "MultiPV") multipv = std::max(1, std::atoi(std::string(tokens[4]).c_str()));
        if (tokens[2] == "UCI_ShowWDL") show_wdl = tokens[4] == "true";
    } else if (tokens[0] == "stop") {
        if (searching) {
            searching = false;
            std::cout << "bestmove " << uci::moveToUci(infinite_best) << std::endl;
        }
    } else if (tokens[0] == "go" && analysis_mode() && tokens.size() > 1 &&
               (tokens[1] == "infinite" || tokens[1] == "movetime" || tokens[1] == "depth")) {
        auto moves = Movelist();
        movegen::legalmoves(moves, board);
        if (moves.empty()) {
            std::cout << "info depth 0 score " << (board.inCheck() ? "mate 0" : "cp 0") << std::endl;
            std::cout << "bestmove (none)" << std::endl;
            return;
        }
        const int lines = std::min<int>(multipv, moves.size());
        for (int depth = 1; depth <= 8; ++depth) {
            for (int i = 0; i < lines; ++i) {
                Board after = board;
                after.makeMove(moves[i]);
                auto replies = Movelist();
                movegen::legalmoves(replies, after);
                std::cout << "info depth " << depth << " seldepth " << depth + 2 << " multipv " << i + 1
                          << " score cp " << 40 - 15 * i + depth;
                if (show_wdl) std::cout << " wdl " << 300 - 20 * i << " 600 " << 100 + 20 * i;
                std::cout << " nodes " << 1000 * depth << " nps 500000 time " << 2 * depth << " pv "
                          << uci::moveToUci(moves[i]);
                if (!replies.empty()) std::cout << " " << uci::moveToUci(replies[0]);
                std::cout << "\n";
            }
        }
        std::cout << std::flush;
        infinite_best = moves[0];
        if (tokens[1] == "infinite") {
            searching = true;
            return;
        }
        std::cout << "bestmove " << uci::moveToUci(infinite_best) << std::endl;
    } else if (tokens[0] == "go") {
        if (const char *crash = std::getenv("FCG_MOCK_STDERR_CRASH")) {
            std::cout << "info string crashing" << std::endl;
            std::string report;
            for (int i = 0, n = std::atoi(crash); i < n; ++i)
                report += "crash report " + std::to_string(i + 1) + "/" + std::to_string(n) + " " +
                          std::string(1000, '.') + '\n';
            std::cerr << report << std::flush;
            std::_Exit(3);
        }
        const auto move = random_move(board);
        if (const char *text = std::getenv("FCG_MOCK_STDERR")) {
            const char *pad = std::getenv("FCG_MOCK_STDERR_PAD");
            std::cerr << text << " " << uci::moveToUci(move) << " " << std::string(pad ? std::atoi(pad) : 0, '.')
                      << std::endl;
        }
        if (const char *after = std::getenv("FCG_MOCK_AFTER_BESTMOVE")) {
            std::cout << "info depth 1 score cp 0 pv " << uci::moveToUci(move) << "\nbestmove " << uci::moveToUci(move)
                      << "\ninfo string " << after << "\nreadyok\n"
                      << std::flush;
            return;
        }
        if (std::getenv("FCG_MOCK_SEARCH")) {
            const int depth   = random_number(6, 16);
            const int ms      = random_number(5, 40);
            const long nodes  = 3000L * depth * random_number(8, 12);
            const auto before = random_move(board);
            Board after       = board;
            after.makeMove(move);
            // A reply only while the game goes on: fastchess warns about a PV that continues after its end.
            auto replies = Movelist();
            if (after.isGameOver().second == GameResult::NONE) movegen::legalmoves(replies, after);
            const std::string reply =
                replies.empty() ? "" : " " + uci::moveToUci(replies[random_number(0, replies.size() - 1)]);
            std::cout << "info depth " << depth - 1 << " seldepth " << depth + 2 << " score cp 10 nodes " << nodes / 3
                      << " nps " << nodes / 3 * 1000 / (ms / 2 + 1) << " time " << ms / 2 << " pv "
                      << uci::moveToUci(before) << "\n"
                      << "info depth " << depth << " seldepth " << depth + random_number(2, 8) << " score cp 10 nodes "
                      << nodes << " nps " << nodes * 1000 / ms << " time " << ms << " pv " << uci::moveToUci(move)
                      << reply << std::endl;
        } else {
            std::cout << "info depth 1 score cp 0 pv " << uci::moveToUci(move) << std::endl;
        }
        if (const char *think = std::getenv("FCG_MOCK_MOVE_MS"))
            std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(think)));
        std::cout << "bestmove " << uci::moveToUci(move) << std::endl;
    }
}

void uci_loop() {
    std::string input;
    std::cin >> std::ws;

    Board board = Board();

    while (true) {
        if (!std::getline(std::cin, input) && std::cin.eof()) {
            input = "quit";
        }

        if (input == "quit") {
            return;
        } else {
            uci_line(board, input);
        }
    }
}

int main(int argc, char *argv[]) {
    initialize_rng(argc, argv);
    uci_loop();
}
