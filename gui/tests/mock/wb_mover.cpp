// A WinBoard engine for the tests: plays random legal moves through the xboard protocol.
//   wb_mover [v1|v2] [resign]
// v2 (default): protocol 2 features (ping, setboard, usermove, san), moves and PVs in SAN.
// v1: no features: positions through edit mode, moves in the old "1. ... e2e4" form, PVs in coordinates.
// resign: resigns instead of its sixth move.
// A move it cannot play is reported ("Illegal move") and ends the program, so that a test sees it.

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include "../../../app/third_party/chess.hpp"

using namespace chess;

namespace {

std::mt19937 gen(std::random_device{}());
bool v1     = false;
bool resign = false;

Board board;
bool force      = false;
Color engine_on = Color::BLACK;
int own_moves   = 0;

// Edit mode.
bool editing      = false;
Color edit_colour = Color::WHITE;

Move randomMove() {
    Movelist moves;
    movegen::legalmoves(moves, board);
    std::uniform_int_distribution<int> pick(0, moves.size() - 1);
    return moves[pick(gen)];
}

void think() {
    Movelist moves;
    movegen::legalmoves(moves, board);
    if (moves.empty()) return;
    if (resign && own_moves == 5) {
        std::cout << "resign" << std::endl;
        return;
    }
    const auto move = randomMove();
    const auto text = v1 ? uci::moveToUci(move) : uci::moveToSan(board, move);
    std::cout << "1 12 0 5 " << text << std::endl;  // ply score time nodes pv
    if (v1) {
        std::cout << (board.fullMoveNumber()) << ". ... " << text << std::endl;
    } else {
        std::cout << "move " << text << std::endl;
    }
    board.makeMove(move);
    ++own_moves;  // it keeps playing the same colour
}

bool applyMove(const std::string& text) {
    Movelist moves;
    movegen::legalmoves(moves, board);
    Move move = Move::NO_MOVE;
    try {
        move = uci::isUciMove(text) ? uci::uciToMove(board, text) : uci::parseSan(board, text);
    } catch (...) {
    }
    bool legal = false;
    for (const auto m : moves) legal |= m == move;
    if (!legal) {
        std::cout << "Illegal move: " << text << std::endl;
        std::exit(3);
    }
    board.makeMove(move);
    return true;
}

std::string grid(64, '.');  // a8..h1, the edit mode board
Color edit_stm = Color::WHITE;

void editCommand(const std::string& cmd) {
    if (cmd == "#") {
        grid.assign(64, '.');
    } else if (cmd == "c") {
        edit_colour = ~edit_colour;
    } else if (cmd == ".") {
        // The side to move is kept; castling when king and rook stand at home.
        std::string placement;
        for (int rank = 0; rank < 8; ++rank) {
            int empty = 0;
            for (int file = 0; file < 8; ++file) {
                const char c = grid[rank * 8 + file];
                if (c == '.') {
                    ++empty;
                    continue;
                }
                if (empty) placement += std::to_string(empty), empty = 0;
                placement += c;
            }
            if (empty) placement += std::to_string(empty);
            if (rank < 7) placement += '/';
        }
        const auto at = [&](const char* sq) { return grid[(7 - (sq[1] - '1')) * 8 + (sq[0] - 'a')]; };
        std::string castling;
        if (at("e1") == 'K' && at("h1") == 'R') castling += 'K';
        if (at("e1") == 'K' && at("a1") == 'R') castling += 'Q';
        if (at("e8") == 'k' && at("h8") == 'r') castling += 'k';
        if (at("e8") == 'k' && at("a8") == 'r') castling += 'q';
        board.setFen(placement + (edit_stm == Color::WHITE ? " w " : " b ") + (castling.empty() ? "-" : castling) +
                     " - 0 1");
        editing = false;
    } else if (cmd.size() == 3 && std::string("PNBRQK").find(cmd[0]) != std::string::npos) {
        const char piece = edit_colour == Color::WHITE ? cmd[0] : static_cast<char>(std::tolower(cmd[0]));
        grid[(7 - (cmd[2] - '1')) * 8 + (cmd[1] - 'a')] = piece;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "v1") v1 = true;
        if (a == "resign") resign = true;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        if (cmd.empty()) continue;

        if (editing) {
            editCommand(cmd);
            continue;
        }

        if (cmd == "protover") {
            if (!v1) {
                std::cout << "feature ping=1 setboard=1 usermove=1 san=1 myname=\"WB Mover\" memory=1" << std::endl;
                std::cout << "feature option=\"Style -combo Solid /// *Normal /// Risky\" option=\"Clear Hash -button\""
                          << std::endl;
                std::cout << "feature done=1" << std::endl;
            }
        } else if (cmd == "new") {
            board     = Board();
            force     = false;
            engine_on = Color::BLACK;
            own_moves = 0;
        } else if (cmd == "force") {
            force = true;
        } else if (cmd == "go") {
            force     = false;
            engine_on = board.sideToMove();
            think();
        } else if (cmd == "?") {
            // moves at once anyway
        } else if (cmd == "ping") {
            std::string n;
            in >> n;
            std::cout << "pong " << n << std::endl;
        } else if (cmd == "setboard") {
            if (v1) {
                std::cout << "Error (unknown command): setboard" << std::endl;
                continue;
            }
            board.setFen(line.substr(9));
        } else if (cmd == "edit") {
            if (!v1) {
                std::cout << "Error (unknown command): edit" << std::endl;
                continue;
            }
            editing     = true;
            edit_colour = Color::WHITE;
            edit_stm    = board.sideToMove();
        } else if (cmd == "usermove") {
            std::string m;
            in >> m;
            applyMove(m);
            if (!force && board.sideToMove() == engine_on) think();
        } else if (cmd == "quit") {
            return 0;
        } else if (cmd == "xboard" || cmd == "accepted" || cmd == "rejected" || cmd == "level" || cmd == "st" ||
                   cmd == "sd" || cmd == "time" || cmd == "otim" || cmd == "post" || cmd == "easy" || cmd == "hard" ||
                   cmd == "computer" || cmd == "memory" || cmd == "option" || cmd == "nopost" || cmd == "random") {
            // nothing to do
        } else if (v1 && (uci::isUciMove(cmd))) {
            applyMove(cmd);
            if (!force && board.sideToMove() == engine_on) think();
        } else {
            std::cout << "Error (unknown command): " << cmd << std::endl;
        }
    }
    return 0;
}
