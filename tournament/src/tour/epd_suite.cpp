#include "epd_suite.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <stdexcept>

#include <engine/uci_engine.hpp>

#include "chess_text.hpp"

namespace fcg::tour {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// The operations of an EPD line: opcode -> operands, quotes removed.
std::map<std::string, std::string> operations(std::string_view text) {
    std::map<std::string, std::string> out;
    while (!(text = trim(text)).empty()) {
        // An operand may hold a ';' inside quotes.
        std::size_t end = 0;
        bool quoted     = false;
        while (end < text.size() && (quoted || text[end] != ';')) {
            if (text[end] == '"') quoted = !quoted;
            ++end;
        }
        const auto op    = trim(text.substr(0, end));
        const auto space = op.find(' ');
        std::string value(space == std::string_view::npos ? "" : trim(op.substr(space + 1)));
        value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
        if (!op.empty()) out[std::string(op.substr(0, space))] = value;
        text.remove_prefix(std::min(end + 1, text.size()));
    }
    return out;
}

// A SAN move of the position, annotations left out; NO_MOVE when it is none.
chess::Move san(const chess::Board& board, std::string text) {
    while (!text.empty() && (text.back() == '!' || text.back() == '?')) text.pop_back();
    if (text.empty() || !readableSan(text)) return chess::Move::NO_MOVE;
    try {
        return chess::uci::parseSan(board, text);
    } catch (...) {
        return chess::Move::NO_MOVE;
    }
}

std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == ',')) ++i;
        const auto start = i;
        while (i < s.size() && s[i] != ' ' && s[i] != ',') ++i;
        if (i > start) out.emplace_back(s.substr(start, i - start));
    }
    return out;
}

}  // namespace

bool SuitePosition::solvedBy(const chess::Move& move) const {
    if (std::find(avoid.begin(), avoid.end(), move) != avoid.end()) return false;
    if (!best.empty()) return std::find(best.begin(), best.end(), move) != best.end();
    if (!avoid.empty()) return true;
    // Points only: the move with the most.
    return !points.empty() && pointsOf(move) == points.front().second;
}

int SuitePosition::pointsOf(const chess::Move& move) const {
    for (const auto& [m, p] : points) {
        if (m == move) return p;
    }
    return 0;
}

bool Suite::hasPoints() const {
    return std::any_of(positions.begin(), positions.end(), [](const auto& p) { return !p.points.empty(); });
}

int Suite::maxPoints() const {
    int sum = 0;
    for (const auto& p : positions) {
        int best = 0;
        for (const auto& [m, points] : p.points) best = std::max(best, points);
        sum += best;
    }
    return sum;
}

bool parseSuiteLine(std::string_view line, SuitePosition& out) {
    line = trim(line);
    if (line.empty() || line.front() == '#') return false;

    // Four fields of position, then the operations.
    std::size_t at = 0;
    for (int field = 0; field < 4; ++field) {
        at = line.find(' ', at);
        if (at == std::string_view::npos) return false;
        while (at < line.size() && line[at] == ' ') ++at;
    }
    const auto board_part = trim(line.substr(0, at));
    const auto ops        = operations(line.substr(at));

    const auto counter = [&](const char* op, const char* fallback) {
        const auto it = ops.find(op);
        return it != ops.end() && !it->second.empty() ? it->second : std::string(fallback);
    };
    out     = {};
    out.fen = std::string(board_part) + " " + counter("hmvc", "0") + " " + counter("fmvn", "1");

    // A position the moves can be read in: with too many squares the board reader writes past the board, and without
    // a king the move generator reads outside its tables.
    chess::Board board;
    try {
        if (!readableFen(out.fen) || !board.setFen(out.fen) || !legalPosition(board)) return false;
    } catch (...) {
        return false;
    }

    if (const auto it = ops.find("bm"); it != ops.end()) {
        for (const auto& w : words(it->second)) {
            if (const auto m = san(board, w); m != chess::Move::NO_MOVE) out.best.push_back(m);
        }
        out.best_text = it->second;
    }
    if (const auto it = ops.find("am"); it != ops.end()) {
        for (const auto& w : words(it->second)) {
            if (const auto m = san(board, w); m != chess::Move::NO_MOVE) out.avoid.push_back(m);
        }
        out.best_text += (out.best_text.empty() ? "" : " ") + std::string("≠ ") + it->second;
    }
    // STS: c0 "f5=10, Be5+=2, Bf2=3".
    if (const auto it = ops.find("c0"); it != ops.end() && it->second.find('=') != std::string::npos) {
        for (const auto& w : words(it->second)) {
            const auto eq = w.find('=');
            if (eq == std::string::npos) continue;
            const auto m = san(board, w.substr(0, eq));
            int points   = 0;
            try {
                points = std::stoi(w.substr(eq + 1));
            } catch (...) {
                continue;
            }
            if (m != chess::Move::NO_MOVE) out.points.emplace_back(m, points);
        }
        std::stable_sort(out.points.begin(), out.points.end(),
                         [](const auto& a, const auto& b) { return a.second > b.second; });
    }
    if (const auto it = ops.find("id"); it != ops.end()) out.id = it->second;
    return !out.best.empty() || !out.avoid.empty() || !out.points.empty();
}

Suite readSuite(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + file.string());
    Suite suite;
    std::string line;
    while (std::getline(in, line)) {
        if (trim(line).empty()) continue;
        SuitePosition p;
        if (!parseSuiteLine(line, p)) {
            suite.skipped++;
            continue;
        }
        if (p.id.empty()) p.id = "#" + std::to_string(suite.positions.size() + 1);
        suite.positions.push_back(std::move(p));
    }
    return suite;
}

SuiteSearch::SuiteSearch(const SuitePosition& position, int hold) : position_(position), hold_(hold) {
    board_.setFen(position.fen);
}

bool SuiteSearch::info(std::string_view line, int64_t elapsed_ms) {
    const auto info = fastchess::engine::UciEngine::parseInfo(line);
    if (info.depth) depth_ = static_cast<int>(*info.depth);
    if (info.pv.empty() || info.isBound() || (info.multipv && *info.multipv != 1)) return false;

    const auto time = info.time.value_or(elapsed_ms);
    if (info.pv[0] != first_) {
        first_          = info.pv[0];
        const auto move = chess::uci::uciToMove(board_, first_);
        found_ms_       = position_.solvedBy(move) ? time : -1;
        found_depth_    = depth_;
    }
    return hold_ > 0 && found_ms_ >= 0 && depth_ - found_depth_ >= hold_;
}

SuiteAnswer SuiteSearch::finish(std::string_view bestmove, int64_t elapsed_ms) const {
    SuiteAnswer a;
    a.depth = depth_;
    if (bestmove.empty()) {
        a.time_ms = elapsed_ms;
        return a;
    }
    const auto move = chess::uci::uciToMove(board_, std::string(bestmove));
    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board_);
    if (std::find(legal.begin(), legal.end(), move) == legal.end()) {
        a.move    = std::string(bestmove);
        a.time_ms = elapsed_ms;
        return a;
    }
    a.move    = chess::uci::moveToSan(board_, move);
    a.solved  = position_.solvedBy(move);
    a.points  = position_.pointsOf(move);
    a.time_ms = a.solved && first_ == bestmove && found_ms_ >= 0 ? std::min(found_ms_, elapsed_ms) : elapsed_ms;
    return a;
}

}  // namespace fcg::tour
