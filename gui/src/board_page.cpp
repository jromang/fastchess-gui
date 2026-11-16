#include "board_page.hpp"

#include <algorithm>
#include <array>
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

ReplayGame lineOf(const GameTree& tree, int line) {
    if (line < 0 || line >= static_cast<int>(tree.variations.size())) return tree.main;
    const auto& v = tree.variations[static_cast<std::size_t>(line)];
    auto game     = lineOf(tree, v.parent);
    game.moves.resize(std::min(game.moves.size(), static_cast<std::size_t>(std::max(0, v.from))));
    game.moves.insert(game.moves.end(), v.moves.begin(), v.moves.end());
    game.result.clear();
    game.final_comment.clear();
    return game;
}

int ownerOf(const GameTree& tree, int line, int ply) {
    while (line >= 0 && line < static_cast<int>(tree.variations.size()) &&
           ply < tree.variations[static_cast<std::size_t>(line)].from)
        line = tree.variations[static_cast<std::size_t>(line)].parent;
    return line < static_cast<int>(tree.variations.size()) ? line : -1;
}

int branchPly(const GameTree& tree, int line, int ply) {
    while (line >= 0 && line < static_cast<int>(tree.variations.size())) {
        const auto& v = tree.variations[static_cast<std::size_t>(line)];
        ply           = std::min(ply, v.from);
        line          = v.parent;
    }
    return ply;
}

std::pair<int, int> playInTree(GameTree& tree, bool chess960, int line, int ply, chess::Move move) {
    const auto shown = lineOf(tree, line);
    ply              = std::clamp(ply, 0, static_cast<int>(shown.moves.size()));
    const auto board = boardAt(shown, chess960, ply);
    ReplayMove played;
    played.move  = move;
    played.san   = chess::uci::moveToSan(board, move);
    played.mover = board.sideToMove();
    // The line's own next move.
    if (ply < static_cast<int>(shown.moves.size()) && shown.moves[static_cast<std::size_t>(ply)].move == move)
        return {line, ply + 1};
    // At its end, the line grows.
    if (ply == static_cast<int>(shown.moves.size())) {
        if (line < 0)
            tree.main.moves.push_back(std::move(played));
        else
            tree.variations[static_cast<std::size_t>(line)].moves.push_back(std::move(played));
        return {line, ply + 1};
    }
    // A variation from here that starts with it, else a new one.
    const int owner = ownerOf(tree, line, ply);
    for (std::size_t v = 0; v < tree.variations.size(); ++v) {
        const auto& var = tree.variations[v];
        if (var.parent == owner && var.from == ply && !var.moves.empty() && var.moves.front().move == move)
            return {static_cast<int>(v), ply + 1};
    }
    tree.variations.push_back({owner, ply, {std::move(played)}});
    return {static_cast<int>(tree.variations.size()) - 1, ply + 1};
}

int removeVariation(GameTree& tree, int line) {
    if (line < 0 || line >= static_cast<int>(tree.variations.size())) return -1;
    // It and those branching from it, from the last.
    std::vector<bool> gone(tree.variations.size(), false);
    gone[static_cast<std::size_t>(line)] = true;
    for (bool more = true; more;) {
        more = false;
        for (std::size_t v = 0; v < tree.variations.size(); ++v) {
            const int p = tree.variations[v].parent;
            if (!gone[v] && p >= 0 && gone[static_cast<std::size_t>(p)]) gone[v] = more = true;
        }
    }
    std::vector<int> index(tree.variations.size(), -1);
    std::vector<Variation> kept;
    for (std::size_t v = 0; v < tree.variations.size(); ++v) {
        if (gone[v]) continue;
        index[v] = static_cast<int>(kept.size());
        kept.push_back(tree.variations[v]);
    }
    for (auto& v : kept)
        if (v.parent >= 0) v.parent = index[static_cast<std::size_t>(v.parent)];
    const int parent = tree.variations[static_cast<std::size_t>(line)].parent;
    tree.variations  = std::move(kept);
    return parent < 0 ? -1 : index[static_cast<std::size_t>(parent)];
}

int promoteVariation(GameTree& tree, int line) {
    if (line < 0 || line >= static_cast<int>(tree.variations.size())) return -1;
    auto& v            = tree.variations[static_cast<std::size_t>(line)];
    const int parent   = v.parent;
    const int from     = v.from;
    auto& parent_moves = parent < 0 ? tree.main.moves : tree.variations[static_cast<std::size_t>(parent)].moves;
    const int start    = parent < 0 ? 0 : tree.variations[static_cast<std::size_t>(parent)].from;
    const auto local   = static_cast<std::size_t>(std::clamp(from - start, 0, static_cast<int>(parent_moves.size())));
    std::vector<ReplayMove> tail(parent_moves.begin() + static_cast<std::ptrdiff_t>(local), parent_moves.end());
    parent_moves.resize(local);
    parent_moves.insert(parent_moves.end(), v.moves.begin(), v.moves.end());
    tree.variations[static_cast<std::size_t>(line)].moves = std::move(tail);
    // Those branching from the moves that changed line follow them.
    for (std::size_t i = 0; i < tree.variations.size(); ++i) {
        auto& o = tree.variations[i];
        if (static_cast<int>(i) == line) continue;
        if (o.parent == parent && o.from > from)
            o.parent = line;
        else if (o.parent == line)
            o.parent = parent;
    }
    if (parent < 0) tree.main.result.clear();
    return parent;
}

std::vector<std::optional<int64_t>> moveClocks(const ReplayGame& game) {
    std::vector<std::optional<int64_t>> out(game.moves.size());
    // The time control: "300+2", "10+0.1", "600".
    std::optional<double> base;
    double increment = 0;
    {
        const auto tc   = game.header("TimeControl");
        const auto plus = tc.find('+');
        try {
            if (!tc.empty() && tc.find_first_of("/:") == std::string::npos && tc != "-" && tc != "?") {
                base = std::stod(tc.substr(0, plus));
                if (plus != std::string::npos) increment = std::stod(tc.substr(plus + 1));
            }
        } catch (...) {
            base.reset();
        }
    }
    std::array<std::optional<double>, 2> left;
    if (base) left = {*base * 1000.0, *base * 1000.0};
    for (std::size_t i = 0; i < game.moves.size(); ++i) {
        const auto& m  = game.moves[i];
        const int side = m.mover == chess::Color::WHITE ? 0 : 1;
        auto& clock    = left[static_cast<std::size_t>(side)];
        if (m.clock_ms) {
            clock = static_cast<double>(*m.clock_ms);
        } else if (clock && !m.book) {
            if (m.time_ms < 0) {
                clock.reset();  // a move without its time: the clock is lost from here
            } else {
                clock = std::max(0.0, *clock - static_cast<double>(m.time_ms)) + increment * 1000.0;
            }
        }
        if (clock) out[i] = static_cast<int64_t>(std::lround(*clock));
    }
    return out;
}

}  // namespace fcg
