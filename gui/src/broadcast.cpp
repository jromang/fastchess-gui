#include "broadcast.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

#include <json.hpp>
#include <tour/pgn_file.hpp>

#include <fmt/include/fmt/format.h>

namespace fcg {

namespace fs = std::filesystem;

namespace {

std::string half(double points) {
    const auto whole = static_cast<long>(std::floor(points + 1e-9));
    return points - whole > 0.25 ? fmt::format("{}.5", whole) : fmt::format("{}", whole);
}

bool writeFile(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    return !out.fail();
}

// On Windows a program reading the file (an uploader, a viewer) can keep it from being replaced: tried again for a
// moment, then written over in place, which a reader that shares writing allows. The temporary file never stays.
bool writeAtomically(const fs::path& file, const std::string& text) {
    const auto tmp = fs::path(file).concat(".tmp");
    std::error_code ec;
    if (!writeFile(tmp, text)) {
        fs::remove(tmp, ec);
        return false;
    }
#ifdef _WIN32
    constexpr int TRIES = 6;
#else
    constexpr int TRIES = 1;
#endif
    for (int i = 0; i < TRIES; ++i) {
        if (i > 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        fs::rename(tmp, file, ec);
        if (!ec) return true;
    }
    const bool ok = writeFile(file, text);
    fs::remove(tmp, ec);
    return ok;
}

std::string_view trimmed(std::string_view text) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && space(text.front())) text.remove_prefix(1);
    while (!text.empty() && space(text.back())) text.remove_suffix(1);
    return text;
}

// The text, null when empty: what the page shows nothing for.
nlohmann::json orNull(const std::string& text) { return text.empty() ? nlohmann::json(nullptr) : nlohmann::json(text); }

}  // namespace

std::string webChatLink(std::string_view text) {
    constexpr std::string_view scheme = "https://";

    text = trimmed(text);
    if (text.size() <= scheme.size() || text.size() > 2048) return {};
    for (std::size_t i = 0; i < scheme.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) != scheme[i]) return {};
    }
    // Printable ASCII only, and nothing that could end an attribute, a string or a tag where it is put.
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u >= 0x7F || c == '"' || c == '\'' || c == '<' || c == '>' || c == '\\' || c == '`') return {};
    }
    // A host: the scheme's slashes followed by something else than a path, a query or a fragment.
    if (const char c = text[scheme.size()]; c == '/' || c == '?' || c == '#') return {};
    return std::string("https://") + std::string(text.substr(scheme.size()));
}

std::string webDiscordId(std::string_view text) {
    text = trimmed(text);
    if (text.size() < 17 || text.size() > 20) return {};
    if (!std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) return {};
    return std::string(text);
}

std::string snapshotJson(const BroadcastSnapshot& s) {
    nlohmann::json j;
    j["name"]         = s.name;
    j["format"]       = s.format;
    j["time_control"] = s.time_control;
    j["chess960"]     = s.chess960;
    j["played"]       = s.played;
    j["total"]        = s.total;
    j["finished"]     = s.finished;

    auto& games = j["games"] = nlohmann::json::array();
    for (const auto& g : s.games) {
        nlohmann::json game = {{"slot", g.slot},
                               {"number", g.number},
                               {"white", g.white},
                               {"black", g.black},
                               {"white_elo", g.white_elo},
                               {"black_elo", g.black_elo},
                               {"fen", g.fen},
                               {"start_fen", g.start_fen},
                               {"moves", g.san},
                               {"uci", g.uci},
                               {"white_to_move", g.white_to_move},
                               {"clocks", g.clocks},
                               {"white_ms", g.white_ms},
                               {"black_ms", g.black_ms},
                               {"active", g.active},
                               {"result", g.result}};
        for (int side = 0; side < 2; ++side) {
            const auto& sr = g.search[side];
            // What the engine did not tell is null.
            const auto told = [](auto v) { return v > 0 ? nlohmann::json(v) : nlohmann::json(nullptr); };
            game[side == 0 ? "white_search" : "black_search"] = {
                {"depth", sr.depth},
                {"score_cp", sr.score_cp},
                {"nodes", sr.nodes},
                {"time_ms", sr.time_ms},
                {"pv", sr.pv},
                {"pv_san", sr.pv_san},
                {"seldepth", told(sr.seldepth)},
                {"nps", told(sr.nps)},
                {"tbhits", sr.depth ? nlohmann::json(sr.tbhits) : nlohmann::json(nullptr)},
                {"hashfull", sr.hashfull >= 0 ? nlohmann::json(sr.hashfull) : nlohmann::json(nullptr)},
                {"current", sr.current}};
        }
        // By ply, for the charts: White's evaluation (null for a book move or none), depth and time; and which moves
        // came from the opening book.
        auto& evals = game["evals"] = nlohmann::json::array();
        auto& depths = game["depths"] = nlohmann::json::array();
        auto& times = game["times_ms"] = nlohmann::json::array();
        auto& book = game["book"] = nlohmann::json::array();
        for (const auto& p : g.plies) {
            evals.push_back(p.score_cp ? nlohmann::json(*p.score_cp) : nlohmann::json(nullptr));
            depths.push_back(p.book ? nlohmann::json(nullptr) : nlohmann::json(p.depth));
            times.push_back(p.book ? nlohmann::json(nullptr) : nlohmann::json(p.time_ms));
            book.push_back(p.book);
        }
        games.push_back(std::move(game));
    }

    const auto& st  = s.stats;
    j["stats"]      = {{"games", st.games},
                       {"white_wins", st.white_wins},
                       {"black_wins", st.black_wins},
                       {"draws", st.draws},
                       {"plies", st.plies},
                       {"duration_ms", st.duration_ms},
                       {"adjudications", st.adjudications},
                       {"time_losses", st.time_losses},
                       {"crashes", st.crashes},
                       {"stalls", st.stalls},
                       {"illegal_moves", st.illegal_moves}};
    auto& anomalies = j["anomalies"] = nlohmann::json::array();
    for (const auto& a : s.anomalies) {
        anomalies.push_back({{"number", a.number},
                             {"white", a.white},
                             {"black", a.black},
                             {"result", a.result},
                             {"kind", a.kind},
                             {"engine", a.engine},
                             {"reason", a.reason}});
    }
    // Each engine's search on average per move, and its anomalies; null where its moves did not tell.
    auto& engines = j["engines"] = nlohmann::json::array();
    for (const auto& e : s.engines) {
        const auto known = [&](auto v) { return e.moves && v ? nlohmann::json(v) : nlohmann::json(nullptr); };
        engines.push_back({{"name", e.name},
                           {"moves", e.moves},
                           {"depth", e.moves ? nlohmann::json(std::round(e.depth * 10) / 10) : nlohmann::json(nullptr)},
                           {"time_ms", e.moves ? nlohmann::json(e.time_ms) : nlohmann::json(nullptr)},
                           {"nodes", known(e.nodes)},
                           {"nps", known(e.nps)},
                           {"time_losses", e.time_losses},
                           {"crashes", e.crashes},
                           {"stalls", e.stalls},
                           {"illegal_moves", e.illegal_moves}});
    }
    auto& next = j["next"] = nlohmann::json::array();
    for (const auto& n : s.next) {
        next.push_back({{"number", n.number},
                        {"round", n.round},
                        {"cycle", n.cycle},
                        {"white", n.white},
                        {"black", n.black},
                        {"opening", n.opening.empty() ? nlohmann::json(nullptr) : nlohmann::json(n.opening)}});
    }
    // The name of the PGN file the page offers, not where it is.
    j["pgn"] = s.pgn_file.empty() ? nlohmann::json(nullptr) : nlohmann::json(s.pgn_name);

    auto& rows = j["standings"] = nlohmann::json::array();
    for (std::size_t i = 0; i < s.table.rows.size(); ++i) {
        const auto& r      = s.table.rows[i];
        nlohmann::json row = {{"rank", i + 1},       {"name", r.name},
                              {"games", r.games},    {"points", r.points},
                              {"wins", r.wins},      {"draws", r.draws},
                              {"losses", r.losses},  {"sb", r.sb},
                              {"elo", r.rating.elo}, {"elo_error", r.rating.error}};
        auto& cells = row["crosstable"] = nlohmann::json::array();
        for (const auto& c : s.table.rows) {
            if (c.id == r.id) {
                cells.push_back(nullptr);
                continue;
            }
            const auto p = s.table.pair(r.id, c.id);
            cells.push_back({{"wins", p.wins}, {"draws", p.draws}, {"losses", p.losses}});
        }
        rows.push_back(std::move(row));
    }
    j["recent"] = s.recent;
    // Where the viewers talk; only what passes the checks, whoever filled the snapshot.
    j["chat"]    = orNull(webChatLink(s.chat_link));
    j["lichess"] = orNull(webChatLink(s.lichess_url));
    j["discord"] = orNull(webDiscordId(s.discord_id));
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string livePgn(const BroadcastSnapshot& s) {
    std::string out;
    for (const auto& g : s.games) {
        out += fmt::format("[Event \"{}\"]\n[Round \"{}\"]\n[White \"{}\"]\n[Black \"{}\"]\n[Result \"{}\"]\n",
                           tour::pgnTagValue(s.name), g.number, tour::pgnTagValue(g.white), tour::pgnTagValue(g.black),
                           g.active ? "*" : g.result);
        if (g.white_elo > 0) out += fmt::format("[WhiteElo \"{}\"]\n", g.white_elo);
        if (g.black_elo > 0) out += fmt::format("[BlackElo \"{}\"]\n", g.black_elo);
        if (g.start_fen != "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {
            out += fmt::format("[SetUp \"1\"]\n[FEN \"{}\"]\n", g.start_fen);
        }
        if (g.clocks) {
            out += fmt::format("[WhiteClock \"{}\"]\n[BlackClock \"{}\"]\n", g.white_ms, g.black_ms);
        }
        out += "\n";

        // The side to move at the start: from the FEN.
        const auto space = g.start_fen.find(' ');
        bool white       = space == std::string::npos || g.start_fen[space + 1] == 'w';
        const auto last  = g.start_fen.rfind(' ');
        int number       = last == std::string::npos ? 1 : std::max(1, std::atoi(g.start_fen.c_str() + last + 1));
        std::string line;
        for (std::size_t i = 0; i < g.san.size(); ++i) {
            std::string token;
            if (white) {
                token = fmt::format("{}. {}", number, g.san[i]);
            } else {
                token = i == 0 ? fmt::format("{}... {}", number, g.san[i]) : g.san[i];
                ++number;
            }
            white = !white;
            if (line.size() + token.size() > 79) {
                out += line + "\n";
                line.clear();
            }
            line += (line.empty() ? "" : " ") + token;
        }
        out += line + (line.empty() ? "" : " ") + (g.active ? "*" : g.result) + "\n\n";
    }
    return out;
}

std::vector<std::string> tlcsCrosstable(const BroadcastSnapshot& s, uint64_t total_games) {
    const auto& rows   = s.table.rows;
    std::size_t name_w = 6;
    for (const auto& r : rows) name_w = std::max(name_w, r.name.size());

    // Each cell holds one character per game against that opponent: 1 won, = drawn, 0 lost.
    std::size_t cell_w = 3;
    for (const auto& r : rows) {
        for (const auto& c : rows) {
            if (c.id != r.id) cell_w = std::max<std::size_t>(cell_w, s.table.pair(r.id, c.id).games() + 1);
        }
    }

    std::vector<std::string> out;
    std::string header = fmt::format("RANK  {:<{}}  GAMES  POINTS ", "ENGINE", name_w);
    for (std::size_t c = 0; c < rows.size(); ++c) header += fmt::format("{:<{}}", c + 1, cell_w);
    out.push_back(header);
    out.push_back(std::string(header.size(), '-'));

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        // Left aligned: a TLCS client trims the start of what follows "CT:", which would shift the columns.
        const auto rank  = fmt::format("{}.", i + 1);
        std::string line = fmt::format("{:<6}{:<{}}  {:>5}  {:>6} ", rank, r.name, name_w, r.games, half(r.points));
        for (const auto& c : rows) {
            std::string cell;
            if (c.id == r.id) {
                cell = "**";
            } else {
                const auto p = s.table.pair(r.id, c.id);
                cell         = std::string(static_cast<std::size_t>(p.wins), '1') +
                               std::string(static_cast<std::size_t>(p.draws), '=') +
                               std::string(static_cast<std::size_t>(p.losses), '0');
                if (cell.empty()) cell = ".";
            }
            line += fmt::format("{:<{}}", cell, cell_w);
        }
        out.push_back(line);
    }
    out.push_back("");
    out.push_back(fmt::format("total games = {}", total_games));

    // The games, newest first, as TLCS lists them after the table; node-tlcv numbers its games from them. Three
    // spaces at least between the columns.
    if (!s.played_games.empty()) {
        std::size_t white_w = 5, black_w = 5;
        for (const auto& g : s.played_games) {
            white_w = std::max(white_w, g.white.size());
            black_w = std::max(black_w, g.black.size());
        }
        out.push_back("");
        out.push_back(fmt::format("Game No.  {:<{}}   {:<{}}   Result", "White", white_w, "Black", black_w));
        out.push_back(std::string(white_w + black_w + 25, '-'));
        for (const auto& g : s.played_games) {
            out.push_back(
                fmt::format("{:>8}  {:<{}}   {:<{}}   {}", g.number, g.white, white_w, g.black, black_w, g.result));
        }
    }
    return out;
}

std::string writeBroadcastFiles(const BroadcastSnapshot& s, const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return "cannot create " + dir.string();

    bool ok = writeAtomically(dir / "live.pgn", livePgn(s));
    ok &= writeAtomically(dir / "standings.html", tour::tableHtml(s.table, s.name));
    ok &= writeAtomically(dir / "tournament.json", snapshotJson(s));
    return ok ? std::string() : "cannot write in " + dir.string();
}

}  // namespace fcg
