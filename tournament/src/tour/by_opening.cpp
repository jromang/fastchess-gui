#include "by_opening.hpp"

#include <unordered_map>

#include <game/pgn/openings_data.hpp>

namespace fcg::tour {

namespace {

// White's score in a game: 1, 0.5 or 0.
double whiteScore(const GameResult& r) { return r.result == "1-0" ? 1.0 : r.result == "0-1" ? 0.0 : 0.5; }

}  // namespace

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

        chess::Board board;
        board.set960(chess960);
        board.setFen(it->second.fen_epd);
        const fastchess::pgn::Opening* known = nullptr;
        const auto classify                  = [&] {
            if (chess960) return;
            const auto o = fastchess::pgn::EPD_TO_OPENING.find(board.getFen(false));
            if (o != fastchess::pgn::EPD_TO_OPENING.end()) known = &o->second;
        };
        classify();
        std::vector<std::string> moves;
        for (const auto& m : it->second.moves) {
            moves.push_back(chess::uci::moveToUci(m, chess960));
            board.makeMove<true>(m);
            classify();
        }
        auto fen = board.getFen(false);

        const auto [pos, added] = index.emplace(fen, out.size());
        if (added) {
            OpeningStats s;
            s.fen      = std::move(fen);
            s.book_fen = it->second.fen_epd;
            s.moves    = std::move(moves);
            if (known) {
                s.eco  = std::string(known->eco);
                s.name = std::string(known->name);
            }
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
