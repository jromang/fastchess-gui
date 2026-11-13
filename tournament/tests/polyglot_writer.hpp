#pragma once

// Small Polyglot books written by the tests, sorted by key as the format wants them.

#include <algorithm>
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

// The position after `line` (UCI moves from `fen`), its key.
inline uint64_t polyglotKey(const std::string& line, const std::string& fen = std::string(chess::constants::STARTPOS),
                            bool chess960 = false) {
    chess::Board board;
    board.set960(chess960);
    board.setFen(fen);
    std::istringstream in(line);
    for (std::string uci; in >> uci;) board.makeMove<true>(chess::uci::uciToMove(board, uci));
    return board.hash();
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
