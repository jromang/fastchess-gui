#include "pgn_book.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <system_error>

#include <fmt/include/fmt/format.h>
#include <json.hpp>

#include "chess_text.hpp"
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

// The (position, move) pairs met, with the number of games that played each and their score for the side that
// played it. Appended one by one and, every time they have doubled, sorted and merged: the memory stays within a few
// times that of the distinct pairs, however many games there are.
class MoveCounts {
   public:
    struct Count {
        uint64_t key   = 0;
        uint32_t count = 0;
        uint32_t score = 0;  // 2 a win, 1 a draw, 0 a loss
        uint16_t move  = 0;
    };

    void add(uint64_t key, uint16_t move, uint32_t score) {
        counts_.push_back({key, 1, score, move});
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
                const auto add = [](uint32_t& to, uint32_t more) {
                    to = static_cast<uint32_t>(std::min<uint64_t>(uint64_t{to} + more, UINT32_MAX));
                };
                add(counts_[out - 1].count, counts_[i].count);
                add(counts_[out - 1].score, counts_[i].score);
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

// A rating tag's value; 0 when it is none ("", "-", "?").
int rating(std::string_view value) {
    int elo = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') return 0;
        elo = std::min(elo * 10 + (c - '0'), 100000);
    }
    return elo;
}

// Each game, played from its start up to its plies: its pairs counted at its end, when all its moves were legal and
// the options keep it.
class BookVisitor : public chess::pgn::Visitor {
   public:
    BookVisitor(MoveCounts& counts, PgnBookStats& stats, const PgnBookOptions& options)
        : counts_(counts), stats_(stats), options_(options), plies_(std::max(1, options.plies)) {}

    void startPgn() override {
        fen_         = std::string(chess::constants::STARTPOS);
        chess960_ok_ = options_.chess960;
        ply_         = 0;
        set_         = false;
        bad_         = false;
        result_      = -1;
        white_elo_   = 0;
        black_elo_   = 0;
        game_.clear();
    }

    void header(std::string_view key, std::string_view value) override {
        if (key == "FEN") {
            fen_ = std::string(value);
        } else if (key == "Variant") {
            const auto v = lower(std::string(value));
            if (v.find("960") != std::string::npos || v.find("fischer") != std::string::npos) chess960_ok_ = true;
        } else if (key == "Result") {
            result_ = value == "1-0" ? 2 : value == "1/2-1/2" ? 1 : value == "0-1" ? 0 : -1;
        } else if (key == "WhiteElo") {
            white_elo_ = rating(value);
        } else if (key == "BlackElo") {
            black_elo_ = rating(value);
        }
    }

    void startMoves() override {}

    void move(std::string_view san, std::string_view) override {
        if (bad_ || ply_ >= plies_) return;
        if (!set_) {
            set_ = true;
            board_.set960(chess960_ok_);
            // A position the moves can be read in (see legalPosition()).
            if (!readableFen(fen_) || !board_.setFen(fen_) || !legalPosition(board_)) {
                bad_ = true;
                return;
            }
        }
        chess::Move move = chess::Move::NO_MOVE;
        try {
            if (readableSan(san)) move = chess::uci::parseSan(board_, san, legal_);
        } catch (const std::exception&) {
        }
        if (move == chess::Move::NO_MOVE || move == chess::Move::NULL_MOVE) {
            bad_ = true;
            return;
        }
        game_.push_back({board_.hash(), polyglotMove(move), board_.sideToMove() == chess::Color::WHITE});
        playForBook(board_, move);
        ++ply_;
    }

    void endPgn() override {
        if (bad_) {
            ++stats_.skipped;
            return;
        }
        const bool by_score = options_.weight == PgnBookOptions::Weight::Score;
        const bool white_ok = options_.min_elo <= 0 || white_elo_ >= options_.min_elo;
        const bool black_ok = options_.min_elo <= 0 || black_elo_ >= options_.min_elo;
        if ((by_score && result_ < 0) || (!white_ok && !black_ok)) {
            ++stats_.left_out;
            return;
        }
        ++stats_.games;
        // The score of the side that moved: White's is the result, Black's its reverse.
        const auto white = static_cast<uint32_t>(std::max(0, result_));
        for (const auto& m : game_) {
            if (m.white ? white_ok : black_ok) counts_.add(m.key, m.move, m.white ? white : 2 - white);
        }
    }

   private:
    struct Played {
        uint64_t key  = 0;
        uint16_t move = 0;
        bool white    = false;  // played by White
    };

    MoveCounts& counts_;
    PgnBookStats& stats_;
    const PgnBookOptions& options_;
    const int plies_;

    chess::Board board_;
    chess::Movelist legal_;
    std::string fen_;
    bool chess960_ok_ = false;  // this game: Chess960
    int ply_          = 0;
    bool set_         = false;  // its starting position is set
    bool bad_         = false;  // a move or its position could not be read: the game is left out
    int result_       = -1;     // White's score: 2 a win, 1 a draw, 0 a loss; -1 unknown
    int white_elo_    = 0;      // 0 unknown
    int black_elo_    = 0;
    std::vector<Played> game_;
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
        // Windows does not replace a file being read.
        const auto reason = ec.message();
        fs::remove(tmp, ec);
        throw std::runtime_error(fmt::format("cannot write {}: {}", file.string(), reason));
    }
}

PgnBookStats pgnToPolyglot(const fs::path& pgn, const fs::path& bin, const PgnBookOptions& options,
                           const BookProgress& progress) {
    std::ifstream in(pgn, std::ios::binary);
    if (!in) throw std::runtime_error(fmt::format("cannot read {}", pgn.string()));
    std::error_code ec;
    const auto size = fs::file_size(pgn, ec);

    PgnBookStats stats;
    MoveCounts counts;
    BookVisitor visitor(counts, stats, options);
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

    // The moves kept: played by enough games, and weighing something. Their weights: the counts or the scores,
    // scaled per position when the heaviest move goes beyond 16 bits, their ratios kept.
    const auto min_games = static_cast<uint32_t>(std::max(1, options.min_games));
    const auto weightOf  = [&](const MoveCounts::Count& c) -> uint64_t {
        if (c.count < min_games) return 0;
        return options.weight == PgnBookOptions::Weight::Score ? c.score : c.count;
    };
    auto& merged = counts.merged();
    std::vector<PolyglotEntry> entries;
    entries.reserve(merged.size());
    for (std::size_t i = 0; i < merged.size();) {
        std::size_t end = i;
        uint64_t most   = 0;
        for (; end < merged.size() && merged[end].key == merged[i].key; ++end) {
            most = std::max(most, weightOf(merged[end]));
        }
        for (auto k = i; k < end; ++k) {
            const auto w = weightOf(merged[k]);
            if (w == 0) continue;
            const auto weight = most <= 65535 ? w : std::max<uint64_t>(1, (w * 65535 + most / 2) / most);
            entries.push_back({merged[k].key, merged[k].move, static_cast<uint16_t>(weight)});
        }
        if (most > 0) ++stats.positions;
        i = end;
    }
    std::vector<MoveCounts::Count>().swap(merged);
    if (entries.empty()) {
        auto what = stats.games > 0 && min_games > 1
                        ? fmt::format("no move of {} is played by {} games or more", pgn.string(), min_games)
                        : fmt::format("no game of {} gives a move", pgn.string());
        if (!stats.stopped.empty()) what += fmt::format(" ({})", stats.stopped);
        throw EmptyPgnBook(what, stats);
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
    book.file = bin.string();
    try {
        book.stats = pgnToPolyglot(pgn, bin, pgnBookPlies(depth), chess960, progress);
    } catch (const BookCancelled&) {
        throw;
    } catch (const std::exception&) {
        // Another program made the same book meanwhile, and reads it (Windows does not replace a file being read):
        // that one is as good.
        if (auto cached = cachedPgnBook(pgn, depth, chess960)) return *cached;
        throw;
    }
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

void keepCachedBook(const std::string& file) {
    const auto side = sidecar(file);
    std::error_code ec;
    const auto time = fs::last_write_time(side, ec);
    const auto now  = fs::file_time_type::clock::now();
    if (!ec && time < now - std::chrono::hours(24)) fs::last_write_time(side, now, ec);
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
