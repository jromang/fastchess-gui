#include "polyglot.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <numeric>
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

std::vector<uint64_t> choiceWeights(const std::vector<uint16_t>& weights, int randomness) {
    randomness = std::clamp(randomness, -RANDOMNESS_LIMIT, RANDOMNESS_LIMIT);
    std::vector<uint64_t> out(weights.begin(), weights.end());
    if (out.empty() || randomness == 0) return out;  // exactly the weights: the choices of the versions before

    const uint16_t best = *std::max_element(weights.begin(), weights.end());
    const double e      = randomness > 0 ? 1.0 - randomness / 100.0 : 100.0 / (100.0 + randomness);
    for (std::size_t i = 0; i < weights.size(); ++i) {
        const auto w = weights[i];
        if (w == 0) continue;
        if (randomness == RANDOMNESS_LIMIT) {
            out[i] = 1;
        } else if (randomness == -RANDOMNESS_LIMIT) {
            out[i] = w == best ? 1 : 0;
        } else {
            // The best is CHOICE_SCALE exactly (1 to any power is 1); the ratios are below 1, so nothing overflows.
            out[i] = static_cast<uint64_t>(
                std::llround(std::pow(static_cast<double>(w) / best, e) * static_cast<double>(CHOICE_SCALE)));
        }
    }
    return out;
}

std::optional<std::size_t> drawChoice(const std::vector<uint64_t>& weights, uint64_t draw) {
    const uint64_t total = std::accumulate(weights.begin(), weights.end(), uint64_t{0});
    if (total == 0) return std::nullopt;
    // Not std::uniform_int_distribution, which each standard library implements its own way: the same seed gives
    // the same choices everywhere. The bias of the modulo is far below what a game could show.
    auto n = draw % total;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (n < weights[i]) return i;
        n -= weights[i];
    }
    return weights.size() - 1;
}

std::string randomnessLabel(int randomness) {
    randomness = std::clamp(randomness, -RANDOMNESS_LIMIT, RANDOMNESS_LIMIT);
    if (randomness == 0) return "moves by weight";
    if (randomness == -RANDOMNESS_LIMIT) return "best moves only";
    if (randomness == RANDOMNESS_LIMIT) return "every move alike";
    return fmt::format("randomness {:+}", randomness);
}

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

chess::Move PolyglotBook::pick(const chess::Board& board, std::mt19937_64& rng, int randomness) {
    const auto choices = moves(board);
    std::vector<uint16_t> weights;
    weights.reserve(choices.size());
    for (const auto& c : choices) weights.push_back(c.weight);
    const auto chosen = drawChoice(choiceWeights(weights, randomness), rng());
    return chosen ? choices[*chosen].move : chess::Move::NO_MOVE;
}

std::vector<PolyglotLine> polyglotLines(const std::filesystem::path& file, const PolyglotWalk& walk) {
    const auto entries = readBook(file);
    if (entries.empty()) throw std::runtime_error(fmt::format("the Polyglot book {} is empty", file.string()));

    std::vector<PolyglotLine> out;
    std::unordered_set<uint64_t> seen;  // positions already met, as leaves or on the way
    chess::Board board;

    const auto leaf = [&](double share) {
        // An en passant square only when the capture is legal, as a FEN wants it (the key has it whenever a pawn
        // stands beside, see playForBook()).
        out.push_back({board.enpassantSq() == chess::Square::NO_SQ ? board.getFen(false)
                                                                   : chess::Board(board.getFen()).getFen(false),
                       share});
        if (out.size() > walk.max_positions) {
            throw std::runtime_error(
                fmt::format("the Polyglot book {} gives more than {} positions at {} plies: choose fewer plies",
                            file.string(), walk.max_positions, walk.plies));
        }
    };

    const auto visit = [&](auto& self, int depth, double share) -> void {
        if (!seen.insert(board.hash()).second) return;  // a transposition: already followed

        const auto [first, last] = std::equal_range(entries.begin(), entries.end(), Entry{board.hash()},
                                                    [](const Entry& a, const Entry& b) { return a.key < b.key; });
        std::vector<uint16_t> weights;
        for (auto it = first; it != last; ++it) weights.push_back(it->weight);
        const auto chance   = choiceWeights(weights, walk.randomness);
        const uint64_t best = chance.empty() ? 0 : *std::max_element(chance.begin(), chance.end());

        std::vector<std::pair<chess::Move, uint64_t>> moves;
        uint64_t total = 0;
        if (depth < walk.plies && best > 0) {
            for (std::size_t i = 0; i < chance.size(); ++i) {
                if (chance[i] == 0 || static_cast<double>(chance[i]) < walk.min_share * static_cast<double>(best))
                    continue;
                const auto move = decode(board, (first + static_cast<std::ptrdiff_t>(i))->move);
                if (move == chess::Move::NO_MOVE) continue;
                moves.emplace_back(move, chance[i]);
                total += chance[i];
            }
        }
        if (moves.empty()) {
            if (depth > 0) leaf(share);  // the end of a line; the initial position itself is no opening
            return;
        }
        for (const auto& [move, weight] : moves) {
            playForBook(board, move);
            self(self, depth + 1, share * static_cast<double>(weight) / static_cast<double>(total));
            board.unmakeMove(move);
        }
    };
    visit(visit, 0, 1.0);

    if (out.empty()) {
        throw std::runtime_error(
            fmt::format("the Polyglot book {} has no move from the initial position", file.string()));
    }
    return out;
}

std::vector<std::string> polyglotPositions(const std::filesystem::path& file, const PolyglotWalk& walk) {
    std::vector<std::string> out;
    for (auto& line : polyglotLines(file, walk)) out.push_back(std::move(line.epd));
    return out;
}

std::vector<std::string> drawnInOrder(const std::vector<PolyglotLine>& lines, uint64_t seed) {
    // Each line gets the key u^(1 / share), u uniform in (0, 1); the largest keys first. As logarithms, log(u) /
    // share, which a share far below 1 does not underflow.
    std::mt19937_64 rng(seed);
    std::vector<std::pair<double, std::size_t>> keys;
    keys.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const double u     = (static_cast<double>(rng() >> 11) + 0.5) / 9007199254740992.0;  // 2^53
        const double share = lines[i].share > 0 ? lines[i].share : 1e-300;
        keys.emplace_back(std::log(u) / share, i);
    }
    std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    out.reserve(keys.size());
    for (const auto& [key, i] : keys) out.push_back(lines[i].epd);
    return out;
}

std::size_t polyglotToEpd(const std::filesystem::path& file, const std::filesystem::path& epd, const PolyglotWalk& walk,
                          std::optional<uint64_t> seed) {
    const auto positions = seed ? drawnInOrder(polyglotLines(file, walk), *seed) : polyglotPositions(file, walk);
    std::ofstream out(epd, std::ios::binary | std::ios::trunc);
    for (const auto& p : positions) out << p << '\n';
    out.close();
    if (!out) throw std::runtime_error(fmt::format("cannot write {}", epd.string()));
    return positions.size();
}

}  // namespace fcg::tour
