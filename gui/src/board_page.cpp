#include "board_page.hpp"

#include <algorithm>

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
