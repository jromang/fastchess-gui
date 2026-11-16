#include "board_text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <sstream>
#include <vector>

#include <fmt/include/fmt/format.h>

#include <tour/chess_text.hpp>
#include <tour/pgn_file.hpp>

#include "board_page.hpp"
#include "i18n.hpp"

namespace fcg {

namespace {

std::vector<std::string_view> words(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        const auto from = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i > from) out.push_back(text.substr(from, i - from));
    }
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

bool isNumber(std::string_view s) {
    return !s.empty() && s.size() <= 6 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// The piece placement of a FEN: 7 slashes, digits and piece letters only.
bool looksLikePlacement(std::string_view s) {
    return std::count(s.begin(), s.end(), '/') == 7 && std::all_of(s.begin(), s.end(), [](char c) {
               return c == '/' || (c >= '1' && c <= '8') ||
                      std::string_view("pnbrqkPNBRQK").find(c) != std::string_view::npos;
           });
}

bool isUciMove(std::string_view s) {
    return (s.size() == 4 || s.size() == 5) && s[0] >= 'a' && s[0] <= 'h' && s[1] >= '1' && s[1] <= '8' &&
           s[2] >= 'a' && s[2] <= 'h' && s[3] >= '1' && s[3] <= '8' &&
           (s.size() == 4 || std::string_view("qrbn").find(s[4]) != std::string_view::npos);
}

bool isResult(std::string_view s) { return s == "1-0" || s == "0-1" || s == "1/2-1/2" || s == "*"; }

// A tag line somewhere in the text: it is a PGN.
bool hasTag(std::string_view text) {
    std::size_t at = 0;
    while (at < text.size()) {
        const auto end  = text.find('\n', at);
        const auto line = trim(text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
        if (line.size() > 4 && line.front() == '[' && line.back() == ']' &&
            std::isalpha(static_cast<unsigned char>(line[1])) && line.find('"') != std::string_view::npos) {
            return true;
        }
        if (end == std::string_view::npos) break;
        at = end + 1;
    }
    return false;
}

// The square's piece letter in a FEN's placement, ' ' for an empty square (index: a1 = 0 ... h8 = 63).
std::array<char, 64> placement(std::string_view fen) {
    std::array<char, 64> squares;
    squares.fill(' ');
    int rank = 7, file = 0;
    for (const char c : fen.substr(0, fen.find(' '))) {
        if (c == '/') {
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else if (rank >= 0 && file < 8) {
            squares[static_cast<std::size_t>(rank * 8 + file)] = c;
            ++file;
        }
    }
    return squares;
}

// A score as fastchess writes it in a comment: the mover's, in pawns, a mate in plies.
std::string commentScore(const fastchess::Score& score) {
    if (score.type == fastchess::ScoreType::MATE) {
        const int64_t plies = score.value > 0 ? score.value * 2 - 1 : -score.value * 2;
        return fmt::format("{}M{}", score.value > 0 ? "+" : "-", plies);
    }
    if (score.value == 0) return "0.00";
    return fmt::format("{:+.2f}", static_cast<double>(score.value) / 100.0);
}

}  // namespace

bool needsChess960(const std::string& fen) {
    const auto fields = words(fen);
    if (fields.size() < 3) return false;
    const auto castling = fields[2];
    if (castling == "-") return false;
    if (castling.find_first_not_of("KQkq") != std::string_view::npos) return true;
    const auto squares = placement(fen);
    const auto at      = [&](int file, int rank) { return squares[static_cast<std::size_t>(rank * 8 + file)]; };
    for (const char right : castling) {
        const bool white = right == 'K' || right == 'Q';
        const int rank   = white ? 0 : 7;
        const int rook   = right == 'K' || right == 'k' ? 7 : 0;
        if (at(4, rank) != (white ? 'K' : 'k') || at(rook, rank) != (white ? 'R' : 'r')) return true;
    }
    return false;
}

std::string positionProblem(const std::string& fen, bool chess960) {
    const auto squares = placement(fen);
    const auto count   = [&](char c) { return std::count(squares.begin(), squares.end(), c); };
    if (!looksLikePlacement(trim(fen).substr(0, trim(fen).find(' ')))) {
        return TR("Not a position: a FEN gives eight ranks of eight squares, then the side to move.");
    }
    if (count('K') != 1) return TR("White needs one king, and only one.");
    if (count('k') != 1) return TR("Black needs one king, and only one.");
    if (!tour::readableFen(fen)) {
        return TR(
            "Not a position: a FEN gives eight ranks of eight squares, the side to move (w or b), the castling "
            "rights (KQkq or -), the en passant square (or -) and two move counters.");
    }
    chess::Board board;
    board.set960(chess960);
    board.setFen(fen);
    if ((board.pieces(chess::PieceType::PAWN) &
         (chess::Bitboard(chess::Rank::RANK_1) | chess::Bitboard(chess::Rank::RANK_8))) != 0) {
        return TR("A pawn is on the first or the last rank.");
    }
    if (board.isAttacked(board.kingSq(~board.sideToMove()), board.sideToMove()))
        return TR("The side that is not to move is in check.");
    if (board.checkers().count() > 2) return TR("The side to move is in check by more than two pieces.");
    if (!tour::legalPosition(board)) return TR("Not a legal position.");
    return {};
}

std::optional<ReplayGame> readPgnGame(const std::string& pgn, std::string& error) {
    auto game = parseReplay(pgn);
    if (!game) {
        error = TR("No game in it, or a start position (its FEN tag) that cannot be played.");
        return std::nullopt;
    }
    if (game->start_fen.empty()) game->start_fen = std::string(chess::constants::STARTPOS);
    return game;
}

bool gameIsChess960(const ReplayGame& game) {
    auto variant = game.header("Variant");
    for (auto& c : variant) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return variant.find("960") != std::string::npos || variant.find("fischer") != std::string::npos;
}

BoardText readBoardText(std::string_view text) {
    BoardText out;
    text = trim(text);
    if (text.empty()) {
        out.error = TR("Nothing to read: the text is empty.");
        return out;
    }

    // A FEN, or an EPD line: its first four fields, then the move counters when it has them.
    const auto first  = text.substr(0, text.find('\n'));
    const auto fields = words(first);
    if (!fields.empty() && looksLikePlacement(fields[0])) {
        std::string fen(fields[0]);
        fen += " " + std::string(fields.size() > 1 ? fields[1] : "w");
        fen += " " + std::string(fields.size() > 2 ? fields[2] : "-");
        fen += " " + std::string(fields.size() > 3 ? fields[3] : "-");
        if (fields.size() > 5 && isNumber(fields[4]) && isNumber(fields[5])) {
            fen += " " + std::string(fields[4]) + " " + std::string(fields[5]);
        } else {
            fen += " 0 1";
        }
        out.chess960 = needsChess960(fen);
        if (auto problem = positionProblem(fen, out.chess960); !problem.empty()) {
            out.error = std::move(problem);
            return out;
        }
        chess::Board board;
        board.set960(out.chess960);
        board.setFen(fen);
        out.kind = BoardText::Kind::Position;
        out.fen  = board.getFen();
        return out;
    }

    if (hasTag(text)) {
        out.kind = BoardText::Kind::Games;
        out.pgn  = std::string(text);
        return out;
    }

    // Moves without tags: numbers and a result aside, in UCI or in SAN.
    std::vector<std::string> moves;
    for (auto w : words(text)) {
        // "12." "12..." "1.e4"
        const auto dot = w.find_last_of('.');
        if (dot != std::string_view::npos && isNumber(w.substr(0, w.find('.')))) w = w.substr(dot + 1);
        if (w.empty() || isResult(w)) continue;
        moves.emplace_back(w);
    }
    if (moves.empty()) {
        out.error = TR("No position, game or move found in the text.");
        return out;
    }
    if (std::all_of(moves.begin(), moves.end(), [](const std::string& m) { return isUciMove(m); })) {
        chess::Board board;
        std::string movetext;
        for (const auto& m : moves) {
            const auto move = chess::uci::uciToMove(board, m);
            chess::Movelist legal;
            chess::movegen::legalmoves(legal, board);
            if (move == chess::Move::NO_MOVE || std::find(legal.begin(), legal.end(), move) == legal.end()) {
                if (movetext.empty()) {
                    out.error = trf("{} is not a legal move from the initial position.", m);
                    return out;
                }
                break;  // the moves up to it
            }
            if (board.sideToMove() == chess::Color::WHITE) movetext += fmt::format("{}. ", board.fullMoveNumber());
            movetext += chess::uci::moveToSan(board, move) + " ";
            board.makeMove<true>(move);
        }
        out.kind = BoardText::Kind::Games;
        out.pgn  = "[Event \"?\"]\n\n" + movetext + "*\n";
        return out;
    }
    // SAN: a PGN without its tags.
    std::string error;
    const std::string pgn = "[Event \"?\"]\n\n" + std::string(text) + "\n";
    const auto game       = readPgnGame(pgn, error);
    if (!game || game->moves.empty()) {
        out.error = TR("No position, game or move found in the text.");
        return out;
    }
    out.kind = BoardText::Kind::Games;
    out.pgn  = pgn;
    return out;
}

std::string gamePgn(const ReplayGame& game, bool chess960) { return gamePgn(GameTree{game, {}}, chess960); }

std::string gamePgn(const GameTree& tree, bool chess960) {
    const auto& game = tree.main;
    const auto tag = [](const std::string& name, const std::string& value) {
        return fmt::format("[{} \"{}\"]\n", name, tour::pgnTagValue(value));
    };
    const auto value = [&](const char* name, const char* fallback) {
        const auto v = game.header(name);
        return v.empty() ? std::string(fallback) : v;
    };
    const std::string result = game.result.empty() ? "*" : game.result;
    const std::string start  = game.start_fen.empty() ? std::string(chess::constants::STARTPOS) : game.start_fen;

    std::string out;
    out += tag("Event", value("Event", "?"));
    out += tag("Site", value("Site", "?"));
    out += tag("Date", value("Date", "????.??.??"));
    out += tag("Round", value("Round", "-"));
    out += tag("White", value("White", "?"));
    out += tag("Black", value("Black", "?"));
    out += tag("Result", result);
    static constexpr std::string_view written[] = {"Event", "Site",   "Date",  "Round", "White",
                                                   "Black", "Result", "SetUp", "FEN",   "PlyCount"};
    for (const auto& [name, v] : game.headers) {
        if (std::find(std::begin(written), std::end(written), name) != std::end(written)) continue;
        if (name == "Variant" && chess960) continue;
        out += tag(name, v);
    }
    if (chess960) out += tag("Variant", "Chess960");
    if (chess960 || start != chess::constants::STARTPOS) {
        out += tag("SetUp", "1");
        out += tag("FEN", start);
    }
    out += tag("PlyCount", std::to_string(game.moves.size()));
    out += "\n";

    // The moves, lines of at most 80 characters.
    chess::Board board;
    board.set960(chess960);
    board.setFen(start);
    std::string line;
    const auto put = [&](const std::string& word) {
        if (!line.empty() && line.size() + 1 + word.size() > 80) {
            out += line + "\n";
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
    };
    // A line's moves from absolute ply `base`, each followed by the variations that replace it.
    const std::function<void(int, int, chess::Board)> write = [&](int which, int base, chess::Board at) {
        const auto& moves = which < 0 ? game.moves : tree.variations[static_cast<std::size_t>(which)].moves;
        bool number       = true;
        for (std::size_t i = 0; i < moves.size(); ++i) {
            const auto& m = moves[i];
            const int ply = base + static_cast<int>(i);
            if (at.sideToMove() == chess::Color::WHITE)
                put(fmt::format("{}.", at.fullMoveNumber()));
            else if (number)
                put(fmt::format("{}...", at.fullMoveNumber()));
            number = false;
            // A move and its comment on the same line, as other readers expect them.
            auto move = chess::uci::moveToSan(at, m.move);
            if (m.book) {
                move += " {book}";
            } else if (m.score) {
                move += " {" + commentScore(*m.score) + "/" + std::to_string(m.depth);
                if (m.time_ms >= 0) move += fmt::format(" {:.3f}s", static_cast<double>(m.time_ms) / 1000.0);
                move += "}";
            }
            put(move);
            for (std::size_t v = 0; v < tree.variations.size(); ++v) {
                const auto& var = tree.variations[v];
                if (var.parent != which || var.from != ply || var.moves.empty()) continue;
                put("(");
                write(static_cast<int>(v), ply, at);
                put(")");
                number = true;
            }
            at.makeMove<true>(m.move);
        }
    };
    write(-1, 0, board);
    put(result);
    out += line + "\n\n";
    // No space inside the brackets.
    for (const auto& [from, to] :
         {std::pair<std::string, std::string>{"( ", "("}, {" )", ")"}, {"(\n", "("}, {"\n)", ")"}}) {
        for (auto at = out.find(from); at != std::string::npos; at = out.find(from, at))
            out.replace(at, from.size(), to);
    }
    return out;
}

std::vector<Variation> readVariations(const std::string& pgn, const ReplayGame& main, bool chess960) {
    GameTree tree{main, {}};
    // The moves' text: after the tags.
    std::size_t at = 0;
    while (at < pgn.size()) {
        const auto eol = pgn.find('\n', at);
        const auto line =
            trim(std::string_view(pgn).substr(at, eol == std::string::npos ? std::string::npos : eol - at));
        if (!line.empty() && line.front() != '[') break;
        if (eol == std::string::npos) return {};
        at = eol + 1;
    }
    struct Where {
        int line = -1;
        int ply  = 0;
        chess::Board board, before;  // the position, and the one before the last move
        bool moved = false;
    };
    chess::Board start;
    start.set960(chess960);
    start.setFen(main.start_fen.empty() ? std::string(chess::constants::STARTPOS) : main.start_fen);
    Where here{-1, 0, start, start, false};
    std::vector<Where> stack;
    while (at < pgn.size()) {
        const char c = pgn[at];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++at;
        } else if (c == '{') {
            const auto end = pgn.find('}', at);
            at             = end == std::string::npos ? pgn.size() : end + 1;
        } else if (c == ';') {
            const auto end = pgn.find('\n', at);
            at             = end == std::string::npos ? pgn.size() : end + 1;
        } else if (c == '(') {
            ++at;
            stack.push_back(here);
            if (!here.moved || here.line == -2) {
                here.line = -2;  // a variation before any move, or in one skipped: skipped
                continue;
            }
            const int from = here.ply - 1;
            tree.variations.push_back({ownerOf(tree, here.line, from), from, {}});
            here = Where{static_cast<int>(tree.variations.size()) - 1, from, here.before, here.before, false};
        } else if (c == ')') {
            ++at;
            if (stack.empty()) continue;
            here = stack.back();
            stack.pop_back();
        } else {
            auto end = at;
            while (end < pgn.size() && !std::isspace(static_cast<unsigned char>(pgn[end])) && pgn[end] != '(' &&
                   pgn[end] != ')' && pgn[end] != '{' && pgn[end] != ';')
                ++end;
            std::string word = pgn.substr(at, end - at);
            at               = end;
            // Numbers, results and annotations are not moves.
            while (!word.empty() && std::isdigit(static_cast<unsigned char>(word.front()))) {
                const auto dot = word.find('.');
                if (dot == std::string::npos) break;
                const auto rest = word.find_first_not_of('.', dot);
                word            = rest == std::string::npos ? std::string() : word.substr(rest);
            }
            if (word.empty() || word.front() == '$' || word == "*" || word == "1-0" || word == "0-1" ||
                word == "1/2-1/2" || std::isdigit(static_cast<unsigned char>(word.front())))
                continue;
            while (!word.empty() && (word.back() == '!' || word.back() == '?')) word.pop_back();
            if (here.line == -2) continue;
            chess::Move move = chess::Move::NO_MOVE;
            try {
                move = chess::uci::parseSan(here.board, word);
            } catch (...) {
            }
            if (move == chess::Move::NO_MOVE) {
                here.line = here.line < 0 ? -1 : -2;  // what follows in this line cannot be played
                if (here.line == -1) break;
                continue;
            }
            if (here.line >= 0) {
                ReplayMove m;
                m.move  = move;
                m.san   = chess::uci::moveToSan(here.board, move);
                m.mover = here.board.sideToMove();
                tree.variations[static_cast<std::size_t>(here.line)].moves.push_back(std::move(m));
            }
            here.before = here.board;
            here.board.makeMove<true>(move);
            here.ply++;
            here.moved = true;
        }
    }
    // Those with no move left out, the others renumbered.
    std::vector<int> index(tree.variations.size(), -1);
    std::vector<Variation> kept;
    for (std::size_t v = 0; v < tree.variations.size(); ++v) {
        const auto& var = tree.variations[v];
        if (var.moves.empty() || (var.parent >= 0 && index[static_cast<std::size_t>(var.parent)] < 0)) continue;
        index[v] = static_cast<int>(kept.size());
        kept.push_back(var);
        if (kept.back().parent >= 0) kept.back().parent = index[static_cast<std::size_t>(var.parent)];
    }
    return kept;
}

}  // namespace fcg
