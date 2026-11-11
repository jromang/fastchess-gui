#include "legacy.hpp"

#include <fstream>
#include <map>
#include <stdexcept>

#include <json.hpp>

#include "model.hpp"
#include "pgn_file.hpp"

#include <types/match_data.hpp>

namespace fcg::tour {

namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + file.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// One game of a PGN file: where it is and its headers.
struct PgnGame {
    PgnSpan span;
    std::map<std::string, std::string> headers;
};

std::vector<PgnGame> splitGames(const std::string& text) {
    std::vector<std::size_t> starts;
    for (std::size_t pos = 0; pos < text.size();) {
        if (text.compare(pos, 7, "[Event ") == 0) starts.push_back(pos);
        const auto nl = text.find('\n', pos);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }

    std::vector<PgnGame> games;
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const auto end = i + 1 < starts.size() ? starts[i + 1] : text.size();
        PgnGame game;
        game.span = {starts[i], static_cast<uint32_t>(end - starts[i])};

        // Header lines: [Key "Value"]
        for (std::size_t pos = starts[i]; pos < end && text[pos] == '[';) {
            const auto nl    = text.find('\n', pos);
            const auto line  = text.substr(pos, (nl == std::string::npos ? end : nl) - pos);
            const auto space = line.find(' ');
            const auto q1    = line.find('"');
            const auto q2    = line.rfind('"');
            if (space != std::string::npos && q1 != std::string::npos && q2 > q1) {
                game.headers[line.substr(1, space - 1)] = line.substr(q1 + 1, q2 - q1 - 1);
            }
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
        games.push_back(std::move(game));
    }
    return games;
}

fastchess::MatchTermination termination(const std::string& header) {
    using fastchess::MatchTermination;
    if (header == "adjudication") return MatchTermination::ADJUDICATION;
    if (header == "abandoned") return MatchTermination::DISCONNECT;
    if (header == "time forfeit") return MatchTermination::TIMEOUT;
    if (header == "illegal move") return MatchTermination::ILLEGAL_MOVE;
    return MatchTermination::NORMAL;
}

uint32_t durationMs(const std::string& text) {
    int h = 0, m = 0, s = 0;
    if (std::sscanf(text.c_str(), "%d:%d:%d", &h, &m, &s) != 3) return 0;
    return static_cast<uint32_t>((h * 3600 + m * 60 + s) * 1000);
}

}  // namespace

bool isLegacyRun(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "config.json", ec) && fs::is_regular_file(dir / "games.pgn", ec) &&
           !fs::exists(dir / "tournament.json", ec);
}

std::vector<fs::path> legacyRuns(const fs::path& root) {
    std::vector<fs::path> dirs;
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), last; !ec && it != last; it.increment(ec)) {
        if (it->is_directory(ec) && isLegacyRun(it->path())) dirs.push_back(it->path());
    }
    return dirs;
}

void importLegacyRun(const fs::path& dir) {
    if (!isLegacyRun(dir)) throw std::runtime_error(dir.string() + " is not a match of an earlier version");

    TournamentSpec spec;
    try {
        const auto config = nlohmann::json::parse(readFile(dir / "config.json"));
        config.get_to(spec.conditions);
        const auto engines = config.at("engines").get<std::vector<fastchess::EngineConfiguration>>();
        if (engines.size() != 2) throw std::runtime_error("not a two-engine match");
        for (int i = 0; i < 2; ++i) spec.participants.push_back({i, engines[i]});
    } catch (const std::exception& e) {
        throw std::runtime_error((dir / "config.json").string() + ": " + e.what());
    }

    const auto& a = spec.participants[0].engine.name;
    const auto& b = spec.participants[1].engine.name;
    spec.name     = a + " vs " + b;
    spec.format   = Format::Match;

    const auto games    = splitGames(readFile(dir / "games.pgn"));
    const auto cb_games = fs::exists(dir / "games-chessbase.pgn") ? splitGames(readFile(dir / "games-chessbase.pgn"))
                                                                  : std::vector<PgnGame>{};

    nlohmann::json journal = nlohmann::json::array();
    std::map<uint64_t, bool> seen;
    for (std::size_t i = 0; i < games.size(); ++i) {
        const auto& h     = games[i].headers;
        const auto white  = h.count("White") ? h.at("White") : "";
        const auto black  = h.count("Black") ? h.at("Black") : "";
        const auto result = h.count("Result") ? h.at("Result") : "*";
        if (result == "*" || !((white == a && black == b) || (white == b && black == a))) continue;

        // fastchess numbered a pair of games by its round; the engine listed first had White in game 0.
        const uint32_t encounter = h.count("Round")
                                       ? static_cast<uint32_t>(std::max(1, std::atoi(h.at("Round").c_str())) - 1)
                                       : static_cast<uint32_t>(i / 2);
        const int game           = white == a ? 0 : 1;
        if (!seen.emplace(uint64_t{encounter} * 2 + game, true).second) continue;

        const auto term = termination(h.count("Termination") ? h.at("Termination") : "");
        journal.push_back({{"t", "game_finished"},
                           {"encounter", encounter},
                           {"game", game},
                           {"white", white == a ? 0 : 1},
                           {"black", white == a ? 1 : 0},
                           {"result", result},
                           {"termination", static_cast<int>(term)},
                           {"reason", h.count("Termination") ? h.at("Termination") : ""},
                           {"plies", h.count("PlyCount") ? std::atoi(h.at("PlyCount").c_str()) : 0},
                           {"ms", durationMs(h.count("GameDuration") ? h.at("GameDuration") : "")},
                           {"pgn", {games[i].span.offset, games[i].span.length}},
                           {"cb", i < cb_games.size() ? nlohmann::json{cb_games[i].span.offset, cb_games[i].span.length}
                                                      : nlohmann::json{0, 0}},
                           {"at", 0}});
    }
    journal.push_back({{"t", "finished"}, {"why", "imported"}});

    // The journal first: a folder with a journal but no tournament.json is still seen as legacy and imported again.
    {
        std::ofstream out(dir / "journal.jsonl", std::ios::binary | std::ios::trunc);
        for (const auto& event : journal)
            out << event.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
        if (!out) throw std::runtime_error("cannot write " + (dir / "journal.jsonl").string());
    }
    {
        const auto tmp = dir / "tournament.json.tmp";
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << nlohmann::json(spec).dump(4, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
        out.close();
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
        fs::rename(tmp, dir / "tournament.json");
    }
}

}  // namespace fcg::tour
