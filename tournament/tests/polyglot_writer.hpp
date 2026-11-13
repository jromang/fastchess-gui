#pragma once

// Small Polyglot books written by the tests, sorted by key as the format wants them.

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <chess.hpp>

namespace fcg::test {

// A Polyglot move from "e2e4" (castling as the king taking its rook: "e1h1"), "e7e8q" with a promotion.
inline uint16_t polyglotMove(const std::string& uci) {
    const auto sq  = [](char f, char r) { return (r - '1') * 8 + (f - 'a'); };
    const int from = sq(uci[0], uci[1]), to = sq(uci[2], uci[3]);
    int promotion = 0;
    if (uci.size() > 4) promotion = std::string("nbrq").find(uci[4]) + 1;
    return static_cast<uint16_t>((promotion << 12) | ((from / 8) << 9) | ((from % 8) << 6) | ((to / 8) << 3) |
                                 (to % 8));
}

// The key the format gives the en passant file `file` (0 = a): the difference between a position where White may
// take en passant on that file and the same without the en passant square.
inline uint64_t polyglotEpKey(int file) {
    // Black's pawn just stepped to the file, White's stands beside it; the kings far away.
    std::string row(8, '1');
    row[file]                           = 'p';
    row[file < 7 ? file + 1 : file - 1] = 'P';
    std::string compact;  // "1p" runs of empty squares as digits
    for (std::size_t i = 0; i < row.size();) {
        if (row[i] != '1') {
            compact += row[i++];
            continue;
        }
        int n = 0;
        while (i < row.size() && row[i] == '1') ++n, ++i;
        compact += static_cast<char>('0' + n);
    }
    const auto placement = "k7/8/8/" + compact + "/8/8/8/7K w - ";
    const char ep[]      = {static_cast<char>('a' + file), '6', 0};
    return chess::Board(placement + ep + " 0 1").hash() ^ chess::Board(placement + "- 0 1").hash();
}

// The Polyglot key of the position after `line` (UCI moves from `fen`), worked out apart from the program's: the
// pieces, the castling rights and the side to move as chess-library hashes them without an en passant square (its
// numbers are the format's, which the official test keys check), the en passant file by the format's own rule:
// after a pawn's double step, whenever a pawn of the side to move stands beside it, whether or not it may legally
// take (it may not when it is pinned).
inline uint64_t polyglotKey(const std::string& line, const std::string& fen = std::string(chess::constants::STARTPOS),
                            bool chess960 = false) {
    chess::Board board;
    board.set960(chess960);
    board.setFen(fen);
    std::istringstream in(line);
    int stepped = -1;  // the square of a pawn that just stepped two squares
    {
        // Or the FEN says one did.
        std::istringstream fields(fen);
        std::string placement, side, castling, ep;
        fields >> placement >> side >> castling >> ep;
        if (ep.size() == 2) stepped = (ep[0] - 'a') + 8 * (ep[1] == '3' ? 3 : 4);
    }
    for (std::string uci; in >> uci;) {
        const auto move = chess::uci::uciToMove(board, uci);
        const bool pawn = board.at(move.from()).type() == chess::PieceType::PAWN;
        stepped = pawn && std::abs(move.to().index() - move.from().index()) == 16 ? move.to().index() : -1;
        board.makeMove(move);
    }

    // The same position without an en passant square.
    std::istringstream fields(board.getFen());
    std::string placement, side, castling, ep;
    fields >> placement >> side >> castling >> ep;
    chess::Board plain;
    plain.set960(chess960);
    plain.setFen(placement + " " + side + " " + castling + " - 0 1");
    auto key = plain.hash();

    if (stepped >= 0) {
        const auto mine = board.sideToMove() == chess::Color::WHITE ? chess::Piece::WHITEPAWN : chess::Piece::BLACKPAWN;
        const int file  = stepped % 8;
        bool beside     = false;
        for (const int f : {file - 1, file + 1}) {
            if (f >= 0 && f < 8 && board.at(chess::Square(stepped - file + f)) == mine) beside = true;
        }
        if (beside) key ^= polyglotEpKey(file);
    }
    return key;
}

// Writes a book of (key, move, weight) entries, sorted by key, the moves of a key in the order given.
inline void writePolyglot(const std::filesystem::path& path,
                          std::vector<std::tuple<uint64_t, std::string, int>> entries) {
    std::stable_sort(entries.begin(), entries.end(),
                     [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const auto put = [&](uint64_t v, int bytes) {
        for (int i = bytes - 1; i >= 0; --i) out.put(static_cast<char>((v >> (8 * i)) & 0xff));
    };
    for (const auto& [key, move, weight] : entries) {
        put(key, 8);
        put(polyglotMove(move), 2);
        put(static_cast<uint64_t>(weight), 2);
        put(0, 4);
    }
}

}  // namespace fcg::test
