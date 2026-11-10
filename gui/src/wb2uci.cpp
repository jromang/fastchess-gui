#include "wb2uci.hpp"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

#include <fmt/include/fmt/format.h>

#ifdef _WIN64
#    include <engine/process/process_win.hpp>
#else
#    include <engine/process/process_posix.hpp>
#endif

namespace fcg::wb2uci {

namespace {

std::string trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

std::vector<std::string> split(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        const auto b = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i > b) out.emplace_back(s.substr(b, i - b));
    }
    return out;
}

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

std::optional<long long> toInt(std::string_view s) {
    if (s.empty()) return std::nullopt;
    size_t i = s[0] == '-' || s[0] == '+' ? 1 : 0;
    if (i == s.size()) return std::nullopt;
    for (size_t k = i; k < s.size(); ++k) {
        if (!std::isdigit(static_cast<unsigned char>(s[k]))) return std::nullopt;
    }
    try {
        return std::stoll(std::string(s));
    } catch (...) {
        return std::nullopt;
    }
}

// The value after `key` in a UCI go command.
std::optional<long long> goValue(const std::vector<std::string>& tokens, std::string_view key) {
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i] == key) return toInt(tokens[i + 1]);
    }
    return std::nullopt;
}

// A move as an engine writes it, coordinates or SAN, if it is legal in `board`.
std::optional<chess::Move> parseMove(const chess::Board& board, std::string text) {
    while (!text.empty() && std::string_view("+#!?").find(text.back()) != std::string_view::npos) text.pop_back();
    if (text.empty()) return std::nullopt;

    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board);
    const auto isLegal = [&](chess::Move m) { return std::find(legal.begin(), legal.end(), m) != legal.end(); };

    // Long algebraic notation to coordinates: Ng8-f6, e4xd5, e7-e8=Q.
    std::string coord = text;
    if (!coord.empty() && std::string_view("KQRBNP").find(coord[0]) != std::string_view::npos) coord.erase(0, 1);
    if (coord.size() >= 5 && (coord[2] == '-' || coord[2] == 'x' || coord[2] == ':')) coord.erase(2, 1);
    if (coord.size() == 6 && coord[4] == '=') coord.erase(4, 1);  // e7e8=q
    if (coord.size() == 5) coord[4] = static_cast<char>(std::tolower(static_cast<unsigned char>(coord[4])));
    if (chess::uci::isUciMove(coord)) {
        try {
            const auto m = chess::uci::uciToMove(board, coord);
            if (isLegal(m)) return m;
        } catch (...) {
        }
    }

    std::string san = text;
    if (startsWith(san, "0-0")) std::replace(san.begin(), san.end(), '0', 'O');
    try {
        const auto m = chess::uci::parseSan(board, san);
        if (m != chess::Move::NO_MOVE && isLegal(m)) return m;
    } catch (...) {
    }

    // Castling as the king's own move (e1g1), which is also how Chess960 engines may write it.
    if (coord.size() == 4 && chess::uci::isUciMove(coord)) {
        for (const auto m : legal) {
            if (m.typeOf() != chess::Move::CASTLING || std::string(m.from()) != coord.substr(0, 2)) continue;
            const bool king_side = m.to() > m.from();
            if (coord.substr(2) == std::string(1, king_side ? 'g' : 'c') + coord[1]) return m;
        }
    }
    return std::nullopt;
}

// UCI castling in Chess960 is king takes rook (e1h1); in standard chess, the king's move (e1g1).
std::string uciMove(chess::Move m, bool chess960) { return chess::uci::moveToUci(m, chess960); }

// The castling rights of a Shredder-FEN (HAha) as X-FEN (KQkq), which is what WinBoard sends in Chess960.
std::string xfen(const std::string& fen) {
    auto parts = split(fen);
    if (parts.size() < 3 || parts[2] == "-") return fen;
    const chess::Board board(fen, true);
    std::string rights;
    for (const char c : parts[2]) {
        const bool white = std::isupper(static_cast<unsigned char>(c));
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == 'k' || lower == 'q') {
            rights += c;
            continue;
        }
        const auto king      = board.kingSq(white ? chess::Color::WHITE : chess::Color::BLACK);
        const bool king_side = lower - 'a' > static_cast<int>(king.file());
        rights += white ? (king_side ? 'K' : 'Q') : (king_side ? 'k' : 'q');
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? " " : "") + (i == 2 ? rights : parts[i]);
    return out;
}

// "level MPS BASE INC": BASE in minutes or minutes:seconds, INC in seconds.
std::string levelCommand(long long moves, long long base_ms, long long inc_ms) {
    const long long secs = std::max(1LL, (base_ms + 500) / 1000);
    const auto base      = secs % 60 ? fmt::format("{}:{:02}", secs / 60, secs % 60) : fmt::format("{}", secs / 60);
    const auto inc       = inc_ms % 1000 == 0 ? fmt::format("{}", inc_ms / 1000) : fmt::format("{:g}", inc_ms / 1000.0);
    return fmt::format("level {} {} {}", std::max(0LL, moves), base, inc);
}

const std::vector<std::string> KNOWN_FEATURES = {
    "ping",  "setboard", "playother", "san",      "usermove", "time", "draw",    "sigint",   "sigterm",
    "reuse", "analyze",  "myname",    "variants", "colors",   "ics",  "name",    "pause",    "nps",
    "debug", "memory",   "smp",       "egt",      "option",   "done", "exclude", "setscore", "highlight"};

const std::vector<std::string> OPTION_KINDS = {"-spin", "-slider", "-check",  "-string", "-file",
                                               "-path", "-combo",  "-button", "-save",   "-reset"};

}  // namespace

std::vector<std::pair<std::string, std::string>> parseFeatures(const std::string& args) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t i = 0;
    while (i < args.size()) {
        while (i < args.size() && std::isspace(static_cast<unsigned char>(args[i]))) ++i;
        const auto name_start = i;
        while (i < args.size() && args[i] != '=' && !std::isspace(static_cast<unsigned char>(args[i]))) ++i;
        const auto name = args.substr(name_start, i - name_start);
        while (i < args.size() && std::isspace(static_cast<unsigned char>(args[i]))) ++i;
        if (i >= args.size() || args[i] != '=') {
            if (i == name_start) ++i;
            continue;
        }
        ++i;
        while (i < args.size() && std::isspace(static_cast<unsigned char>(args[i]))) ++i;
        std::string value;
        if (i < args.size() && args[i] == '"') {
            const auto end = args.find('"', i + 1);
            value          = args.substr(i + 1, end == std::string::npos ? std::string::npos : end - i - 1);
            i              = end == std::string::npos ? args.size() : end + 1;
        } else {
            const auto b = i;
            while (i < args.size() && !std::isspace(static_cast<unsigned char>(args[i]))) ++i;
            value = args.substr(b, i - b);
        }
        if (!name.empty()) out.emplace_back(name, value);
    }
    return out;
}

std::optional<CecpOption> parseOption(const std::string& text) {
    // The name may contain spaces: it ends at the first " -kind".
    size_t best = std::string::npos;
    std::string kind;
    for (const auto& k : OPTION_KINDS) {
        size_t pos = 0;
        while ((pos = text.find(" " + k, pos)) != std::string::npos) {
            const auto after = pos + 1 + k.size();
            if (after == text.size() || text[after] == ' ') break;
            ++pos;
        }
        if (pos != std::string::npos && pos < best) best = pos, kind = k;
    }
    if (best == std::string::npos) return std::nullopt;

    CecpOption option;
    option.name = trim(text.substr(0, best));
    option.kind = kind;
    if (option.name.empty()) return std::nullopt;
    const auto rest = trim(text.substr(best + 1 + kind.size()));

    if (kind == "-spin" || kind == "-slider") {
        const auto v = split(rest);
        if (v.size() < 3) return std::nullopt;
        option.value = v[0], option.min = v[1], option.max = v[2];
    } else if (kind == "-check") {
        option.value = rest == "1" ? "1" : "0";
    } else if (kind == "-string" || kind == "-file" || kind == "-path") {
        option.value = rest;
    } else if (kind == "-combo") {
        size_t pos = 0;
        while (pos <= rest.size()) {
            const auto end = rest.find("///", pos);
            auto var       = trim(rest.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
            if (!var.empty() && var[0] == '*') {
                var          = trim(var.substr(1));
                option.value = var;
            }
            if (!var.empty()) option.vars.push_back(var);
            if (end == std::string::npos) break;
            pos = end + 3;
        }
        if (option.vars.empty()) return std::nullopt;
        if (option.value.empty()) option.value = option.vars.front();
    }
    return option;
}

Adapter::Adapter(Sink to_gui, Sink to_engine, std::string name)
    : to_gui_(std::move(to_gui)), to_engine_(std::move(to_engine)), name_(std::move(name)) {}

void Adapter::start(Clock::time_point now) {
    now_ = now;
    to_engine_("xboard");
    to_engine_("protover 2");
    init_deadline_ = now + FEATURE_TIMEOUT;
}

void Adapter::tick(Clock::time_point now) {
    now_ = now;
    if (init_ == Init::Waiting && now >= init_deadline_) finishInit();
    if (ready_deadline_ && now >= *ready_deadline_) {
        // The owed move never came: the engine dropped that search.
        ready_deadline_.reset();
        stale_ = false;
        to_gui_("readyok");
    }
    if (move_deadline_ && now >= *move_deadline_) {
        move_deadline_.reset();
        if (searching_) to_engine_("?");
    }
    if (claim_deadline_ && now >= *claim_deadline_) {
        claim_deadline_.reset();
        if (searching_) {
            // xboard forfeits a claim it cannot confirm; this one stopped the engine.
            searching_ = false;
            info("the engine claimed a result and stopped playing");
            to_gui_("bestmove 0000");
        }
    }
}

void Adapter::finishInit() {
    if (init_ == Init::Done) return;
    init_ = Init::Done;
    while (!queued_.empty() && !quitting_) {
        const auto line = std::move(queued_.front());
        queued_.pop_front();
        handleGui(line);
    }
}

bool Adapter::playsChess960() const {
    const auto it = features_.find("variants");
    return it != features_.end() && ("," + it->second + ",").find(",fischerandom,") != std::string::npos;
}

bool Adapter::feature(const std::string& name, bool def) const {
    const auto it = features_.find(name);
    return it == features_.end() ? def : it->second == "1";
}

void Adapter::info(const std::string& text) const { to_gui_("info string " + text); }

// ---------------------------------------------------------------------------
// From the GUI

void Adapter::fromGui(const std::string& raw, Clock::time_point now) {
    now_            = now;
    const auto line = trim(raw);
    if (line.empty()) return;
    if (line == "quit") {
        quitting_ = true;
        to_engine_("quit");
        return;
    }
    if (init_ == Init::Waiting) {
        queued_.push_back(line);
        return;
    }
    handleGui(line);
}

void Adapter::handleGui(const std::string& line) {
    const auto tokens = split(line);
    if (tokens.empty()) return;
    const auto& cmd = tokens[0];

    if (cmd == "uci") {
        sendUciId();
    } else if (cmd == "isready") {
        if (feature("ping", false)) {
            // Moves that come before the pong belong to searches given up on: dropped (as xboard does).
            pending_pong_ = ++ping_;
            to_engine_(fmt::format("ping {}", ping_));
        } else if (stale_) {
            ready_deadline_ = now_ + OWED_MOVE_TIMEOUT;  // ready once the owed move is in, or given up
        } else {
            to_gui_("readyok");
        }
    } else if (cmd == "setoption") {
        const auto name_pos  = line.find(" name ");
        const auto value_pos = line.find(" value ");
        if (name_pos == std::string::npos) return;
        const auto name_end = value_pos == std::string::npos ? std::string::npos : value_pos - name_pos - 6;
        setOption(trim(line.substr(name_pos + 6, name_end)),
                  value_pos == std::string::npos ? "" : trim(line.substr(value_pos + 7)));
    } else if (cmd == "ucinewgame") {
        abandonSearch();
        new_game_ = true;
    } else if (cmd == "position") {
        position(tokens);
    } else if (cmd == "go") {
        go(tokens);
    } else if (cmd == "stop") {
        if (searching_) to_engine_("?");
    }
    // ponderhit, debug, register: not used
}

void Adapter::sendUciId() {
    to_gui_("id name " + name_);
    to_gui_("id author (WinBoard engine)");

    const auto has = [&](const std::string& name) {
        return std::any_of(options_.begin(), options_.end(), [&](const auto& o) { return o.name == name; });
    };
    if (feature("memory", false) && !has("Hash")) to_gui_("option name Hash type spin default 64 min 1 max 1048576");
    if (feature("smp", false) && !has("Threads")) to_gui_("option name Threads type spin default 1 min 1 max 1024");
    if (playsChess960()) to_gui_("option name UCI_Chess960 type check default false");
    const auto egt = features_.find("egt");
    if (egt != features_.end() && egt->second.find("syzygy") != std::string::npos && !has("SyzygyPath")) {
        to_gui_("option name SyzygyPath type string default <empty>");
    }

    for (const auto& o : options_) {
        if (o.kind == "-spin" || o.kind == "-slider") {
            to_gui_(fmt::format("option name {} type spin default {} min {} max {}", o.name, o.value, o.min, o.max));
        } else if (o.kind == "-check") {
            to_gui_(fmt::format("option name {} type check default {}", o.name, o.value == "1" ? "true" : "false"));
        } else if (o.kind == "-string" || o.kind == "-file" || o.kind == "-path") {
            to_gui_(
                fmt::format("option name {} type string default {}", o.name, o.value.empty() ? "<empty>" : o.value));
        } else if (o.kind == "-combo") {
            std::string vars;
            for (const auto& v : o.vars) vars += " var " + v;
            to_gui_(fmt::format("option name {} type combo default {}{}", o.name, o.value, vars));
        } else {
            to_gui_(fmt::format("option name {} type button", o.name));
        }
    }
    to_gui_("uciok");
}

void Adapter::setOption(const std::string& name, const std::string& value) {
    if (name == "UCI_Chess960" && playsChess960()) {
        chess960_ = value == "true";
        new_game_ = true;
        return;
    }
    for (const auto& o : options_) {
        if (o.name != name) continue;
        if (o.kind == "-button" || o.kind == "-save" || o.kind == "-reset") {
            to_engine_("option " + name);
        } else if (o.kind == "-check") {
            to_engine_(fmt::format("option {}={}", name, value == "true" || value == "1" ? 1 : 0));
        } else {
            to_engine_(fmt::format("option {}={}", name, value == "<empty>" ? "" : value));
        }
        return;
    }
    if (name == "Hash" && feature("memory", false)) {
        held_["memory"] = "memory " + value;
        to_engine_(held_["memory"]);
    } else if (name == "Threads" && feature("smp", false)) {
        held_["cores"] = "cores " + value;
        to_engine_(held_["cores"]);
    } else if (name == "SyzygyPath" && !value.empty() && value != "<empty>" &&
               features_["egt"].find("syzygy") != std::string::npos) {
        held_["egtpath"] = "egtpath syzygy " + value;
        to_engine_(held_["egtpath"]);
    } else {
        info("no such option: " + name);
    }
}

void Adapter::position(const std::vector<std::string>& tokens) {
    std::string fen = std::string(chess::constants::STARTPOS);
    size_t i        = 1;
    if (i < tokens.size() && tokens[i] == "startpos") {
        ++i;
    } else if (i < tokens.size() && tokens[i] == "fen") {
        ++i;
        std::string f;
        for (; i < tokens.size() && tokens[i] != "moves"; ++i) f += (f.empty() ? "" : " ") + tokens[i];
        fen = f;
    }
    std::vector<std::string> moves;
    if (i < tokens.size() && tokens[i] == "moves") {
        for (++i; i < tokens.size(); ++i) moves.push_back(tokens[i]);
    }
    abandonSearch();
    gui_fen_   = fen;
    gui_moves_ = moves;

    // The engine follows the game: only the moves it has not seen yet are sent. Anything else starts it again.
    const bool extends = moves.size() >= moves_.size() && std::equal(moves_.begin(), moves_.end(), moves.begin());
    if (new_game_ || !in_sync_ || fen != start_fen_ || !extends) {
        newGame(fen);
    } else {
        to_engine_("force");
    }
    sendMoves(moves);
}

void Adapter::sendMoves(const std::vector<std::string>& moves) {
    for (size_t k = moves_.size(); k < moves.size(); ++k) {
        const auto move = parseMove(board_, moves[k]);
        if (!move) {
            info("illegal move from the GUI: " + moves[k]);
            in_sync_ = false;
            return;
        }
        const auto text = moveText(board_, *move);
        to_engine_(text);
        sent_moves_.push_back(text.substr(startsWith(text, "usermove ") ? 9 : 0));
        board_.makeMove(*move);
        moves_.push_back(moves[k]);
    }
}

// The GUI moved on while a search was still running: its move, if it comes, answers nothing.
void Adapter::abandonSearch() {
    if (!searching_) return;
    searching_ = false;
    stale_     = true;
    move_deadline_.reset();
    claim_deadline_.reset();
}

void Adapter::sendHeld() {
    for (const auto& [name, command] : held_) to_engine_(command);
}

void Adapter::newGame(const std::string& fen) {
    new_game_   = false;
    in_sync_    = true;
    level_sent_ = false;
    resigned_   = false;
    start_fen_  = fen;
    moves_.clear();
    sent_moves_.clear();
    board_ = chess::Board(fen, chess960_);

    // As WinBoard: memory, cores and egtpath before every new game.
    sendHeld();
    to_engine_("new");
    if (chess960_) to_engine_("variant fischerandom");
    to_engine_("force");
    if (fen != chess::constants::STARTPOS || chess960_) {
        if (feature("setboard", false)) {
            to_engine_("setboard " + (chess960_ ? xfen(fen) : fen));
        } else {
            // Protocol 1: edit mode, which keeps the side to move (black to move: a white move first) and
            // assumes castling rights wherever king and rook stand at home.
            if (board_.sideToMove() == chess::Color::BLACK) {
                const chess::Board start;
                to_engine_(moveText(start, chess::uci::uciToMove(start, "a2a3")));
            }
            const auto castling = fen.size() > 0 ? split(fen) : std::vector<std::string>{};
            if (castling.size() > 2 && castling[2] == "-" &&
                ((board_.at(chess::Square::SQ_E1) == chess::Piece::WHITEKING &&
                  (board_.at(chess::Square::SQ_H1) == chess::Piece::WHITEROOK ||
                   board_.at(chess::Square::SQ_A1) == chess::Piece::WHITEROOK)) ||
                 (board_.at(chess::Square::SQ_E8) == chess::Piece::BLACKKING &&
                  (board_.at(chess::Square::SQ_H8) == chess::Piece::BLACKROOK ||
                   board_.at(chess::Square::SQ_A8) == chess::Piece::BLACKROOK)))) {
                info("edit mode gives the engine castling rights this position does not have");
            }
            to_engine_("edit");
            to_engine_("#");
            for (const auto colour : {chess::Color::WHITE, chess::Color::BLACK}) {
                if (colour == chess::Color::BLACK) to_engine_("c");
                for (int sq = 0; sq < 64; ++sq) {
                    const auto piece = board_.at(chess::Square(sq));
                    if (piece == chess::Piece::NONE || piece.color() != colour) continue;
                    const char letter = "PNBRQK"[static_cast<int>(piece.type())];
                    to_engine_(std::string(1, letter) + std::string(chess::Square(sq)));
                }
            }
            to_engine_(".");
        }
    }
    to_engine_("post");
    to_engine_("hard");  // "easy" alone toggles pondering in some old engines: WinBoard sends both
    to_engine_("easy");
    if (!features_.empty()) to_engine_("computer");  // protocol 2
}

void Adapter::go(const std::vector<std::string>& tokens) {
    if (resigned_) {
        resigned_ = false;
        to_gui_("bestmove resign");
        return;
    }
    if (!in_sync_) {
        // The engine lost track (it rejected a move, or moved on its own): set it up again.
        info("setting the engine up again");
        newGame(gui_fen_);
        sendMoves(gui_moves_);
    }

    const bool white    = board_.sideToMove() == chess::Color::WHITE;
    const auto mine     = goValue(tokens, white ? "wtime" : "btime");
    const auto theirs   = goValue(tokens, white ? "btime" : "wtime");
    const auto inc      = goValue(tokens, white ? "winc" : "binc");
    const auto movetime = goValue(tokens, "movetime");
    engine_colour_      = board_.sideToMove();

    if (!level_sent_) {
        level_sent_ = true;
        if (movetime) {
            // Whole seconds, rounded down (it must not run over); "?" at the end of shorter ones.
            to_engine_(fmt::format("st {}", std::max(1LL, *movetime / 1000)));
        } else if (mine) {
            to_engine_(levelCommand(goValue(tokens, "movestogo").value_or(0), *mine, inc.value_or(0)));
        } else if (const auto nodes = goValue(tokens, "nodes")) {
            if (feature("nps", false)) {
                to_engine_("st 1");
                to_engine_(fmt::format("nps {}", *nodes));
            } else {
                info("the engine does not support a node limit (no nps feature): 1 second per move instead");
                to_engine_("st 1");
            }
        } else {
            to_engine_("st 86400");  // depth only, or infinite
        }
        if (const auto depth = goValue(tokens, "depth")) to_engine_(fmt::format("sd {}", *depth));
    }

    // The clocks, in centiseconds, at least 1. Colours as WinBoard sends them to engines with colors=1 (the
    // default): the other colour before the clocks (a GNU Chess kludge), the engine's own before go.
    const bool colours = feature("colors", true);
    const auto own     = mine ? mine : movetime;
    if (own && feature("time", true)) {
        if (colours) to_engine_(white ? "black" : "white");
        to_engine_(fmt::format("time {}", std::max(1LL, *own / 10)));
        to_engine_(fmt::format("otim {}", std::max(1LL, theirs.value_or(*own) / 10)));
    }
    if (colours) to_engine_(white ? "white" : "black");
    searching_ = true;
    stale_     = false;
    move_deadline_.reset();
    if (movetime) move_deadline_ = now_ + std::chrono::milliseconds(*movetime);
    to_engine_("go");
}

std::string Adapter::moveText(const chess::Board& board, chess::Move move) const {
    // In Chess960 castling is always O-O or O-O-O (the protocol's rule: coordinates would be ambiguous).
    const auto text = chess960_ && move.typeOf() == chess::Move::CASTLING ? chess::uci::moveToSan(board, move)
                      : feature("san", false)                             ? chess::uci::moveToSan(board, move)
                                                                          : uciMove(move, chess960_);
    return feature("usermove", false) ? "usermove " + text : text;
}

// ---------------------------------------------------------------------------
// From the engine

void Adapter::fromEngine(const std::string& raw, Clock::time_point now) {
    now_            = now;
    const auto line = trim(raw);
    if (line.empty() || line[0] == '#') return;
    const auto tokens = split(line);
    const auto& cmd   = tokens[0];
    const auto lower  = [&] {
        std::string s = line;
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }();

    if (cmd == "feature") {
        onFeatures(line.substr(7));
        if (init_ == Init::Waiting) {
            const bool done0 = features_.count("done") && features_["done"] == "0";
            init_deadline_   = now + (done0 ? FEATURE_DONE0_TIMEOUT : FEATURE_TIMEOUT);
        }
        return;
    }
    if (startsWith(cmd, "telluser") || cmd == "tellusererror") {
        info(line);
        return;
    }
    if (startsWith(line, "Error")) {
        info(line);
        if (init_ == Init::Waiting && line.find("protover") != std::string::npos) finishInit();  // protocol 1
        if (line.find("edit") != std::string::npos) {
            info(
                "the engine cannot start from a position (it has neither setboard nor edit): give it openings as "
                "moves (a PGN book), not as positions");
        }
        return;
    }
    if (init_ == Init::Waiting) return;  // a banner

    if (cmd == "pong") {
        if (pending_pong_ && tokens.size() > 1 && toInt(tokens[1]) == *pending_pong_) {
            pending_pong_.reset();
            stale_ = false;  // everything it sent before is in
            to_gui_("readyok");
        }
    } else if (cmd == "ping" && tokens.size() > 1) {
        to_engine_("pong " + tokens[1]);
    } else if (cmd == "move" && tokens.size() > 1) {
        engineMove(tokens[1]);
    } else if (tokens.size() >= 3 && tokens[1] == "..." &&
               toInt(std::string_view(cmd).substr(0, cmd.find_last_not_of('.') + 1))) {
        engineMove(tokens[2]);  // protocol 1: "1. ... e2e4"
    } else if (startsWith(lower, "illegal move")) {
        // "Illegal move: e2e4", "Illegal move (reason): e2e4", "Illegal move (no matching move)e2e4". Some engines
        // say it of commands they do not know: only one about a move we sent means the engine lost track.
        const auto colon = line.rfind(':');
        const auto paren = line.rfind(')');
        auto what        = colon != std::string::npos   ? line.substr(colon + 1)
                           : paren != std::string::npos ? line.substr(paren + 1)
                                                        : line.substr(12);
        what             = trim(what);
        if (std::find(sent_moves_.begin(), sent_moves_.end(), what) != sent_moves_.end()) {
            info("the engine rejects a move: " + line);
            in_sync_ = false;
        }
    } else if (cmd == "resign" || cmd == "1-0" || cmd == "0-1" || cmd == "1/2-1/2" || cmd == "*" ||
               startsWith(lower, "computer resigns") || startsWith(lower, "white resigns") ||
               startsWith(lower, "black resigns") || startsWith(lower, "white mates") ||
               startsWith(lower, "black mates") || startsWith(lower, "checkmate") ||
               startsWith(lower, "computer mates") || startsWith(lower, "opponent mates") ||
               startsWith(lower, "draw") || startsWith(lower, "game is a draw")) {
        engineResult(lower);
    } else if (std::isdigit(static_cast<unsigned char>(cmd[0]))) {
        thinking(line);
    }
}

void Adapter::onFeatures(const std::string& args) {
    for (const auto& [name, value] : parseFeatures(args)) {
        const bool known = std::find(KNOWN_FEATURES.begin(), KNOWN_FEATURES.end(), name) != KNOWN_FEATURES.end();
        to_engine_((known ? "accepted " : "rejected ") + name);
        if (!known) continue;
        if (name == "option") {
            if (auto option = parseOption(value)) {
                auto it = std::find_if(options_.begin(), options_.end(),
                                       [&](const auto& o) { return o.name == option->name; });
                if (it != options_.end()) {
                    *it = std::move(*option);
                } else {
                    options_.push_back(std::move(*option));
                }
            }
            continue;
        }
        features_[name] = value;
        if (name == "myname" && !value.empty()) name_ = value;
        if (name == "done" && value == "1") finishInit();
    }
}

// A result from the engine. Its own loss is a resignation; anything else is a claim: the GUI judges the game itself,
// but an engine that claimed may stop playing.
void Adapter::engineResult(const std::string& lower) {
    const auto first   = split(lower).front();
    const bool white   = engine_colour_ == chess::Color::WHITE;
    const bool resigns = first == "resign" || startsWith(lower, "computer resigns") ||
                         (white && (first == "0-1" || startsWith(lower, "white resigns"))) ||
                         (!white && (first == "1-0" || startsWith(lower, "black resigns")));
    if (resigns) {
        if (searching_) {
            searching_ = false;
            claim_deadline_.reset();
            move_deadline_.reset();
            to_gui_("bestmove resign");
        } else if (!stale_) {
            resigned_ = true;  // on the opponent's move: the answer to the next go
        }
        return;
    }
    info("the engine claims " + lower);
    if (searching_) claim_deadline_ = now_ + CLAIM_TIMEOUT;
}

void Adapter::engineMove(const std::string& text) {
    if (!searching_ && (stale_ || pending_pong_)) {
        // The move of a search the GUI gave up on.
        stale_ = false;
        if (ready_deadline_) {
            ready_deadline_.reset();
            to_gui_("readyok");
        }
        return;
    }
    if (!searching_) {
        // Moving when it was not asked to (in force mode): its board is not the game's any more.
        info("the engine moved when it was not its turn: " + text);
        in_sync_ = false;
        return;
    }
    searching_ = false;
    move_deadline_.reset();
    claim_deadline_.reset();
    const auto move = parseMove(board_, text);
    if (!move) {
        in_sync_ = false;
        info("illegal move from the engine: " + text);
        to_gui_("bestmove " + text);  // the GUI forfeits it
        return;
    }
    const auto uci = uciMove(*move, chess960_);
    board_.makeMove(*move);
    moves_.push_back(uci);
    to_gui_("bestmove " + uci);
}

// "ply score time nodes pv": time in centiseconds.
void Adapter::thinking(const std::string& line) {
    if (!searching_) return;
    const auto tokens = split(line);
    if (tokens.size() < 4) return;

    // A sign glued to the depth ("12&", "12.") means the time is in seconds, as xboard reads it.
    auto ply_text      = tokens[0];
    const bool seconds = !ply_text.empty() && !std::isdigit(static_cast<unsigned char>(ply_text.back()));
    while (!ply_text.empty() && !std::isdigit(static_cast<unsigned char>(ply_text.back()))) ply_text.pop_back();
    auto score_text = tokens[1];  // a bound may follow: "35?" "35!"
    while (!score_text.empty() && !std::isdigit(static_cast<unsigned char>(score_text.back()))) score_text.pop_back();
    const auto ply   = toInt(ply_text);
    const auto score = toInt(score_text);
    const auto time  = toInt(tokens[2]);
    const auto nodes = toInt(tokens[3]);
    if (!ply || !score || !time || !nodes) return;

    // Mate in N moves: +-(100000 + N). Older engines: just below a multiple of 1000 (31998, 99997...), as
    // cutechess reads them.
    std::string uci_score = fmt::format("cp {}", *score);
    const auto abs_score  = std::llabs(*score);
    if (abs_score >= 100000) {
        const auto n = std::max(1LL, abs_score - 100000);
        uci_score    = fmt::format("mate {}", *score > 0 ? n : -n);
    } else if (abs_score > 9900 && 1000 - abs_score % 1000 < 100) {
        const auto n = std::max(1LL, (1000 - abs_score % 1000 + 1) / 2);
        uci_score    = fmt::format("mate {}", *score > 0 ? n : -n);
    }

    // The PV, in coordinates, as far as it is legal.
    // Up to three numbers may come before it: selective depth, speed, tablebase hits.
    size_t first = 4;
    while (first < tokens.size() && first < 7 && toInt(tokens[first])) ++first;
    std::string pv;
    chess::Board board = board_;
    for (size_t i = first; i < tokens.size(); ++i) {
        auto t = tokens[i];
        if (t.find_first_of("({[<") == 0) break;
        // Move numbers: "12.", "12...", "..." or glued to the move, "12.Nf3".
        if (const auto dots = t.find_last_of('.'); dots != std::string::npos) t = t.substr(dots + 1);
        if (t.empty()) continue;
        const auto move = parseMove(board, t);
        if (!move) break;
        pv += " " + uciMove(*move, chess960_);
        board.makeMove(*move);
    }

    const auto ms = *time * (seconds ? 1000 : 10);
    to_gui_(fmt::format("info depth {} score {} time {} nodes {} nps {}{}", *ply, uci_score, ms, *nodes,
                        ms > 0 ? *nodes * 1000 / ms : 0, pv.empty() ? "" : " pv" + pv));
}

// ---------------------------------------------------------------------------
// The --wb2uci mode

int run(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::fprintf(stderr, "usage: fastchess-gui --wb2uci ENGINE [ARGS...]\n");
        return 2;
    }

#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);  // a write to an engine that just died
#endif

    std::string engine_args;
    for (size_t i = 1; i < args.size(); ++i) {
        const bool quote = args[i].empty() || args[i].find_first_of(" \t") != std::string::npos;
        engine_args += (i > 1 ? " " : "") + (quote ? "\"" + args[i] + "\"" : args[i]);
    }

    std::mutex mutex;

    // FCG_WB2UCI_LOG=FILE: every line exchanged, appended to FILE, to see what an engine does.
    std::FILE* log  = nullptr;
    const auto t0   = Clock::now();
    const auto name = fmt::format("{}#{}", std::filesystem::path(args[0]).stem().string(),
                                  std::chrono::steady_clock::now().time_since_epoch().count() % 100000);
    if (const char* path = std::getenv("FCG_WB2UCI_LOG")) log = std::fopen(path, "a");
    const auto trace = [&](const char* dir, const std::string& line) {
        if (!log) return;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
        std::fprintf(log, "%s %8lld %s %s\n", name.c_str(), static_cast<long long>(ms), dir, line.c_str());
        std::fflush(log);
    };

    const auto to_gui = [&](const std::string& line) {
        trace("<gui", line);
        std::fputs((line + "\n").c_str(), stdout);
        std::fflush(stdout);
    };

    fastchess::engine::process::Process process;
    process.setRealtimeLogging(false);
    Adapter adapter(
        to_gui,
        [&](const std::string& line) {
            trace(">eng", line);
            (void)process.writeInput(line + "\n");
        },
        std::filesystem::path(args[0]).stem().string());

    process.setLineCallback([&](const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex);
        trace("<eng", line);
        adapter.fromEngine(line, Clock::now());
    });
    // The engine runs in our working directory, which fastchess set to the engine's.
    if (auto started = process.init("", args[0], engine_args, "wb2uci"); !started) {
        to_gui("info string cannot start " + args[0] + ": " + started.message);
        return 1;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        adapter.start(Clock::now());
    }

    // The engine's output; when it ends, so does the adapter, and the GUI sees the engine stop.
    std::thread([&] {
        std::vector<fastchess::engine::process::Line> lines;
        std::string r_message;
        while (true) {
            // "move" only bounds how many lines accumulate; each line goes through the callback.
            const auto r = process.readOutput(lines, "move", std::chrono::milliseconds(0));
            if (r.code == fastchess::engine::process::Status::ERR) {
                r_message = r.message;
                break;
            }
        }
        bool quitting = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            quitting = adapter.quitting();
            trace("---", "engine output closed: " + r_message);
            if (!quitting) to_gui("info string the engine stopped");
        }
        std::fflush(stdout);
        std::_Exit(quitting ? 0 : 1);
    }).detach();

    std::thread([&] {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            std::lock_guard<std::mutex> lock(mutex);
            adapter.tick(Clock::now());
        }
    }).detach();

    std::string line;
    while (std::getline(std::cin, line)) {
        std::lock_guard<std::mutex> lock(mutex);
        trace(">gui", line);
        adapter.fromGui(line, Clock::now());
        if (adapter.quitting()) break;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!adapter.quitting()) adapter.fromGui("quit", Clock::now());
    }

    // A moment for the engine to quit (xboard waits a second), then it is killed.
    const auto until = Clock::now() + std::chrono::seconds(1);
    while (Clock::now() < until && process.alive()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    process.forceKill();
    std::fflush(stdout);
    std::_Exit(0);
}

}  // namespace fcg::wb2uci
