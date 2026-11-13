#include "ratinglist.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <string_view>

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
    // Games and the points of the lower id, by pair of engines that met: a database of thousands of engines has
    // far fewer pairs than n².
    std::map<std::pair<uint32_t, uint32_t>, std::pair<double, double>> met;
    std::vector<RatingEntry> entries(n);
    std::vector<std::set<uint32_t>> sources(n);
    std::set<uint32_t> counted;
    std::set<std::string> tcs;

    for (const auto& g : db.games) {
        tcs.insert(db.time_controls[g.time_control]);
        if (!accept(g)) continue;
        const auto i = g.white, j = g.black;
        if (i != j) {
            auto& pair = met[{std::min(i, j), std::max(i, j)}];
            pair.first += 1;
            pair.second += i < j ? g.white_score : 1 - g.white_score;
        }
        entries[i].games++, entries[j].games++;
        entries[i].points += g.white_score;
        entries[j].points += 1 - g.white_score;
        if (g.white_score > 0.75f) {
            entries[i].wins++, entries[j].losses++;
        } else if (g.white_score < 0.25f) {
            entries[i].losses++, entries[j].wins++;
        } else {
            entries[i].draws++, entries[j].draws++;
        }
        sources[i].insert(g.source), sources[j].insert(g.source);
        counted.insert(g.source);
        list.games++;
        (g.white_score > 0.75f ? list.white_wins : g.white_score < 0.25f ? list.black_wins : list.draws)++;
    }

    // Engines without a game are not rated at all.
    std::vector<uint32_t> present;
    std::vector<std::size_t> position(n);
    std::optional<std::pair<int, double>> fixed;
    for (uint32_t i = 0; i < n; ++i) {
        if (entries[i].games == 0) continue;
        position[i] = present.size();
        if (anchor && db.engines[i] == anchor->first)
            fixed = std::make_pair(static_cast<int>(present.size()), anchor->second);
        present.push_back(i);
    }
    std::vector<PairGames> pairs;
    pairs.reserve(met.size());
    for (const auto& [ids, record] : met) {
        pairs.push_back({position[ids.first], position[ids.second], record.first, record.second});
    }
    const auto solution = ratingSolution(present.size(), pairs, fixed);
    const auto& ratings = solution.ratings;

    // The opponents' average, each opponent weighed by the games against it (games against itself aside).
    std::vector<double> opponents(present.size(), 0.0), met_games(present.size(), 0.0);
    for (const auto& p : pairs) {
        opponents[p.a] += p.games * ratings[p.b].elo, met_games[p.a] += p.games;
        opponents[p.b] += p.games * ratings[p.a].elo, met_games[p.b] += p.games;
    }

    std::vector<std::size_t> listed;  // positions in `present`, best first
    for (std::size_t a = 0; a < present.size(); ++a) {
        auto& e       = entries[present[a]];
        e.name        = db.engines[present[a]];
        e.rating      = ratings[a];
        e.tournaments = static_cast<int>(sources[present[a]].size());
        e.opponents   = met_games[a] > 0 ? opponents[a] / met_games[a] : 0.0;
        if (e.games >= min_games) listed.push_back(a);
    }
    std::stable_sort(listed.begin(), listed.end(),
                     [&](std::size_t a, std::size_t b) { return ratings[a].elo > ratings[b].elo; });
    for (std::size_t k = 0; k < listed.size(); ++k) {
        auto e = entries[present[listed[k]]];
        if (k + 1 < listed.size()) {
            const auto a = listed[k], b = listed[k + 1];
            e.gap = ratings[a].elo - ratings[b].elo;
            e.los = likelihoodOfSuperiority(*e.gap, solution.differenceError(a, b));
        }
        list.entries.push_back(std::move(e));
    }
    list.exact_los = solution.exact;
    list.anchored  = fixed.has_value();

    list.tournaments = static_cast<int>(counted.size());
    for (const auto source : counted) {
        const auto& s = db.sources[source];
        (s.external ? list.pgn_files : list.tournament_names).push_back(s.name);
    }
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
    std::string out = fmt::format(
        "{:>4}  {:<{}}  {:>6}  {:>5}  {:>6}  {:>6}  {:>6}  {:>6}  {:>6}  {:>6}  {:>6}  {:>6}  {:>6}  {:>11}\n", "#",
        "Engine", width, "Elo", "+/-", "Games", "Score", "+", "=", "-", "Draws", "Opp.", "LOS", "Gap", "Tournaments");
    for (std::size_t i = 0; i < list.entries.size(); ++i) {
        const auto& e = list.entries[i];
        out += fmt::format(
            "{:>4}  {:<{}}  {:>6}  {:>5.0f}  {:>6}  {:>5.1f}%  {:>6}  {:>6}  {:>6}  {:>5.1f}%  {:>6}  {:>6}  {:>6}  "
            "{:>11}\n",
            i + 1, e.name, width, std::lround(e.rating.elo), e.rating.error, e.games, 100.0 * e.score(), e.wins,
            e.draws, e.losses, 100.0 * e.drawRate(), std::lround(e.opponents),
            e.los ? fmt::format("{:.1f}%", 100.0 * *e.los) : "", e.gap ? fmt::format("{:.0f}", *e.gap) : "",
            e.tournaments);
    }
    out += fmt::format("\n{} games, {} tournaments\n", list.games, list.tournaments);
    return out;
}

namespace {

// The columns of the copies after the rank and the name, as text: Elo, error, games, score, wins, draws, losses, draw
// rate, opponents, LOS and gap (empty for the last), tournaments.
std::vector<std::string> cells(const RatingEntry& e) {
    return {std::to_string(std::lround(e.rating.elo)),
            fmt::format("{:.0f}", e.rating.error),
            std::to_string(e.games),
            fmt::format("{:.1f}%", 100.0 * e.score()),
            std::to_string(e.wins),
            std::to_string(e.draws),
            std::to_string(e.losses),
            fmt::format("{:.1f}%", 100.0 * e.drawRate()),
            std::to_string(std::lround(e.opponents)),
            e.los ? fmt::format("{:.1f}%", 100.0 * *e.los) : "",
            e.gap ? fmt::format("{:.0f}", *e.gap) : "",
            std::to_string(e.tournaments)};
}

const char* const HEADS[] = {"Elo", "+/-",   "Games", "Score", "+",   "=",
                             "-",   "Draws", "Opp.",  "LOS",   "Gap", "Tournaments"};

std::string joined(const std::vector<std::string>& items, const std::string& separator) {
    std::string out;
    for (const auto& item : items) out += (out.empty() ? "" : separator) + item;
    return out;
}

}  // namespace

std::string ratingListCsv(const RatingList& list) {
    std::string out = "rank,engine,elo,error,games,score,wins,draws,losses,draw_rate,opponents,los,gap,tournaments\n";
    for (std::size_t i = 0; i < list.entries.size(); ++i) {
        const auto& e = list.entries[i];
        auto name     = e.name;
        if (name.find_first_of(",\"\n") != std::string::npos) {
            std::string quoted = "\"";
            for (const char c : name) quoted += c == '"' ? std::string("\"\"") : std::string(1, c);
            name = quoted + "\"";
        }
        out += fmt::format("{},{},{:.1f},{:.1f},{},{:.4f},{},{},{},{:.4f},{:.1f},{},{},{}\n", i + 1, name, e.rating.elo,
                           e.rating.error, e.games, e.score(), e.wins, e.draws, e.losses, e.drawRate(), e.opponents,
                           e.los ? fmt::format("{:.4f}", *e.los) : "", e.gap ? fmt::format("{:.1f}", *e.gap) : "",
                           e.tournaments);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> ratingListHeader(const RatingList& list, const RatingListInfo& info) {
    std::vector<std::pair<std::string, std::string>> out;
    const auto named = [](std::size_t n, const char* one, const char* many, const std::vector<std::string>& names) {
        return fmt::format("{} {}: {}", n, n == 1 ? one : many, joined(names, ", "));
    };
    std::vector<std::string> sources;
    if (!list.tournament_names.empty()) {
        sources.push_back(named(list.tournament_names.size(), "tournament", "tournaments", list.tournament_names));
    }
    if (!list.pgn_files.empty())
        sources.push_back(named(list.pgn_files.size(), "PGN file", "PGN files", list.pgn_files));
    out.emplace_back("Sources", sources.empty() ? "none" : joined(sources, "; "));
    out.emplace_back("Games",
                     fmt::format("{}: White wins {}, Black wins {}, draws {}; White scores {:.1f}%", list.games,
                                 list.white_wins, list.black_wins, list.draws, 100.0 * list.whiteScore()));
    out.emplace_back("Anchor", info.anchor.empty() ? "none, the ratings average 0"
                                                   : fmt::format("{} at {:.0f}", info.anchor, info.anchor_elo));
    out.emplace_back(
        "Filters",
        fmt::format("{}, {}, {}, imported games {}",
                    info.time_control.empty() ? "every time control" : "time control " + info.time_control,
                    info.group.empty() ? "every engine" : "group " + info.group,
                    info.period.empty() ? "every game" : info.period, info.external ? "counted" : "left out"));
    out.emplace_back("Minimum games", std::to_string(info.min_games));
    std::string made = info.date;
    if (!info.version.empty()) made += (made.empty() ? "" : " ") + std::string("by fastchess-gui ") + info.version;
    if (!made.empty()) out.emplace_back("Made", made);
    return out;
}

std::string ratingListHeaderText(const RatingList& list, const RatingListInfo& info) {
    std::string out;
    for (const auto& [label, value] : ratingListHeader(list, info)) out += label + ": " + value + "\n";
    return out;
}

std::string ratingListHtml(const RatingList& list, const RatingListInfo& info, const std::string& title) {
    std::string out = htmlPageStart(title) + "<dl class=\"head\">";
    for (const auto& [label, value] : ratingListHeader(list, info)) {
        out += fmt::format("<dt>{}</dt><dd>{}</dd>", escapeHtml(label), escapeHtml(value));
    }
    out += "</dl>\n<div class=\"card\"><table><thead><tr><th>#</th><th class=\"l\">Engine</th>";
    static const char* const TIPS[] = {"",
                                       "Half-width of the 95 % interval",
                                       "",
                                       "Points per game",
                                       "Wins",
                                       "Draws",
                                       "Losses",
                                       "Share of drawn games",
                                       "Average rating of the opponents, game by game",
                                       "Likelihood of superiority over the next engine",
                                       "Elo above the next engine",
                                       "Tournaments and PGN files"};
    for (std::size_t c = 0; c < std::size(HEADS); ++c) {
        const std::string head = HEADS[c] == std::string("+/-") ? "&plusmn;"
                                 : HEADS[c] == std::string("-") ? "&minus;"
                                                                : HEADS[c];
        out += *TIPS[c] ? fmt::format("<th title=\"{}\">{}</th>", TIPS[c], head) : fmt::format("<th>{}</th>", head);
    }
    out += "</tr></thead><tbody>\n";
    for (std::size_t i = 0; i < list.entries.size(); ++i) {
        const auto& e = list.entries[i];
        out += fmt::format("<tr><td>{}</td><td class=\"l\">{}</td>", i + 1, escapeHtml(e.name));
        for (const auto& cell : cells(e)) out += "<td>" + cell + "</td>";
        out += "</tr>\n";
    }
    out += "</tbody></table></div>\n</body></html>\n";
    return out;
}

std::string escapeMarkdown(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '\n' || c == '\r') {
            out += ' ';
            continue;
        }
        if (std::string_view("\\|<>*_`[]").find(c) != std::string_view::npos) out += '\\';
        out += c;
    }
    return out;
}

std::string ratingListMarkdown(const RatingList& list, const RatingListInfo& info, const std::string& title) {
    std::string out = "## " + escapeMarkdown(title) + "\n\n";
    for (const auto& [label, value] : ratingListHeader(list, info)) {
        out += "- " + escapeMarkdown(label) + ": " + escapeMarkdown(value) + "\n";
    }
    out += "\n| # | Engine |";
    for (const auto* head : HEADS) out += fmt::format(" {} |", escapeMarkdown(head));
    out += "\n|--:|:--|";
    for (std::size_t c = 0; c < std::size(HEADS); ++c) out += "--:|";
    out += "\n";
    for (std::size_t i = 0; i < list.entries.size(); ++i) {
        const auto& e = list.entries[i];
        out += fmt::format("| {} | {} |", i + 1, escapeMarkdown(e.name));
        for (const auto& cell : cells(e)) out += " " + cell + " |";
        out += "\n";
    }
    return out;
}

}  // namespace fcg::tour
