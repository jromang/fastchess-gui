#include "book_scan.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>

#include "polyglot.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The first four fields of an EPD line (placement, side, castling, en passant): its position.
std::string epdPosition(std::string_view line) {
    std::size_t pos = 0;
    for (int field = 0; field < 4 && pos != std::string_view::npos; ++field) {
        pos = line.find_first_not_of(' ', pos);
        if (pos == std::string_view::npos) break;
        pos = line.find(' ', pos);
    }
    return std::string(line.substr(0, pos == std::string_view::npos ? line.size() : pos));
}

// Castling rights other than K, Q, k, q (Shredder-FEN, X-FEN files), or kings and rooks away from their usual
// squares while castling is allowed: a Chess960 position.
bool looksChess960(const std::string& fen) {
    std::istringstream in(fen);
    std::string placement, side, castling;
    in >> placement >> side >> castling;
    if (castling.empty() || castling == "-") return false;
    if (castling.find_first_not_of("KQkq") != std::string::npos) return true;

    chess::Board board;
    try {
        board.setFen(placement + " " + side + " - - 0 1");
    } catch (...) {
        return false;
    }
    const auto at = [&](const char* sq) { return board.at(chess::Square(sq)); };
    using chess::Piece;
    for (const char c : castling) {
        const bool white = std::isupper(static_cast<unsigned char>(c));
        const bool king  = at(white ? "e1" : "e8") == (white ? Piece::WHITEKING : Piece::BLACKKING);
        const auto rook =
            std::tolower(static_cast<unsigned char>(c)) == 'k' ? (white ? "h1" : "h8") : (white ? "a1" : "a8");
        if (!king || at(rook) != (white ? Piece::WHITEROOK : Piece::BLACKROOK)) return true;
    }
    return false;
}

bool legal(const chess::Board& board) {
    using namespace chess;
    if (board.pieces(PieceType::KING, Color::WHITE).count() != 1) return false;
    if (board.pieces(PieceType::KING, Color::BLACK).count() != 1) return false;
    if ((board.pieces(PieceType::PAWN) & (Bitboard(Rank::RANK_1) | Bitboard(Rank::RANK_8))) != 0) return false;
    // The side that just moved cannot be in check.
    const auto them = ~board.sideToMove();
    return !board.isAttacked(board.kingSq(them), board.sideToMove());
}

bool over(const chess::Board& board) {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    return moves.empty() || board.isInsufficientMaterial();
}

uint64_t mix(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

uint64_t flip(uint64_t bb) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(bb);
#else
    uint64_t out = 0;
    for (int i = 0; i < 8; ++i) out |= ((bb >> (8 * i)) & 0xff) << (8 * (7 - i));
    return out;
#endif
}

// Identifies a position, or (mirror) the same position with colours reversed: ranks flipped, pieces of the other
// colour, the other side to move, castling rights and en passant swapped.
uint64_t positionKey(const chess::Board& board, bool mirror) {
    using namespace chess;
    uint64_t h = 0x9e3779b97f4a7c15ULL;
    for (int type = 0; type < 6; ++type) {
        for (const Color colour : {Color::WHITE, Color::BLACK}) {
            auto bb =
                board.pieces(PieceType(static_cast<PieceType::underlying>(type)), mirror ? ~colour : colour).getBits();
            h = mix(h ^ (mirror ? flip(bb) : bb));
        }
    }
    const bool white = (board.sideToMove() == Color::WHITE) != mirror;
    const auto cr    = board.castlingRights();
    uint64_t rights  = 0;
    for (const Color colour : {Color::WHITE, Color::BLACK}) {
        for (const auto side : {Board::CastlingRights::Side::KING_SIDE, Board::CastlingRights::Side::QUEEN_SIDE}) {
            const Color c = mirror ? ~colour : colour;
            rights        = rights * 9 + (cr.has(c, side) ? 1 + static_cast<int>(cr.getRookFile(c, side)) : 0);
        }
    }
    const auto ep = board.enpassantSq();
    const int sq  = ep == Square::NO_SQ ? 64 : mirror ? (ep.index() ^ 56) : ep.index();
    return mix(h ^ mix(rights * 131 + static_cast<uint64_t>(sq) * 2 + white));
}

}  // namespace

BookScan::BookScan(const fs::path& file, int plies, const std::atomic<bool>* cancel, std::atomic<std::size_t>* done) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) throw std::runtime_error("file not found: " + file.string());

    const auto ext = lower(file.extension().string());
    if (isPolyglot(file)) {
        kind_ = Kind::Polyglot;
        PolyglotWalk walk;
        if (plies > 0) walk.plies = plies;
        book_ = polyglotPositions(file, walk);
    } else if (ext == ".pgn") {
        kind_ = Kind::Pgn;
        // A Chess960 PGN book says so in its Variant tags.
        std::ifstream in(file, std::ios::binary);
        std::string head(1 << 16, '\0');
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<std::size_t>(in.gcount()));
        const auto h = lower(head);
        chess960_    = h.find("[variant \"chess960\"]") != std::string::npos ||
                       h.find("[variant \"fischerandom\"]") != std::string::npos;
        book_        = fastchess::book::PgnReader(file.string(), plies, chess960_);
    } else if (ext == ".epd") {
        kind_ = Kind::Epd;
        book_ = fastchess::book::EpdReader(file.string());
    } else {
        throw std::runtime_error("not an opening book (.epd, .pgn or .bin): " + file.string());
    }

    const auto n = std::visit(
        [](const auto& b) -> std::size_t {
            if constexpr (std::is_same_v<std::decay_t<decltype(b)>, std::vector<std::string>>) {
                return b.size();
            } else {
                return b.get().size();
            }
        },
        book_);
    if (n == 0) throw std::runtime_error("the opening book is empty: " + file.string());

    // EPD positions say themselves whether they are Chess960 ones; the first one decides for the book.
    if (kind_ == Kind::Epd) chess960_ = looksChess960(position(0));

    // Each position once: its hash, then duplicates and mirrored ones by sorting the hashes.
    flags_.assign(n, 0);
    std::vector<std::pair<uint64_t, std::size_t>> keys;  // (hash, index)
    std::vector<uint64_t> mirror_keys(n, 0);
    keys.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (cancel && *cancel) {
            flags_.resize(i);
            mirror_keys.resize(i);
            break;
        }
        if (done) *done = i;

        chess::Board board;
        board.set960(chess960_);
        const auto fen = position(i);
        try {
            if (!board.setFen(fen) || !legal(board)) {
                flags_[i] |= BOOK_ILLEGAL;
                continue;
            }
        } catch (...) {
            flags_[i] |= BOOK_ILLEGAL;
            continue;
        }
        if (over(board)) flags_[i] |= BOOK_OVER;
        const auto key = positionKey(board, false);
        keys.emplace_back(key, i);
        if (const auto mirror = positionKey(board, true); mirror != key) mirror_keys[i] = mirror;
    }
    std::sort(keys.begin(), keys.end());
    for (std::size_t k = 1; k < keys.size(); ++k) {
        if (keys[k].first == keys[k - 1].first) flags_[keys[k].second] |= BOOK_DUPLICATE;
    }
    // An opening whose mirror image came earlier in the book.
    for (std::size_t i = 0; i < mirror_keys.size(); ++i) {
        if (mirror_keys[i] == 0 || (flags_[i] & BOOK_DUPLICATE)) continue;
        const auto it = std::lower_bound(keys.begin(), keys.end(), std::make_pair(mirror_keys[i], std::size_t{0}));
        if (it != keys.end() && it->first == mirror_keys[i] && it->second < i) flags_[i] |= BOOK_MIRRORED;
    }
    if (done) *done = flags_.size();
}

std::size_t BookScan::count(uint8_t flag) const {
    return static_cast<std::size_t>(std::count_if(flags_.begin(), flags_.end(), [&](uint8_t f) { return f & flag; }));
}

std::string BookScan::position(std::size_t i) const {
    if (const auto* epd = std::get_if<fastchess::book::EpdReader>(&book_)) return epdPosition(epd->get()[i].view());
    if (const auto* fens = std::get_if<std::vector<std::string>>(&book_)) return (*fens)[i];

    const auto& opening = std::get<fastchess::book::PgnReader>(book_).get()[i];
    chess::Board board;
    board.set960(chess960_);
    board.setFen(opening.fen_epd);
    for (const auto& m : opening.moves) board.makeMove<true>(m);
    return board.getFen(false);
}

BookItem BookScan::item(std::size_t i) const {
    BookItem out;
    out.fen   = position(i);
    out.flags = flags_.at(i);
    if (const auto* pgn = std::get_if<fastchess::book::PgnReader>(&book_)) {
        const auto& opening = pgn->get()[i];
        chess::Board board;
        board.set960(chess960_);
        board.setFen(opening.fen_epd);
        for (const auto& m : opening.moves) {
            out.san.push_back(chess::uci::moveToSan(board, m));
            board.makeMove<true>(m);
        }
    }
    return out;
}

std::size_t BookScan::writeEpd(const fs::path& file, uint8_t drop) const {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    std::size_t written = 0;
    for (std::size_t i = 0; i < flags_.size(); ++i) {
        if (flags_[i] & drop) continue;
        out << position(i) << '\n';
        ++written;
    }
    out.close();
    if (!out) throw std::runtime_error("cannot write " + file.string());
    return written;
}

}  // namespace fcg::tour
