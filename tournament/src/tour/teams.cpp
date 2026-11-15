#include "teams.hpp"

#include <cmath>

#include <fmt/include/fmt/format.h>
#include <matchmaking/elo/elo_pentanomial.hpp>
#include <matchmaking/elo/elo_wdl.hpp>

namespace fcg::tour {

namespace {

constexpr const char* LETTERS[] = {"A", "B"};

template <typename Elo>
void fillElo(const Elo& elo, TeamTable& t) {
    // An even score of draws only, or a whole one, has no interval.
    if (!std::isfinite(elo.diff()) || !std::isfinite(elo.error()) || elo.error() <= 0) return;
    t.elo   = elo.diff() + 0.0;  // + 0.0: an even score is +0, not -0
    t.error = elo.error();
    // los() is only exposed formatted, as "97.21 %".
    try {
        const double los = std::stod(elo.los()) / 100.0;
        if (std::isfinite(los)) t.los = los;
    } catch (const std::exception&) {
    }
}

// Team `team`'s Elo over the other one, as the copies write it.
std::string teamElo(const TeamTable& t, int team) {
    return t.elo ? fmt::format("{:+.1f}", (team == 0 ? *t.elo : -*t.elo) + 0.0) : "-";
}

std::string place(int team, std::size_t k) { return fmt::format("{}{}", LETTERS[team], k + 1); }

const Standing* standingOf(const Table& table, int id) {
    for (const auto& r : table.rows) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

}  // namespace

PairRecord TeamTable::pair(int a, int b) const {
    const auto it = pairs.find({a, b});
    return it == pairs.end() ? PairRecord{} : it->second;
}

int TeamTable::teamOf(int id) const {
    for (int team = 0; team < 2; ++team) {
        for (const auto& m : teams[static_cast<std::size_t>(team)].members) {
            if (m.id == id) return team;
        }
    }
    return -1;
}

TeamTable computeTeams(const TournamentSpec& spec, const std::vector<GameResult>& results) {
    TeamTable t;
    std::map<int, int> team_of;
    for (int team = 0; team < 2; ++team) {
        auto& s = t.teams[static_cast<std::size_t>(team)];
        s.name  = spec.teamName(team);
        for (const auto& p : spec.participants) {
            if (p.team != team) continue;
            s.members.push_back({p.id, p.engine.name, p.active});
            team_of[p.id] = team;
        }
    }
    const auto teamOf = [&](int id) {
        const auto it = team_of.find(id);
        return it == team_of.end() ? -1 : it->second;
    };

    // The games that count of each encounter, for the pairs of games.
    std::map<uint32_t, std::vector<const GameResult*>> encounters;
    auto& a_team = t.teams[0];
    auto& b_team = t.teams[1];
    for (const auto& r : results) {
        if (r.voided) continue;
        const int tw = teamOf(r.white), tb = teamOf(r.black);
        if (tw < 0 || tb < 0 || tw == tb) continue;
        const int a     = tw == 0 ? r.white : r.black;
        const int b     = tw == 0 ? r.black : r.white;
        const double sa = r.scoreOf(a);
        auto& ab        = t.pairs[{a, b}];
        auto& ba        = t.pairs[{b, a}];

        a_team.games++, b_team.games++;
        a_team.points += sa;
        b_team.points += 1.0 - sa;
        (r.white == a ? ab : ba).white++;
        if (sa == 1.0) {
            a_team.wins++, b_team.losses++, ab.wins++, ba.losses++, t.stats.wins++;
        } else if (sa == 0.0) {
            a_team.losses++, b_team.wins++, ab.losses++, ba.wins++, t.stats.losses++;
        } else {
            a_team.draws++, b_team.draws++, ab.draws++, ba.draws++, t.stats.draws++;
        }
        encounters[r.encounter].push_back(&r);
    }

    for (const auto& [index, games] : encounters) {
        const auto& first = *games.front();
        const int a       = teamOf(first.white) == 0 ? first.white : first.black;
        const int b       = a == first.white ? first.black : first.white;
        if (games.size() != 2) {
            t.pairs[{a, b}].unpaired++;
            t.pairs[{b, a}].unpaired++;
            continue;
        }
        const auto e = encounterStats(*games[0], *games[1], a);
        t.stats.penta_WW += e.penta_WW;
        t.stats.penta_WD += e.penta_WD;
        t.stats.penta_WL += e.penta_WL;
        t.stats.penta_DD += e.penta_DD;
        t.stats.penta_LD += e.penta_LD;
        t.stats.penta_LL += e.penta_LL;
    }

    if (t.stats.totalPairs() > 0) {
        fillElo(fastchess::elo::EloPentanomial(t.stats), t);
    } else if (t.stats.sum() > 0) {
        fillElo(fastchess::elo::EloWDL(t.stats), t);
    }
    return t;
}

std::string teamScoreLine(const TeamTable& t) {
    const auto& a   = t.teams[0];
    const auto& b   = t.teams[1];
    std::string out = fmt::format("{} {} - {} {}", a.name, pointsText(a.points), pointsText(b.points), b.name);
    if (a.games) out += fmt::format(" · {:.1f} %", 100.0 * a.score());
    if (t.elo) out += fmt::format(" · Elo {:+.1f} ± {:.1f}", *t.elo, *t.error);
    return out;
}

std::string teamText(const TeamTable& t, const Table& engines, const std::string& title, const std::string& footer) {
    std::size_t width = 6;
    for (const auto& team : t.teams) {
        width = std::max(width, team.name.size());
        for (const auto& m : team.members) width = std::max(width, m.name.size());
    }

    std::string out = title.empty() ? "" : title + "\n\n";
    out += fmt::format("{:<{}}  {:>6}  {:>5}  {:>6}  {:>7}  {:>5}  {:>14}\n", "Team", width, "Points", "Games", "%",
                       "Elo", "+/-", "+ = -");
    for (int i = 0; i < 2; ++i) {
        const auto& s = t.teams[static_cast<std::size_t>(i)];
        out +=
            fmt::format("{:<{}}  {:>6}  {:>5}  {:>5.1f}%  {:>7}  {:>5}  {:>14}\n", s.name, width, pointsText(s.points),
                        s.games, 100.0 * s.score(), teamElo(t, i), t.error ? fmt::format("{:.1f}", *t.error) : "-",
                        fmt::format("+{} ={} -{}", s.wins, s.draws, s.losses));
    }
    if (t.los) out += fmt::format("LOS of {}: {:.1f} %\n", t.teams[0].name, 100.0 * *t.los);

    // Each team's engines, with their points against each engine of the other team, numbered by board.
    for (int i = 0; i < 2; ++i) {
        const auto& mine   = t.teams[static_cast<std::size_t>(i)];
        const auto& theirs = t.teams[static_cast<std::size_t>(1 - i)];
        out += fmt::format("\n{:>3}  {:<{}}  {:>6}  {:>5}  {:>6}  {:>7}  {:>5}  {:>14}  {:>3}", "#", mine.name, width,
                           "Points", "Games", "%", "Elo", "+/-", "+ = -", "TF");
        for (std::size_t j = 0; j < theirs.members.size(); ++j) out += fmt::format("  {:>4}", place(1 - i, j));
        out += "\n";
        for (std::size_t k = 0; k < mine.members.size(); ++k) {
            const auto& m     = mine.members[k];
            const auto* found = standingOf(engines, m.id);
            const Standing s  = found ? *found : Standing{};
            out +=
                fmt::format("{:>3}  {:<{}}  {:>6}  {:>5}  {:>5.1f}%  {:>+7}  {:>5.0f}  {:>14}  {:>3}", place(i, k),
                            m.name, width, pointsText(s.points), s.games, 100.0 * s.score(), std::lround(s.rating.elo),
                            s.rating.error, fmt::format("+{} ={} -{}", s.wins, s.draws, s.losses), s.timeouts);
            for (const auto& o : theirs.members) {
                const auto p = t.pair(m.id, o.id);
                out += fmt::format("  {:>4}", p.games() ? pointsText(p.points()) : ".");
            }
            out += "\n";
        }
    }
    if (!footer.empty()) out += "\n" + footer + "\n";
    return out;
}

std::string teamHtml(const TeamTable& t, const Table& engines, const std::string& title, const std::string& footer) {
    std::string out = htmlPageStart(title);
    out +=
        R"(<div class="card"><table><thead><tr><th class="l">Team</th><th>Points</th><th>Games</th><th>%</th><th>Elo</th><th>&plusmn;</th><th>+</th><th>=</th><th>&minus;</th></tr></thead><tbody>)"
        "\n";
    for (int i = 0; i < 2; ++i) {
        const auto& s = t.teams[static_cast<std::size_t>(i)];
        out += fmt::format(
            "<tr><td class=\"l\">{}</td><td>{}</td><td>{}</td><td>{:.1f}</td><td>{}</td><td>{}</td><td>{}</td>"
            "<td>{}</td><td>{}</td></tr>\n",
            escapeHtml(s.name), pointsText(s.points), s.games, 100.0 * s.score(), teamElo(t, i),
            t.error ? fmt::format("{:.1f}", *t.error) : "", s.wins, s.draws, s.losses);
    }
    out += "</tbody></table></div>\n";
    if (t.los) {
        out += fmt::format("<p class=\"foot\">LOS of {}: {:.1f} %</p>\n", escapeHtml(t.teams[0].name), 100.0 * *t.los);
    }

    for (int i = 0; i < 2; ++i) {
        const auto& mine   = t.teams[static_cast<std::size_t>(i)];
        const auto& theirs = t.teams[static_cast<std::size_t>(1 - i)];
        out += fmt::format(
            R"(<div class="card" style="margin-top:16px"><table><thead><tr><th>#</th><th class="l">{}</th><th>Points</th><th>Games</th><th>%</th><th>Elo</th><th>&plusmn;</th><th>+</th><th>=</th><th>&minus;</th><th title="Losses on time">TF</th>)",
            escapeHtml(mine.name));
        for (std::size_t j = 0; j < theirs.members.size(); ++j) {
            out += fmt::format("<th title=\"{}\">{}</th>", escapeHtml(theirs.members[j].name), place(1 - i, j));
        }
        out += "</tr></thead><tbody>\n";
        for (std::size_t k = 0; k < mine.members.size(); ++k) {
            const auto& m     = mine.members[k];
            const auto* found = standingOf(engines, m.id);
            const Standing s  = found ? *found : Standing{};
            out += fmt::format(
                "<tr><td>{}</td><td class=\"l\">{}</td><td>{}</td><td>{}</td><td>{:.1f}</td><td>{:+}</td>"
                "<td>{:.0f}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td>",
                place(i, k), escapeHtml(m.name), pointsText(s.points), s.games, 100.0 * s.score(),
                std::lround(s.rating.elo), s.rating.error, s.wins, s.draws, s.losses, s.timeouts);
            for (const auto& o : theirs.members) {
                const auto p = t.pair(m.id, o.id);
                out += fmt::format("<td title=\"+{} ={} -{}\">{}</td>", p.wins, p.draws, p.losses,
                                   p.games() ? pointsText(p.points()) : "");
            }
            out += "</tr>\n";
        }
        out += "</tbody></table></div>\n";
    }
    if (!footer.empty()) out += "<p class=\"foot\">" + escapeHtml(footer) + "</p>\n";
    out += "</body></html>\n";
    return out;
}

}  // namespace fcg::tour
