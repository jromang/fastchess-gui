#include "polyglot.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

#include <fmt/include/fmt/format.h>
#include <chess.hpp>

namespace fcg::tour {

namespace {

// One 16-byte entry, big endian: key, move, weight, learn (unused).
struct Entry {
    uint64_t key    = 0;
    uint16_t move   = 0;
    uint16_t weight = 0;
};

uint64_t bigEndian(const unsigned char* p, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
    return v;
}

std::vector<Entry> readBook(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error(fmt::format("cannot read the Polyglot book {}", file.string()));

    std::vector<Entry> entries;
    unsigned char raw[16];
    while (in.read(reinterpret_cast<char*>(raw), sizeof(raw))) {
        entries.push_back({bigEndian(raw, 8), static_cast<uint16_t>(bigEndian(raw + 8, 2)),
                           static_cast<uint16_t>(bigEndian(raw + 10, 2))});
    }
    // Books are sorted by key; a stable sort keeps the order of the moves of a position if one is not.
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });
    return entries;
}

// The legal move a book move stands for: from, to, promotion piece; castling as the king taking its rook, which is
// also how chess-library writes it.
chess::Move decode(const chess::Board& board, uint16_t move) {
    const int to_file = move & 7, to_rank = (move >> 3) & 7;
    const int from_file = (move >> 6) & 7, from_rank = (move >> 9) & 7;
    const int promotion = (move >> 12) & 7;  // 0 none, 1 knight, 2 bishop, 3 rook, 4 queen

    const chess::Square from(from_rank * 8 + from_file);
    const chess::Square to(to_rank * 8 + to_file);

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    for (const auto& m : moves) {
        if (m.from() != from || m.to() != to) continue;
        if (m.typeOf() == chess::Move::PROMOTION) {
            static constexpr chess::PieceType::underlying PIECES[] = {
                chess::PieceType::underlying::NONE, chess::PieceType::underlying::KNIGHT,
                chess::PieceType::underlying::BISHOP, chess::PieceType::underlying::ROOK,
                chess::PieceType::underlying::QUEEN};
            if (promotion < 1 || promotion > 4 || m.promotionType() != chess::PieceType(PIECES[promotion])) continue;
        }
        return m;
    }
    return chess::Move::NO_MOVE;
}

}  // namespace

bool isPolyglot(const std::filesystem::path& file) {
    auto ext = file.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".bin";
}

PolyglotBook::PolyglotBook(const std::filesystem::path& file) : in_(file, std::ios::binary) {
    if (!in_) throw std::runtime_error(fmt::format("cannot read the Polyglot book {}", file.string()));
    in_.seekg(0, std::ios::end);
    const auto size = static_cast<uint64_t>(std::max<std::streamoff>(0, in_.tellg()));
    if (size < 16 || size % 16 != 0) throw std::runtime_error(fmt::format("{} is no Polyglot book", file.string()));
    count_ = size / 16;

    if (sorted()) return;
    if (count_ > SORT_MAX) {
        throw UnsortedPolyglotBook(
            fmt::format("the Polyglot book {} is not sorted by key, as the format wants, and too large to be sorted "
                        "in memory",
                        file.string()));
    }
    // Sorted in memory, the moves of a position kept in the book's order.
    std::vector<Entry> entries;
    entries.reserve(count_);
    for (uint64_t i = 0; i < count_; ++i) entries.push_back(at(i));
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });
    memory_ = std::move(entries);
}

PolyglotBook::Entry PolyglotBook::at(uint64_t i) {
    if (!memory_.empty()) return memory_[i];
    unsigned char raw[16];
    in_.clear();
    in_.seekg(static_cast<std::streamoff>(i * 16));
    if (!in_.read(reinterpret_cast<char*>(raw), sizeof(raw))) return Entry{~uint64_t{0}};
    return Entry{bigEndian(raw, 8), static_cast<uint16_t>(bigEndian(raw + 8, 2)),
                 static_cast<uint16_t>(bigEndian(raw + 10, 2))};
}

bool PolyglotBook::sorted() {
    // A book up to SORT_MAX entries: every key, read in one pass. A larger one: keys at evenly spread entries, which
    // tells an unsorted book (one written in the order of its lines, or of its positions) at little cost.
    uint64_t previous = 0;
    if (count_ <= SORT_MAX) {
        in_.clear();
        in_.seekg(0);
        std::vector<char> chunk(16 * 4096);
        for (uint64_t done = 0; done < count_;) {
            const auto n = std::min<uint64_t>(count_ - done, chunk.size() / 16);
            if (!in_.read(chunk.data(), static_cast<std::streamsize>(n * 16))) return false;
            for (uint64_t i = 0; i < n; ++i) {
                const auto key = bigEndian(reinterpret_cast<const unsigned char*>(chunk.data()) + 16 * i, 8);
                if (key < previous) return false;
                previous = key;
            }
            done += n;
        }
        return true;
    }
    constexpr uint64_t SAMPLES = 4096;
    for (uint64_t s = 0; s <= SAMPLES; ++s) {
        const auto key = at(s * (count_ - 1) / SAMPLES).key;
        if (key < previous) return false;
        previous = key;
    }
    return true;
}

std::vector<PolyglotBook::Choice> PolyglotBook::moves(const chess::Board& board) {
    const auto key = board.hash();

    // The first entry of the key.
    uint64_t lo = 0, hi = count_;
    while (lo < hi) {
        const auto mid = lo + (hi - lo) / 2;
        if (at(mid).key < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    std::vector<Choice> out;
    for (auto i = lo; i < count_; ++i) {
        const auto e = at(i);
        if (e.key != key) break;
        if (e.weight == 0) continue;
        const auto move = decode(board, e.move);
        if (move != chess::Move::NO_MOVE) out.push_back({move, e.weight});
    }
    return out;
}

chess::Move PolyglotBook::pick(const chess::Board& board, std::mt19937_64& rng) {
    const auto choices = moves(board);
    uint64_t total     = 0;
    for (const auto& c : choices) total += c.weight;
    if (total == 0) return chess::Move::NO_MOVE;

    // Not std::uniform_int_distribution, which each standard library implements its own way: the same seed gives
    // the same choices everywhere. The bias of the modulo is far below what a game could show.
    auto n = rng() % total;
    for (const auto& c : choices) {
        if (n < c.weight) return c.move;
        n -= c.weight;
    }
    return choices.back().move;
}

std::vector<std::string> polyglotPositions(const std::filesystem::path& file, const PolyglotWalk& walk) {
    const auto entries = readBook(file);
    if (entries.empty()) throw std::runtime_error(fmt::format("the Polyglot book {} is empty", file.string()));

    std::vector<std::string> out;
    std::unordered_set<uint64_t> seen;  // positions already met, as leaves or on the way
    chess::Board board;

    const auto leaf = [&] {
        // An en passant square only when the capture is legal, as a FEN wants it (the key has it whenever a pawn
        // stands beside, see playForBook()).
        out.push_back(board.enpassantSq() == chess::Square::NO_SQ ? board.getFen(false)
                                                                  : chess::Board(board.getFen()).getFen(false));
        if (out.size() > walk.max_positions) {
            throw std::runtime_error(
                fmt::format("the Polyglot book {} gives more than {} positions at {} plies: choose fewer plies",
                            file.string(), walk.max_positions, walk.plies));
        }
    };

    const auto visit = [&](auto& self, int depth) -> void {
        if (!seen.insert(board.hash()).second) return;  // a transposition: already followed

        const auto [first, last] = std::equal_range(entries.begin(), entries.end(), Entry{board.hash()},
                                                    [](const Entry& a, const Entry& b) { return a.key < b.key; });
        uint16_t best            = 0;
        for (auto it = first; it != last; ++it) best = std::max(best, it->weight);

        std::vector<chess::Move> moves;
        if (depth < walk.plies && best > 0) {
            for (auto it = first; it != last; ++it) {
                if (it->weight == 0 || it->weight < walk.min_share * best) continue;
                const auto move = decode(board, it->move);
                if (move != chess::Move::NO_MOVE) moves.push_back(move);
            }
        }
        if (moves.empty()) {
            if (depth > 0) leaf();  // the end of a line; the initial position itself is no opening
            return;
        }
        for (const auto& move : moves) {
            playForBook(board, move);
            self(self, depth + 1);
            board.unmakeMove(move);
        }
    };
    visit(visit, 0);

    if (out.empty()) {
        throw std::runtime_error(
            fmt::format("the Polyglot book {} has no move from the initial position", file.string()));
    }
    return out;
}

std::size_t polyglotToEpd(const std::filesystem::path& file, const std::filesystem::path& epd,
                          const PolyglotWalk& walk) {
    const auto positions = polyglotPositions(file, walk);
    std::ofstream out(epd, std::ios::binary | std::ios::trunc);
    for (const auto& p : positions) out << p << '\n';
    out.close();
    if (!out) throw std::runtime_error(fmt::format("cannot write {}", epd.string()));
    return positions.size();
}

}  // namespace fcg::tour
