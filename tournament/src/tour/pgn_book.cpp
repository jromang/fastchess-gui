#include "pgn_book.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <system_error>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include "files.hpp"
#include "polyglot.hpp"

#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

namespace fcg::tour {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

long processId() {
#ifdef _WIN32
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(::getpid());
#endif
}

// The (position, move) pairs met, with the number of games that played each. Appended one by one and, every time
// they have doubled, sorted and merged: the memory stays within a few times that of the distinct pairs, however
// many games there are.
class MoveCounts {
   public:
    struct Count {
        uint64_t key   = 0;
        uint16_t move  = 0;
        uint32_t count = 0;
    };

    void add(uint64_t key, uint16_t move) {
        counts_.push_back({key, move, 1});
        if (counts_.size() >= merge_at_) merge();
    }

    // Sorted by key and move, each pair once.
    std::vector<Count>& merged() {
        merge();
        return counts_;
    }

   private:
    void merge() {
        std::sort(counts_.begin(), counts_.end(),
                  [](const Count& a, const Count& b) { return a.key != b.key ? a.key < b.key : a.move < b.move; });
        std::size_t out = 0;
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            if (out > 0 && counts_[out - 1].key == counts_[i].key && counts_[out - 1].move == counts_[i].move) {
                const uint64_t sum     = uint64_t{counts_[out - 1].count} + counts_[i].count;
                counts_[out - 1].count = static_cast<uint32_t>(std::min<uint64_t>(sum, UINT32_MAX));
            } else {
                counts_[out++] = counts_[i];
            }
        }
        counts_.resize(out);
        merge_at_ = std::max<std::size_t>(std::size_t{1} << 20, 2 * out);
    }

    std::vector<Count> counts_;
    std::size_t merge_at_ = std::size_t{1} << 20;
};

// Each game, played from its start up to `plies`: its pairs counted at its end, when all its moves were legal.
class BookVisitor : public chess::pgn::Visitor {
   public:
    BookVisitor(MoveCounts& counts, PgnBookStats& stats, int plies, bool chess960)
        : counts_(counts), stats_(stats), plies_(plies), chess960_(chess960) {}

    void startPgn() override {
        fen_         = std::string(chess::constants::STARTPOS);
        chess960_ok_ = chess960_;
        ply_         = 0;
        set_         = false;
        bad_         = false;
        game_.clear();
    }

    void header(std::string_view key, std::string_view value) override {
        if (key == "FEN") {
            fen_ = std::string(value);
        } else if (key == "Variant") {
            const auto v = lower(std::string(value));
            if (v.find("960") != std::string::npos || v.find("fischer") != std::string::npos) chess960_ok_ = true;
        }
    }

    void startMoves() override {}

    void move(std::string_view san, std::string_view) override {
        if (bad_ || ply_ >= plies_) return;
        if (!set_) {
            set_ = true;
            board_.set960(chess960_ok_);
            if (!board_.setFen(fen_)) {
                bad_ = true;
                return;
            }
        }
        chess::Move move = chess::Move::NO_MOVE;
        try {
            move = chess::uci::parseSan(board_, san, legal_);
        } catch (const std::exception&) {
        }
        if (move == chess::Move::NO_MOVE || move == chess::Move::NULL_MOVE) {
            bad_ = true;
            return;
        }
        game_.emplace_back(board_.hash(), polyglotMove(move));
        playForBook(board_, move);
        ++ply_;
    }

    void endPgn() override {
        if (bad_) {
            ++stats_.skipped;
            return;
        }
        ++stats_.games;
        for (const auto& [key, move] : game_) counts_.add(key, move);
    }

   private:
    MoveCounts& counts_;
    PgnBookStats& stats_;
    const int plies_;
    const bool chess960_;

    chess::Board board_;
    chess::Movelist legal_;
    std::string fen_;
    bool chess960_ok_ = false;  // this game: Chess960
    int ply_          = 0;
    bool set_         = false;  // its starting position is set
    bool bad_         = false;  // a move or its position could not be read: the game is left out
    std::vector<std::pair<uint64_t, uint16_t>> game_;
};

// Absolute: the adapter is given the book's path, and runs in its engine's folder.
fs::path cacheDir() {
    std::error_code ec;
    const auto dir = book_cache_dir.empty() ? fs::temp_directory_path(ec) / "fastchess-gui-book-cache" : book_cache_dir;
    const auto absolute = fs::absolute(dir, ec);
    return ec ? dir : absolute.lexically_normal();
}

// FNV-1a, 64 bits.
uint64_t fnv(const void* data, std::size_t size, uint64_t h = 0xcbf29ce484222325ULL) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 0x100000001b3ULL;
    return h;
}

// The sidecar of a cached book: what it was made from and what the conversion found.
fs::path sidecar(const fs::path& bin) { return fs::path(bin).replace_extension(".json"); }

}  // namespace

bool isPgnBook(const fs::path& file) { return lower(file.extension().string()) == ".pgn"; }

uint16_t polyglotMove(chess::Move move) {
    int promotion = 0;
    if (move.typeOf() == chess::Move::PROMOTION) {
        const auto piece = move.promotionType();
        promotion        = piece == chess::PieceType::KNIGHT   ? 1
                           : piece == chess::PieceType::BISHOP ? 2
                           : piece == chess::PieceType::ROOK   ? 3
                                                               : 4;
    }
    // A square's index is its rank times eight plus its file, as in the format's fields.
    return static_cast<uint16_t>((promotion << 12) | (move.from().index() << 6) | move.to().index());
}

void writePolyglotBook(const fs::path& file, std::vector<PolyglotEntry> entries) {
    std::sort(entries.begin(), entries.end(), [](const PolyglotEntry& a, const PolyglotEntry& b) {
        if (a.key != b.key) return a.key < b.key;
        if (a.weight != b.weight) return a.weight > b.weight;
        return a.move < b.move;
    });

    static std::atomic<unsigned> written{0};
    const auto tmp = fs::path(file).concat(fmt::format(".{}-{}.tmp", processId(), written++));
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        std::vector<char> chunk;
        chunk.reserve(16 * 4096);
        const auto put = [&](uint64_t v, int bytes) {
            for (int i = bytes - 1; i >= 0; --i) chunk.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
        };
        for (const auto& e : entries) {
            put(e.key, 8);
            put(e.move, 2);
            put(e.weight, 2);
            put(0, 4);  // learn: unused
            if (chunk.size() >= 16 * 4096) {
                out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                chunk.clear();
            }
        }
        out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        out.close();
        if (!out) {
            fs::remove(tmp, ec);
            throw std::runtime_error(fmt::format("cannot write {}", tmp.string()));
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        // Another program made the same book meanwhile (and may be reading it, which Windows does not let a rename
        // replace): that one is as good.
        fs::remove(tmp, ec);
        if (!fs::is_regular_file(file, ec)) throw std::runtime_error(fmt::format("cannot write {}", file.string()));
    }
}

PgnBookStats pgnToPolyglot(const fs::path& pgn, const fs::path& bin, int plies, bool chess960,
                           const BookProgress& progress) {
    std::ifstream in(pgn, std::ios::binary);
    if (!in) throw std::runtime_error(fmt::format("cannot read {}", pgn.string()));
    std::error_code ec;
    const auto size = fs::file_size(pgn, ec);

    PgnBookStats stats;
    MoveCounts counts;
    BookVisitor visitor(counts, stats, std::max(1, plies), chess960);
    chess::pgn::StreamParser parser(in);
    double read = 0;  // the share reported last
    for (uint64_t n = 1; parser.readNextGame(visitor); ++n) {
        // Where the parser is in the file, every few games: it reads ahead by blocks, which is close enough.
        if (progress && n % 64 == 0) {
            if (const auto at = in.tellg(); at >= 0 && size > 0) {
                read = std::max(read, std::min(1.0, static_cast<double>(at) / static_cast<double>(size)));
            }
            if (!progress(read)) throw BookCancelled();
        }
    }
    if (const auto& error = parser.error(); error.hasError()) {
        stats.stopped = error.code() == chess::pgn::StreamParserError::ExceededMaxStringLength
                            ? std::string("a tag or a move is too long")
                            : error.message();
    }

    // The weights: the counts, scaled per position when the most played move goes beyond 16 bits.
    auto& merged = counts.merged();
    std::vector<PolyglotEntry> entries;
    entries.reserve(merged.size());
    for (std::size_t i = 0; i < merged.size();) {
        std::size_t end = i;
        uint32_t most   = 0;
        for (; end < merged.size() && merged[end].key == merged[i].key; ++end) most = std::max(most, merged[end].count);
        for (auto k = i; k < end; ++k) {
            const uint64_t c = merged[k].count;
            const auto weight =
                most <= 65535 ? c : std::max<uint64_t>(1, (c * 65535 + most / 2) / static_cast<uint64_t>(most));
            entries.push_back({merged[k].key, merged[k].move, static_cast<uint16_t>(weight)});
        }
        ++stats.positions;
        i = end;
    }
    std::vector<MoveCounts::Count>().swap(merged);
    if (entries.empty()) {
        throw std::runtime_error(stats.stopped.empty()
                                     ? fmt::format("no game of {} gives a move", pgn.string())
                                     : fmt::format("no game of {} gives a move ({})", pgn.string(), stats.stopped));
    }
    stats.entries = entries.size();
    if (progress && !progress(1.0)) throw BookCancelled();
    writePolyglotBook(bin, std::move(entries));
    return stats;
}

fs::path pgnBookCacheFile(const std::string& pgn, int depth, bool chess960) {
    std::error_code ec;
    const auto file = fs::absolute(fs::path(pgn), ec).lexically_normal();
    if (ec) return {};
    const auto size = fs::file_size(file, ec);
    if (ec) return {};
    const auto time = fs::last_write_time(file, ec);
    if (ec) return {};

    // The name: a few letters of the file's, to tell the books apart, and a hash of all it is made from (the
    // conversion's version, should it change).
    const auto& native = file.native();
    auto h             = fnv(native.data(), native.size() * sizeof(native[0]));
    const auto rest =
        fmt::format("|{}|{}|{}|{}|v1", size, time.time_since_epoch().count(), pgnBookPlies(depth), chess960 ? 1 : 0);
    h = fnv(rest.data(), rest.size(), h);
    std::string stem;
    for (const auto c : file.stem().u8string()) {
        if (stem.size() >= 32) break;
        const auto b = static_cast<unsigned char>(c);
        if (std::isalnum(b) || b == '-' || b == '_') stem += static_cast<char>(b);
    }
    return cacheDir() / fmt::format("{}{}{:016x}.bin", stem, stem.empty() ? "" : "-", h);
}

std::optional<PreparedBook> cachedPgnBook(const std::string& pgn, int depth, bool chess960) {
    const auto bin = pgnBookCacheFile(pgn, depth, chess960);
    std::error_code ec;
    if (bin.empty() || !fs::is_regular_file(bin, ec)) return std::nullopt;
    PreparedBook book;
    book.file   = bin.string();
    book.reused = true;
    try {
        std::ifstream in(sidecar(bin), std::ios::binary);
        if (!in) return std::nullopt;
        const auto j         = nlohmann::json::parse(in);
        book.stats.games     = j.value("games", uint64_t{0});
        book.stats.skipped   = j.value("skipped", uint64_t{0});
        book.stats.positions = j.value("positions", uint64_t{0});
        book.stats.entries   = j.value("entries", uint64_t{0});
        book.stats.stopped   = j.value("stopped", std::string());
    } catch (const std::exception&) {
        return std::nullopt;  // made again
    }
    // Used: kept by pruneBookCache() for another while.
    fs::last_write_time(sidecar(bin), fs::file_time_type::clock::now(), ec);
    return book;
}

PreparedBook preparePgnBook(const std::string& pgn, int depth, bool chess960, const BookProgress& progress) {
    if (auto cached = cachedPgnBook(pgn, depth, chess960)) return *cached;
    const auto bin = pgnBookCacheFile(pgn, depth, chess960);
    if (bin.empty()) throw std::runtime_error(fmt::format("file not found: {}", pgn));

    PreparedBook book;
    book.file              = bin.string();
    book.stats             = pgnToPolyglot(pgn, bin, pgnBookPlies(depth), chess960, progress);
    const nlohmann::json j = {{"pgn", pgn},
                              {"plies", pgnBookPlies(depth)},
                              {"chess960", chess960},
                              {"games", book.stats.games},
                              {"skipped", book.stats.skipped},
                              {"positions", book.stats.positions},
                              {"entries", book.stats.entries},
                              {"stopped", book.stats.stopped}};
    replaceFile(sidecar(bin), j.dump(2) + "\n");
    return book;
}

std::string ownBookFile(const std::string& book, int depth, bool chess960) {
    if (!isPgnBook(book)) return book;
    if (const auto cached = cachedPgnBook(book, depth, chess960)) return cached->file;
    return pgnBookCacheFile(book, depth, chess960).string();
}

std::size_t pruneBookCache(int days) {
    const auto dir = cacheDir();
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(24) * days;
    std::error_code ec;
    std::vector<fs::path> remove;
    for (fs::directory_iterator it(dir, ec), last; !ec && it != last; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        // A book's age is its sidecar's, which each use renews; a file without one (left by a conversion that did
        // not end) its own.
        const auto& file = it->path();
        auto base        = file;
        while (base.has_extension() && base.extension() != ".bin" && base.extension() != ".json") {
            base.replace_extension();
        }
        const auto side = sidecar(fs::path(base).replace_extension(".bin"));
        std::error_code e;
        auto time = fs::last_write_time(side, e);
        if (e || file.extension() == ".tmp") time = fs::last_write_time(file, e);
        if (!e && time < old) remove.push_back(file);
    }
    std::size_t removed = 0;
    for (const auto& file : remove) removed += fs::remove(file, ec) ? 1 : 0;
    return removed;
}

}  // namespace fcg::tour
