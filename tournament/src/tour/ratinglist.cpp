#include "ratinglist.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <map>
#include <set>

#include <fmt/include/fmt/format.h>

#include "journal.hpp"
#include "pgn_file.hpp"
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
    if (limit.plies > 0) return fmt::format("depth {}", limit.plies);
    const auto& tc = limit.tc;
    if (tc.fixed_time > 0) return fmt::format("{}s/move", seconds(tc.fixed_time));
    return fmt::format("{}{}+{}", tc.moves > 0 ? fmt::format("{}/", tc.moves) : "", seconds(tc.time),
                       seconds(tc.increment));
}

namespace {

// Maximum likelihood ratings of the games of `db` for which `accept(game)` holds.
template <typename Accept>
RatingList rate(const GameDb& db, const NamedAnchor& anchor, int min_games, Accept&& accept) {
    RatingList list;
    const auto n = db.engines.size();
    std::vector<std::vector<double>> games(n, std::vector<double>(n, 0.0)), points = games;
    std::vector<RatingEntry> entries(n);
    std::vector<std::set<uint32_t>> sources(n);
    std::set<uint32_t> counted;
    std::set<std::string> tcs;

    for (const auto& g : db.games) {
        tcs.insert(db.time_controls[g.time_control]);
        if (!accept(g)) continue;
        const auto i = g.white, j = g.black;
        games[i][j] += 1;
        games[j][i] += 1;
        points[i][j] += g.white_score;
        points[j][i] += 1 - g.white_score;
        entries[i].games++, entries[j].games++;
        entries[i].points += g.white_score;
        entries[j].points += 1 - g.white_score;
        sources[i].insert(g.source), sources[j].insert(g.source);
        counted.insert(g.source);
        list.games++;
    }

    // Engines without a game are not rated at all.
    std::vector<uint32_t> present;
    for (uint32_t i = 0; i < n; ++i) {
        if (entries[i].games > 0) present.push_back(i);
    }
    std::vector<std::vector<double>> g2(present.size(), std::vector<double>(present.size())), p2 = g2;
    std::optional<std::pair<int, double>> fixed;
    for (std::size_t a = 0; a < present.size(); ++a) {
        for (std::size_t b = 0; b < present.size(); ++b) {
            g2[a][b] = games[present[a]][present[b]];
            p2[a][b] = points[present[a]][present[b]];
        }
        if (anchor && db.engines[present[a]] == anchor->first)
            fixed = std::make_pair(static_cast<int>(a), anchor->second);
    }
    const auto ratings = maximumLikelihood(g2, p2, fixed);
    for (std::size_t a = 0; a < present.size(); ++a) {
        auto& e       = entries[present[a]];
        e.name        = db.engines[present[a]];
        e.rating      = ratings[a];
        e.tournaments = static_cast<int>(sources[present[a]].size());
        if (e.games >= min_games) list.entries.push_back(e);
    }
    std::sort(list.entries.begin(), list.entries.end(),
              [](const RatingEntry& a, const RatingEntry& b) { return a.rating.elo > b.rating.elo; });

    list.tournaments   = static_cast<int>(counted.size());
    list.time_controls = {tcs.begin(), tcs.end()};
    return list;
}

}  // namespace

RatingList ratingList(const GameDb& db, const GameFilter& filter, const NamedAnchor& anchor, int min_games) {
    return rate(db, anchor, min_games, [&](const DbGame& g) { return filter.accepts(db, g); });
}

RatingList buildRatingList(const std::vector<fs::path>& dirs, const std::string& time_control,
                           const NamedAnchor& anchor) {
    GameFilter filter;
    filter.time_control = time_control;
    return ratingList(readGameDb(dirs), filter, anchor);
}

std::vector<RatingPoint> ratingHistory(const GameDb& db, const GameFilter& filter, const NamedAnchor& anchor,
                                       const std::string& engine) {
    const auto id = db.engine(engine);
    if (!id) return {};

    // The sources the engine played in, by the time of their last game.
    std::map<uint32_t, int64_t> last;
    for (const auto& g : db.games) {
        if ((g.white == *id || g.black == *id) && filter.accepts(db, g)) {
            last[g.source] = std::max(last[g.source], g.played_at);
        }
    }
    std::vector<std::pair<int64_t, uint32_t>> order;
    for (const auto& [source, time] : last) order.emplace_back(time, source);
    std::sort(order.begin(), order.end());

    std::vector<RatingPoint> out;
    for (const auto& [time, source] : order) {
        // Every game played up to the end of this source: earlier sources, and this one.
        const auto list = rate(db, anchor, 0, [&](const DbGame& g) {
            if (!filter.accepts(db, g)) return false;
            if (g.source == source) return true;
            const auto it  = last.find(g.source);
            const auto end = it != last.end() ? it->second : g.played_at;
            return end < time || (end == time && g.source < source);
        });
        for (const auto& e : list.entries) {
            if (e.name == engine) out.push_back({time, db.sources[source].name, e.rating, e.games});
        }
    }
    return out;
}

std::string ratingPgn(const GameDb& db, const GameFilter& filter) {
    std::string out;
    for (const auto& g : db.games) {
        if (!filter.accepts(db, g)) continue;
        std::string date = "????.??.??";
        if (g.played_at > 0) {
            const std::time_t t = static_cast<std::time_t>(g.played_at / 1000);
            std::tm tm{};
#ifdef _WIN32
            gmtime_s(&tm, &t);
#else
            gmtime_r(&t, &tm);
#endif
            date = fmt::format("{:04}.{:02}.{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        }
        const char* result = g.white_score == 1 ? "1-0" : g.white_score == 0 ? "0-1" : "1/2-1/2";
        out += fmt::format(
            "[Event \"{}\"]\n[Date \"{}\"]\n[White \"{}\"]\n[Black \"{}\"]\n[Result \"{}\"]\n"
            "[TimeControl \"{}\"]\n\n{}\n\n",
            pgnTagValue(db.sources[g.source].name), date, pgnTagValue(db.engines[g.white]),
            pgnTagValue(db.engines[g.black]), result, pgnTagValue(db.time_controls[g.time_control]), result);
    }
    return out;
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
