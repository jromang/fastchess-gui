#include "broadcast.hpp"

#include <cmath>
#include <fstream>

#include <json.hpp>

#include <fmt/include/fmt/format.h>

namespace fcg {

namespace fs = std::filesystem;

namespace {

std::string half(double points) {
    const auto whole = static_cast<long>(std::floor(points + 1e-9));
    return points - whole > 0.25 ? fmt::format("{}.5", whole) : fmt::format("{}", whole);
}

bool writeAtomically(const fs::path& file, const std::string& text) {
    const auto tmp = fs::path(file).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    return !ec;
}

}  // namespace

std::string snapshotJson(const BroadcastSnapshot& s) {
    nlohmann::json j;
    j["name"]     = s.name;
    j["format"]   = s.format;
    j["played"]   = s.played;
    j["total"]    = s.total;
    j["finished"] = s.finished;

    auto& games = j["games"] = nlohmann::json::array();
    for (const auto& g : s.games) {
        nlohmann::json game = {{"slot", g.slot},         {"number", g.number},
                               {"white", g.white},       {"black", g.black},
                               {"fen", g.fen},           {"start_fen", g.start_fen},
                               {"moves", g.san},         {"white_to_move", g.white_to_move},
                               {"clocks", g.clocks},     {"white_ms", g.white_ms},
                               {"black_ms", g.black_ms}, {"active", g.active},
                               {"result", g.result}};
        for (int side = 0; side < 2; ++side) {
            const auto& sr                                    = g.search[side];
            game[side == 0 ? "white_search" : "black_search"] = {{"depth", sr.depth},
                                                                 {"score_cp", sr.score_cp},
                                                                 {"nodes", sr.nodes},
                                                                 {"time_ms", sr.time_ms},
                                                                 {"pv", sr.pv}};
        }
        games.push_back(std::move(game));
    }

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
    return j.dump();
}

std::string livePgn(const BroadcastSnapshot& s) {
    std::string out;
    for (const auto& g : s.games) {
        out += fmt::format("[Event \"{}\"]\n[Round \"{}\"]\n[White \"{}\"]\n[Black \"{}\"]\n[Result \"{}\"]\n", s.name,
                           g.number, g.white, g.black, g.active ? "*" : g.result);
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
