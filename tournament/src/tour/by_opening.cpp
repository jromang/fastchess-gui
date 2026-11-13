#include "by_opening.hpp"

#include <unordered_map>

#include <game/pgn/openings_data.hpp>

namespace fcg::tour {

namespace {

// White's score in a game: 1, 0.5 or 0.
double whiteScore(const GameResult& r) { return r.result == "1-0" ? 1.0 : r.result == "0-1" ? 0.0 : 0.5; }

}  // namespace

ClassifiedOpening classify(const fastchess::book::Opening& opening, bool chess960) {
    ClassifiedOpening out;
    chess::Board board;
    board.set960(chess960);
    board.setFen(opening.fen_epd);
    const auto known = [&] {
        if (chess960) return;
        const auto o = fastchess::pgn::EPD_TO_OPENING.find(board.getFen(false));
        if (o == fastchess::pgn::EPD_TO_OPENING.end()) return;
        out.eco  = std::string(o->second.eco);
        out.name = std::string(o->second.name);
    };
    known();
    for (const auto& m : opening.moves) {
        out.moves.push_back(chess::uci::moveToUci(m, chess960));
        board.makeMove<true>(m);
        known();
    }
    out.fen = board.getFen(false);
    return out;
}

std::string openingLabel(const ClassifiedOpening& opening) {
    if (!opening.eco.empty()) return opening.eco + " " + opening.name;
    std::string label;
    for (const auto& m : opening.moves) label += (label.empty() ? "" : " ") + m;
    return label;
}

std::vector<OpeningStats> resultsByOpening(const std::vector<GameResult>& results,
                                           const std::map<uint32_t, fastchess::book::Opening>& openings,
                                           bool chess960) {
    std::vector<OpeningStats> out;
    std::unordered_map<std::string, std::size_t> index;  // starting position -> entry

    struct Pair {
        std::size_t entry = 0;
        std::vector<double> white;  // White's score in each counted game
    };
    std::map<uint32_t, Pair> pairs;  // by encounter

    for (const auto& r : results) {
        if (r.voided) continue;
        const auto it = openings.find(r.encounter);
        if (it == openings.end()) continue;

        auto c                  = classify(it->second, chess960);
        const auto [pos, added] = index.emplace(c.fen, out.size());
        if (added) {
            OpeningStats s;
            s.fen      = std::move(c.fen);
            s.book_fen = it->second.fen_epd;
            s.moves    = std::move(c.moves);
            s.eco      = std::move(c.eco);
            s.name     = std::move(c.name);
            out.push_back(std::move(s));
        }
        auto& s = out[pos->second];
        s.games++;
        s.white_points += whiteScore(r);
        if (r.result == "1/2-1/2") s.draws++;
        s.plies += r.plies;
        s.game_numbers.push_back(r.number() + 1);

        auto& pair = pairs[r.encounter];
        pair.entry = pos->second;
        pair.white.push_back(whiteScore(r));
    }

    // The colours are reversed in the second game: White winning both means each engine won once.
    for (const auto& [encounter, pair] : pairs) {
        if (pair.white.size() != 2) continue;
        auto& s = out[pair.entry];
        s.pairs++;
        const double a = pair.white[0], b = pair.white[1];
        if (a == 1 && b == 1) s.white_twice++;
        if (a == 0 && b == 0) s.black_twice++;
        if (a + b == 1 && a != 0.5) s.one_engine_twice++;
        if (a == 0.5 && b == 0.5) s.drawn_twice++;
    }
    return out;
}

std::string openingsEpd(const std::vector<const OpeningStats*>& openings) {
    std::string out;
    for (const auto* o : openings) out += o->fen + "\n";
    return out;
}

}  // namespace fcg::tour
