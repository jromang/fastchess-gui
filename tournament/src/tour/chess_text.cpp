#include "chess_text.hpp"

#include <algorithm>
#include <vector>

namespace fcg::tour {

bool readableFen(std::string_view fen) {
    // The fields as the board reader splits them: at each space, those before the first left out.
    fen.remove_prefix(std::min(fen.find_first_not_of(' '), fen.size()));
    std::vector<std::string_view> fields;
    for (std::size_t at = 0; at <= fen.size() && fields.size() < 6;) {
        const auto end = std::min(fen.find(' ', at), fen.size());
        fields.push_back(fen.substr(at, end - at));
        at = end + 1;
    }
    const auto only = [](std::string_view s, std::string_view allowed) {
        return std::all_of(s.begin(), s.end(), [&](char c) { return allowed.find(c) != std::string_view::npos; });
    };
    // Castling rights in letters it knows, KQkq or the rooks' files (Shredder-FEN): from any other byte it takes a
    // file far off the board.
    if (fields.size() > 2 && fields[2] != "-" && (fields[2].size() > 4 || !only(fields[2], "KQkqABCDEFGHabcdefgh"))) {
        return false;
    }
    // Move counters of a few digits: it doubles the full move number in an int.
    for (std::size_t i = 4; i < fields.size(); ++i) {
        if (fields[i].size() > 6 || !only(fields[i], "0123456789")) return false;
    }
    int rank = 0, file = 0;
    for (const char c : fields[0]) {
        if (c == '/') {
            if (file != 8 || ++rank > 7) return false;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else if (std::string_view("pnbrqkPNBRQK").find(c) != std::string_view::npos) {
            ++file;
        } else {
            return false;
        }
        if (file > 8) return false;
    }
    // A king of each colour: it looks for them, for the castling rights and the en passant square, and takes the
    // square past h8 for one that is not there.
    const auto kings = [&](char king) { return std::count(fields[0].begin(), fields[0].end(), king); };
    return rank == 7 && file == 8 && kings('K') == 1 && kings('k') == 1;
}

bool legalPosition(const chess::Board& board) {
    using namespace chess;
    if (board.pieces(PieceType::KING, Color::WHITE).count() != 1) return false;
    if (board.pieces(PieceType::KING, Color::BLACK).count() != 1) return false;
    if ((board.pieces(PieceType::PAWN) & (Bitboard(Rank::RANK_1) | Bitboard(Rank::RANK_8))) != 0) return false;
    // The side that just moved cannot be in check.
    const auto them = ~board.sideToMove();
    return !board.isAttacked(board.kingSq(them), board.sideToMove());
}

bool readableSan(std::string_view san) {
    if (san.empty() || (san.front() != 'O' && san.front() != '0')) return true;
    const std::string_view queen_side = san.front() == 'O' ? "O-O-O" : "0-0-0";
    const std::string_view king_side  = queen_side.substr(0, 3);
    if (san.starts_with(queen_side)) {
        san.remove_prefix(queen_side.size());
    } else if (san.starts_with(king_side)) {
        san.remove_prefix(king_side.size());
    } else {
        return false;
    }
    // Check, mate and annotations only after it.
    return std::all_of(san.begin(), san.end(), [](char c) { return c == '+' || c == '#' || c == '!' || c == '?'; });
}

}  // namespace fcg::tour
