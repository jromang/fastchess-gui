#include "ratinglist.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

#include <fmt/include/fmt/core.h>

#include "journal.hpp"
#include "state.hpp"

namespace fcg::tour {

namespace fs = std::filesystem;

namespace {

std::string seconds(int64_t ms) {
    if (ms % 1000 == 0) return std::to_string(ms / 1000);
    return fmt::format("{:g}", ms / 1000.0);
}

}  // namespace

std::string timeControlText(const fastchess::Limit& limit) {
    if (limit.nodes > 0) return fmt::format("{} nodes", limit.nodes);
    const auto& tc = limit.tc;
    return fmt::format("{}{}+{}", tc.moves > 0 ? fmt::format("{}/", tc.moves) : "", seconds(tc.time),
                       seconds(tc.increment));
}

RatingList buildRatingList(const std::vector<fs::path>& dirs, const std::string& time_control,
                           const std::optional<std::pair<std::string, double>>& anchor) {
    RatingList list;
    std::map<std::string, std::size_t> index;
    std::vector<RatingEntry> entries;
    std::vector<std::set<std::string>> seen_in;  // tournaments of each engine
    std::vector<std::vector<double>> games, points;
    std::set<std::string> tcs;

    const auto id = [&](const std::string& name) {
        const auto it = index.find(name);
        if (it != index.end()) return it->second;
        const auto i = entries.size();
        index[name]  = i;
        entries.push_back(RatingEntry{name});
        seen_in.emplace_back();
        for (auto& row : games) row.push_back(0);
        for (auto& row : points) row.push_back(0);
        games.emplace_back(entries.size(), 0.0);
        points.emplace_back(entries.size(), 0.0);
        return i;
    };

    for (const auto& dir : dirs) {
        TournamentSpec spec;
        try {
            std::ifstream in(dir / "tournament.json", std::ios::binary);
            if (!in) continue;
            spec = nlohmann::json::parse(in).get<TournamentSpec>();
        } catch (const std::exception&) {
            continue;
        }
        if (spec.participants.empty()) continue;

        const auto tc = timeControlText(spec.participants.front().engine.limit);
        tcs.insert(tc);
        if (!time_control.empty() && tc != time_control) continue;

        TournamentState::Replayed replayed;
        try {
            replayed = TournamentState::replay(Journal::read(dir / "journal.jsonl", false), spec);
        } catch (const std::exception&) {
            continue;
        }
        if (replayed.results.empty()) continue;

        list.tournaments++;
        const auto name = [&](int pid) {
            const auto* p = spec.participant(pid);
            return p ? p->engine.name : std::string();
        };
        for (const auto& r : replayed.results) {
            const auto w = name(r.white), b = name(r.black);
            if (w.empty() || b.empty() || w == b) continue;
            const auto i = id(w), j = id(b);
            const double sw = r.scoreOf(r.white);
            games[i][j] += 1;
            games[j][i] += 1;
            points[i][j] += sw;
            points[j][i] += 1 - sw;
            entries[i].games++, entries[j].games++;
            entries[i].points += sw;
            entries[j].points += 1 - sw;
            seen_in[i].insert(dir.filename().string());
            seen_in[j].insert(dir.filename().string());
            list.games++;
        }
    }

    std::optional<std::pair<int, double>> fixed;
    if (anchor) {
        if (const auto it = index.find(anchor->first); it != index.end()) {
            fixed = std::make_pair(static_cast<int>(it->second), anchor->second);
        }
    }
    const auto ratings = maximumLikelihood(games, points, fixed);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        entries[i].rating      = ratings[i];
        entries[i].tournaments = static_cast<int>(seen_in[i].size());
    }
    std::sort(entries.begin(), entries.end(),
              [](const RatingEntry& a, const RatingEntry& b) { return a.rating.elo > b.rating.elo; });

    list.entries       = std::move(entries);
    list.time_controls = {tcs.begin(), tcs.end()};
    return list;
}

std::string ratingListText(const RatingList& list) {
    std::size_t width = 6;
    for (const auto& e : list.entries) width = std::max(width, e.name.size());
    std::string out = fmt::format("{:>4}  {:<{}}  {:>6}  {:>5}  {:>6}  {:>5}  {:>11}\n", "#", "Engine", width, "Elo",
                                  "+/-", "Games", "Score", "Tournaments");
    for (std::size_t i = 0; i < list.entries.size(); ++i) {
        const auto& e = list.entries[i];
        out += fmt::format("{:>4}  {:<{}}  {:>6}  {:>5.0f}  {:>6}  {:>4.1f}%  {:>11}\n", i + 1, e.name, width,
                           std::lround(e.rating.elo), e.rating.error, e.games, 100.0 * e.score(), e.tournaments);
    }
    out += fmt::format("\n{} games, {} tournaments\n", list.games, list.tournaments);
    return out;
}

std::string ratingListCsv(const RatingList& list) {
    std::string out = "rank,engine,elo,error,games,score,tournaments\n";
    for (std::size_t i = 0; i < list.entries.size(); ++i) {
        const auto& e = list.entries[i];
        auto name     = e.name;
        if (name.find_first_of(",\"") != std::string::npos) {
            std::string quoted = "\"";
            for (const char c : name) quoted += c == '"' ? std::string("\"\"") : std::string(1, c);
            name = quoted + "\"";
        }
        out += fmt::format("{},{},{:.1f},{:.1f},{},{:.4f},{}\n", i + 1, name, e.rating.elo, e.rating.error, e.games,
                           e.score(), e.tournaments);
    }
    return out;
}

}  // namespace fcg::tour
