#include "pgn_store.hpp"
#include "i18n.hpp"

#include <charconv>
#include <sstream>

namespace fcg {

namespace fs = std::filesystem;

PgnFile::PgnFile(fs::path path) : path_(std::move(path)) {
    std::error_code ec;
    fs::create_directories(path_.parent_path(), ec);

    out_.open(path_, std::ios::binary | std::ios::app);
    size_ = fs::exists(path_, ec) ? fs::file_size(path_, ec) : 0;
}

PgnSpan PgnFile::append(const std::string& pgn) {
    std::lock_guard<std::mutex> lock(mutex_);

    PgnSpan span{size_, static_cast<uint32_t>(pgn.size())};
    out_.write(pgn.data(), static_cast<std::streamsize>(pgn.size()));
    out_.flush();
    size_ += pgn.size();
    return span;
}

std::optional<std::string> PgnFile::read(const PgnSpan& span) const {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return std::nullopt;

    in.seekg(static_cast<std::streamoff>(span.offset));
    std::string text(span.length, '\0');
    in.read(text.data(), span.length);
    if (in.gcount() != static_cast<std::streamsize>(span.length)) return std::nullopt;
    return text;
}

std::string exportSpans(const fs::path& source, const std::vector<PgnSpan>& spans, const fs::path& target) {
    std::ifstream in(source, std::ios::binary);
    if (!in) return trf("Cannot read {}", source.string());

    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) return trf("Cannot write {}", target.string());

    std::string buffer;
    for (const auto& span : spans) {
        buffer.resize(span.length);
        in.seekg(static_cast<std::streamoff>(span.offset));
        in.read(buffer.data(), span.length);
        out.write(buffer.data(), in.gcount());
    }

    return out ? "" : trf("Write error in {}", target.string());
}

std::string ReplayGame::header(const std::string& key) const {
    for (const auto& [k, v] : headers) {
        if (k == key) return v;
    }
    return {};
}

void parseFastchessComment(std::string_view comment, ReplayMove& move) {
    while (!comment.empty() && comment.front() == ' ') comment.remove_prefix(1);

    if (comment.rfind("book", 0) == 0) {
        move.book = true;
        return;
    }

    // <score>/<depth> <seconds>s[, ...]
    const auto slash = comment.find('/');
    if (slash == std::string_view::npos) return;

    const auto score = comment.substr(0, slash);
    if (score.size() >= 2 && (score[0] == '+' || score[0] == '-')) {
        const int sign = score[0] == '-' ? -1 : 1;
        if (score[1] == 'M') {
            // fastchess prints mates in plies; UCI and Score use moves.
            int plies = 0;
            std::from_chars(score.data() + 2, score.data() + score.size(), plies);
            const int moves = sign > 0 ? (plies + 1) / 2 : plies / 2;
            move.score      = fastchess::Score{fastchess::ScoreType::MATE, sign * moves};
        } else {
            double pawns = 0;
            try {
                pawns = std::stod(std::string(score.substr(1)));
            } catch (...) {
            }
            move.score =
                fastchess::Score{fastchess::ScoreType::CP, static_cast<int64_t>(sign * pawns * 100.0 + sign * 0.5)};
        }
    } else if (score.size() >= 4 && score.substr(0, 4) == "0.00") {
        move.score = fastchess::Score{fastchess::ScoreType::CP, 0};
    }

    auto rest          = comment.substr(slash + 1);
    int depth          = 0;
    const auto [p, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), depth);
    if (ec == std::errc()) move.depth = depth;

    rest = rest.substr(static_cast<std::size_t>(p - rest.data()));
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);

    const auto s_at = rest.find('s');
    if (s_at != std::string_view::npos) {
        try {
            move.time_ms = static_cast<int64_t>(std::stod(std::string(rest.substr(0, s_at))) * 1000.0 + 0.5);
        } catch (...) {
        }
    }
}

namespace {

class ReplayVisitor : public chess::pgn::Visitor {
   public:
    explicit ReplayVisitor(ReplayGame& game) : game_(game) {}

    void startPgn() override {}

    void header(std::string_view key, std::string_view value) override {
        game_.headers.emplace_back(std::string(key), std::string(value));
        if (key == "FEN") game_.start_fen = std::string(value);
        if (key == "Result") game_.result = std::string(value);
        if (key == "Variant" && (value == "Chess960" || value == "fischerandom")) chess960_ = true;
    }

    void startMoves() override {
        board_.set960(chess960_);
        board_.setFen(game_.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game_.start_fen);
        if (game_.start_fen.empty()) game_.start_fen = std::string(chess::constants::STARTPOS);
    }

    void move(std::string_view san, std::string_view comment) override {
        if (failed_) return;

        ReplayMove move;
        move.san   = std::string(san);
        move.mover = board_.sideToMove();

        try {
            move.move = chess::uci::parseSan(board_, san);
        } catch (...) {
            failed_             = true;
            game_.final_comment = std::string(comment);
            return;
        }

        if (move.move == chess::Move::NO_MOVE) {
            failed_ = true;
            return;
        }

        parseFastchessComment(comment, move);

        // The last move's comment ends with the termination reason, after the engine data.
        if (const auto comma = comment.rfind(", "); comma != std::string_view::npos) {
            game_.final_comment = std::string(comment.substr(comma + 2));
        }

        board_.makeMove<true>(move.move);
        game_.moves.push_back(std::move(move));
    }

    void endPgn() override {}

   private:
    ReplayGame& game_;
    chess::Board board_;
    bool chess960_ = false;
    bool failed_   = false;
};

}  // namespace

std::optional<ReplayGame> parseReplay(const std::string& pgn) {
    ReplayGame game;
    ReplayVisitor visitor(game);

    std::istringstream stream(pgn);
    chess::pgn::StreamParser parser(stream);
    const auto error = parser.readGames(visitor);

    if (error.hasError() && game.moves.empty() && game.headers.empty()) return std::nullopt;
    return game;
}

}  // namespace fcg
