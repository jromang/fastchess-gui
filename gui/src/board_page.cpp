#include "board_page.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <tour/pgn_file.hpp>

namespace fcg {

chess::Board boardAt(const ReplayGame& game, bool chess960, int ply) {
    chess::Board board;
    board.set960(chess960);
    board.setFen(game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen);
    const int n = std::clamp(ply, 0, static_cast<int>(game.moves.size()));
    for (int i = 0; i < n; ++i) board.makeMove<true>(game.moves[static_cast<std::size_t>(i)].move);
    return board;
}

AnalysisPosition analysisPosition(const ReplayGame& game, bool chess960, int ply) {
    AnalysisPosition position;
    position.chess960 = chess960;
    chess::Board board;
    board.set960(chess960);
    board.setFen(game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen);
    position.fen = board.getFen();
    const int n  = std::clamp(ply, 0, static_cast<int>(game.moves.size()));
    for (int i = 0; i < n; ++i) {
        const auto& move = game.moves[static_cast<std::size_t>(i)].move;
        position.moves.push_back(chess::uci::moveToUci(move, chess960));
        board.makeMove<true>(move);
    }
    return position;
}

fastchess::Score whiteScore(const fastchess::Score& score, chess::Color side_to_move) {
    auto white = score;
    if (side_to_move == chess::Color::BLACK) white.value = -white.value;
    return white;
}

chess::Square shownTarget(const chess::Move& move) {
    if (move.typeOf() != chess::Move::CASTLING) return move.to();
    const bool king_side = move.to().index() > move.from().index();
    return chess::Square(chess::File(king_side ? 6 : 2), move.from().rank());
}

std::vector<chess::Move> movesFrom(const chess::Board& board, chess::Square from) {
    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board);
    std::vector<chess::Move> out;
    for (const auto& m : legal) {
        if (m.from() == from) out.push_back(m);
    }
    return out;
}

std::optional<std::string> pgnSourceGame(const PgnSource& source, std::size_t game) {
    if (!source.index || game >= source.index->games.size()) return std::nullopt;
    const auto& span = source.index->games[game].span;
    if (source.text) {
        if (span.offset > source.text->size()) return std::nullopt;
        return source.text->substr(static_cast<std::size_t>(span.offset), span.length);
    }
    return tour::readSpan(source.file, span);
}

namespace {

chess::Piece editorAt(const PositionEditor& e, int file, int rank) {
    return e.squares[static_cast<std::size_t>(rank * 8 + file)];
}

}  // namespace

bool editorCanCastle(const PositionEditor& e, int right) {
    const bool white  = right < 2;
    const bool king   = right % 2 == 0;
    const int rank    = white ? 0 : 7;
    const auto colour = white ? chess::Color::WHITE : chess::Color::BLACK;
    int king_file     = -1;
    for (int f = 0; f < 8; ++f) {
        if (editorAt(e, f, rank) == chess::Piece(chess::PieceType::KING, colour)) king_file = f;
    }
    const auto rook = chess::Piece(chess::PieceType::ROOK, colour);
    if (!e.chess960) return king_file == 4 && editorAt(e, king ? 7 : 0, rank) == rook;
    if (king_file < 1 || king_file > 6) return false;
    for (int f = king ? king_file + 1 : 0; f < (king ? 8 : king_file); ++f) {
        if (editorAt(e, f, rank) == rook) return true;
    }
    return false;
}

std::string editorFen(const PositionEditor& e) {
    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const auto piece = editorAt(e, file, rank);
            if (piece == chess::Piece::NONE) {
                ++empty;
                continue;
            }
            if (empty) fen += static_cast<char>('0' + empty);
            empty = 0;
            fen += static_cast<std::string>(piece);
        }
        if (empty) fen += static_cast<char>('0' + empty);
        if (rank > 0) fen += '/';
    }
    fen += e.stm == chess::Color::WHITE ? " w " : " b ";
    std::string castling;
    static constexpr char letters[] = {'K', 'Q', 'k', 'q'};
    for (int i = 0; i < 4; ++i) {
        if (e.castle[i] && editorCanCastle(e, i)) castling += letters[i];
    }
    fen += castling.empty() ? "-" : castling;
    // En passant: a pawn of the side not to move just went two squares, past an empty square.
    std::string ep = "-";
    if (e.ep_file >= 0 && e.ep_file < 8) {
        const bool white  = e.stm == chess::Color::WHITE;
        const int pawn_rk = white ? 4 : 3, behind = white ? 5 : 2, start = white ? 6 : 1;
        const auto pawn = chess::Piece(chess::PieceType::PAWN, white ? chess::Color::BLACK : chess::Color::WHITE);
        if (editorAt(e, e.ep_file, pawn_rk) == pawn && editorAt(e, e.ep_file, behind) == chess::Piece::NONE &&
            editorAt(e, e.ep_file, start) == chess::Piece::NONE) {
            ep = std::string(1, static_cast<char>('a' + e.ep_file)) + static_cast<char>('1' + behind);
        }
    }
    fen += " " + ep + " 0 " + std::to_string(std::max(1, e.fullmove));
    return fen;
}

bool editorLoad(PositionEditor& e, const std::string& fen) {
    // The placement alone may lack a king while it is edited: read by hand, the rest through the board.
    std::array<chess::Piece, 64> squares{};
    squares.fill(chess::Piece::NONE);
    const auto space = fen.find(' ');
    const auto place = fen.substr(0, space);
    int rank = 7, file = 0;
    for (const char c : place) {
        if (c == '/') {
            if (file != 8) return false;
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else {
            const auto piece = chess::Piece(std::string_view(&c, 1));
            if (piece == chess::Piece::NONE || rank < 0 || file > 7) return false;
            squares[static_cast<std::size_t>(rank * 8 + file)] = piece;
            ++file;
        }
        if (file > 8) return false;
    }
    if (rank != 0 || file != 8) return false;
    e.squares = squares;
    // The other fields, when there are.
    std::istringstream rest(space == std::string::npos ? std::string() : fen.substr(space + 1));
    std::string stm, castling, ep, half, full;
    rest >> stm >> castling >> ep >> half >> full;
    e.stm = stm == "b" ? chess::Color::BLACK : chess::Color::WHITE;
    for (int i = 0; i < 4; ++i) e.castle[i] = castling.find("KQkq"[i]) != std::string::npos;
    // Shredder-FEN file letters: a right on that side of the king.
    for (const char c : castling) {
        if (std::string_view("KQkq-").find(c) != std::string_view::npos) continue;
        const bool white = c >= 'A' && c <= 'H';
        const int f      = std::tolower(static_cast<unsigned char>(c)) - 'a';
        int king_file    = 4;
        for (int k = 0; k < 8; ++k) {
            if (editorAt(e, k, white ? 0 : 7) ==
                chess::Piece(chess::PieceType::KING, white ? chess::Color::WHITE : chess::Color::BLACK))
                king_file = k;
        }
        e.castle[(white ? 0 : 2) + (f > king_file ? 0 : 1)] = true;
    }
    e.ep_file  = ep.size() == 2 && ep[0] >= 'a' && ep[0] <= 'h' ? ep[0] - 'a' : -1;
    e.fullmove = 1;
    try {
        if (!full.empty()) e.fullmove = std::clamp(std::stoi(full), 1, 9999);
    } catch (...) {
    }
    e.fen_text = editorFen(e);
    return true;
}

int playOnGame(ReplayGame& game, bool chess960, int ply, chess::Move move) {
    ply = std::clamp(ply, 0, static_cast<int>(game.moves.size()));
    if (ply < static_cast<int>(game.moves.size()) && game.moves[static_cast<std::size_t>(ply)].move == move)
        return ply + 1;
    const auto board = boardAt(game, chess960, ply);
    game.moves.resize(static_cast<std::size_t>(ply));
    ReplayMove played;
    played.move  = move;
    played.mover = board.sideToMove();
    played.san   = chess::uci::moveToSan(board, move);
    game.moves.push_back(std::move(played));
    // The game's result is no longer this game's.
    game.result.clear();
    game.final_comment.clear();
    return ply + 1;
}

}  // namespace fcg
