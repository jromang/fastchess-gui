#include "pgn_store.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>

namespace fcg {

namespace fs = std::filesystem;

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

std::string pgnTag(std::string_view pgn, std::string_view tag) {
    std::size_t pos = 0;
    while (pos < pgn.size()) {
        auto end = pgn.find('\n', pos);
        if (end == std::string_view::npos) end = pgn.size();
        auto line = pgn.substr(pos, end - pos);
        pos       = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (line.empty()) continue;
        if (line.front() != '[') break;  // the moves: the header is over

        // [Tag "value"]
        line.remove_prefix(1);
        if (line.substr(0, tag.size()) != tag || line.size() <= tag.size() || line[tag.size()] != ' ') continue;
        const auto open  = line.find('"');
        const auto close = line.rfind('"');
        if (open == std::string_view::npos || close <= open) return {};
        return std::string(line.substr(open + 1, close - open - 1));
    }
    return {};
}

bool openingMatches(const std::string& eco, const std::string& name, const std::string& query) {
    auto upper = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    };
    std::string q;
    for (const char c : query) {
        if (c != ' ') q += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (q.empty()) return true;

    const auto is_eco = [](const std::string& s) {
        return s.size() == 3 && s[0] >= 'A' && s[0] <= 'E' && std::isdigit(static_cast<unsigned char>(s[1])) &&
               std::isdigit(static_cast<unsigned char>(s[2]));
    };
    const auto code = upper(eco);

    // A range of codes.
    if (const auto dash = q.find('-'); dash != std::string::npos) {
        const auto from = q.substr(0, dash), to = q.substr(dash + 1);
        if (is_eco(from) && is_eco(to)) return is_eco(code) && code >= from && code <= to;
    }
    // A code or its beginning: a letter A to E and up to two digits.
    if (q.size() <= 3 && q[0] >= 'A' && q[0] <= 'E' &&
        std::all_of(q.begin() + 1, q.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        return code.rfind(q, 0) == 0;
    }
    // Words of the name, in any order: "sicilian najdorf".
    const auto haystack = upper(name);
    std::istringstream words(upper(query));
    for (std::string word; words >> word;) {
        if (haystack.find(word) == std::string::npos) return false;
    }
    return true;
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

    if (const auto at = rest.find("nps="); at != std::string_view::npos) {
        const auto value = rest.substr(at + 4);
        std::from_chars(value.data(), value.data() + value.size(), move.nps);
    }

    if (const auto at = rest.find("wdl="); at != std::string_view::npos) {
        std::array<int64_t, 3> wdl{};
        const char* p   = rest.data() + at + 4;
        const char* end = rest.data() + rest.size();
        bool ok         = true;
        for (int i = 0; i < 3 && ok; ++i) {
            const auto r = std::from_chars(p, end, wdl[i]);
            ok           = r.ec == std::errc() && (i == 2 || (r.ptr < end && *r.ptr == '/'));
            p            = r.ptr + 1;
        }
        if (ok) move.wdl = wdl;
    }
}

void forEachMoveComment(std::string_view pgn, const std::function<void(bool, const ReplayMove&)>& f) {
    // The side to move first: White, unless a FEN header says otherwise.
    bool white = true;
    if (const auto fen = pgn.find("[FEN \""); fen != std::string_view::npos) {
        const auto space = pgn.find(' ', fen + 6);
        white            = space == std::string_view::npos || pgn.substr(space + 1, 1) != "b";
    }

    // Every move has a comment, {book} included.
    const auto body = pgn.find("\n\n");
    if (body == std::string_view::npos) return;
    for (auto open = pgn.find('{', body); open != std::string_view::npos; open = pgn.find('{', open)) {
        const auto close = pgn.find('}', open);
        if (close == std::string_view::npos) break;

        ReplayMove move;
        parseFastchessComment(pgn.substr(open + 1, close - open - 1), move);
        f(white, move);
        white = !white;
        open  = close;
    }
}

EvalFacts evalFacts(std::string_view pgn, int white_result) {
    // White's point of view, by ply; NaN for a book move or a move without a score.
    std::vector<float> eval;
    std::vector<bool> by_white;
    forEachMoveComment(pgn, [&](bool white, const ReplayMove& m) {
        float e = std::numeric_limits<float>::quiet_NaN();
        if (!m.book && m.score) {
            e = m.score->type == fastchess::ScoreType::MATE ? (m.score->value >= 0 ? 10.0f : -10.0f)
                                                            : std::clamp(m.score->value / 100.0f, -10.0f, 10.0f);
            if (!white) e = -e;
        }
        eval.push_back(e);
        by_white.push_back(white);
    });

    EvalFacts facts;
    const auto n     = static_cast<int>(eval.size());
    const auto known = [&](int i) { return i >= 0 && i < n && !std::isnan(eval[i]); };

    // The engines disagree for 6 plies in a row: the smallest gap between two consecutive plies of the window.
    constexpr int WINDOW = 6;
    for (int i = 0; i + WINDOW <= n; ++i) {
        float gap = std::numeric_limits<float>::max();
        for (int j = i; j + 1 < i + WINDOW; ++j) {
            gap = known(j) && known(j + 1) ? std::min(gap, std::abs(eval[j] - eval[j + 1])) : 0.0f;
            if (gap == 0) break;
        }
        if (gap > facts.disagreement) facts.disagreement = gap, facts.disagree_ply = static_cast<int16_t>(i);
    }

    // One engine's evaluation from one of its moves to the next.
    for (int i = 2; i < n; ++i) {
        if (!known(i) || !known(i - 2)) continue;
        const float change = std::abs(eval[i] - eval[i - 2]);
        if (change > facts.swing) facts.swing = change, facts.swing_ply = static_cast<int16_t>(i);
    }

    // The loser's best evaluation of its own position.
    if (white_result != 0) {
        const bool loser_white = white_result < 0;
        for (int i = 0; i < n; ++i) {
            if (!known(i) || by_white[i] != loser_white) continue;
            const float own = loser_white ? eval[i] : -eval[i];
            if (own > facts.loser_best) facts.loser_best = own, facts.loser_ply = static_cast<int16_t>(i);
        }
    }
    return facts;
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
