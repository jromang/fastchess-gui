#include "standings.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/include/fmt/core.h>

namespace fcg::tour {

namespace {

constexpr double K           = 0.0057564627324851142;  // ln(10) / 400
constexpr double PRIOR_SIGMA = 1000.0;
constexpr double Z95         = 1.959963984540054;

using Matrix = std::vector<std::vector<double>>;

// Solves a x = b by Gaussian elimination with partial pivoting; a is n x n and invertible.
std::vector<double> solve(Matrix a, std::vector<double> b) {
    const auto n = b.size();
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t r = col + 1; r < n; ++r) {
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) pivot = r;
        }
        std::swap(a[col], a[pivot]);
        std::swap(b[col], b[pivot]);

        for (std::size_t r = col + 1; r < n; ++r) {
            const double f = a[r][col] / a[col][col];
            if (f == 0.0) continue;
            for (std::size_t c = col; c < n; ++c) a[r][c] -= f * a[col][c];
            b[r] -= f * b[col];
        }
    }
    std::vector<double> x(n);
    for (std::size_t i = n; i-- > 0;) {
        double s = b[i];
        for (std::size_t c = i + 1; c < n; ++c) s -= a[i][c] * x[c];
        x[i] = s / a[i][i];
    }
    return x;
}

Matrix inverse(const Matrix& a) {
    const auto n = a.size();
    Matrix inv(n, std::vector<double>(n));
    for (std::size_t c = 0; c < n; ++c) {
        std::vector<double> e(n, 0.0);
        e[c]           = 1.0;
        const auto col = solve(a, e);
        for (std::size_t r = 0; r < n; ++r) inv[r][c] = col[r];
    }
    return inv;
}

std::string half(double points) {
    const auto whole = static_cast<long>(std::floor(points + 1e-9));
    const bool h     = points - whole > 0.25;
    if (whole == 0 && h) return "½";
    return fmt::format("{}{}", whole, h ? "½" : "");
}

std::string escapeHtml(const std::string& s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out += c;
        }
    }
    return out;
}

}  // namespace

std::vector<Rating> maximumLikelihood(const Matrix& games, const Matrix& points,
                                      std::optional<std::pair<int, double>> anchor) {
    const auto n = games.size();
    std::vector<Rating> out(n);
    if (n == 0) return out;

    std::vector<double> r(n, 0.0);
    Matrix h(n, std::vector<double>(n));

    for (int iter = 0; iter < 200; ++iter) {
        std::vector<double> g(n, 0.0);
        for (auto& row : h) std::fill(row.begin(), row.end(), 0.0);

        for (std::size_t i = 0; i < n; ++i) {
            g[i] -= r[i] / (PRIOR_SIGMA * PRIOR_SIGMA);
            h[i][i] -= 1.0 / (PRIOR_SIGMA * PRIOR_SIGMA);
            for (std::size_t j = 0; j < n; ++j) {
                if (i == j || games[i][j] <= 0) continue;
                const double p = 1.0 / (1.0 + std::pow(10.0, -(r[i] - r[j]) / 400.0));
                g[i] += K * (points[i][j] - games[i][j] * p);
                const double w = K * K * games[i][j] * p * (1.0 - p);
                h[i][i] -= w;
                h[i][j] += w;
            }
        }

        // Newton step: h * delta = -g
        std::vector<double> minus_g(n);
        for (std::size_t i = 0; i < n; ++i) minus_g[i] = -g[i];
        const auto delta = solve(h, minus_g);

        double largest = 0;
        for (std::size_t i = 0; i < n; ++i) {
            r[i] += std::clamp(delta[i], -400.0, 400.0);  // damped far from the optimum
            largest = std::max(largest, std::abs(delta[i]));
        }
        if (largest < 1e-7) break;
    }

    // Covariance from the information matrix (without the prior's own weight on the common level, which the
    // differences below cancel anyway).
    Matrix information(n, std::vector<double>(n));
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) information[i][j] = -h[i][j];
    }
    const auto cov = inverse(information);

    if (anchor && anchor->first >= 0 && static_cast<std::size_t>(anchor->first) < n) {
        const auto a = static_cast<std::size_t>(anchor->first);
        for (std::size_t i = 0; i < n; ++i) {
            out[i].elo       = anchor->second + r[i] - r[a];
            const double var = cov[i][i] + cov[a][a] - 2 * cov[i][a];
            out[i].error     = i == a ? 0.0 : Z95 * std::sqrt(std::max(0.0, var));
        }
        return out;
    }

    double mean = 0;
    for (const double v : r) mean += v;
    mean /= static_cast<double>(n);

    // Variance of r_i - mean, i.e. of the i-th coordinate of (I - J/n) r.
    double total = 0;
    std::vector<double> row_sum(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            row_sum[i] += cov[i][j];
            total += cov[i][j];
        }
    }
    const double nn = static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double var = cov[i][i] - 2.0 * row_sum[i] / nn + total / (nn * nn);
        out[i].elo       = r[i] - mean;
        out[i].error     = Z95 * std::sqrt(std::max(0.0, var));
    }
    return out;
}

PairRecord Table::pair(int a, int b) const {
    const auto it = pairs.find({a, b});
    return it == pairs.end() ? PairRecord{} : it->second;
}

Table computeTable(const TournamentSpec& spec, const std::vector<GameResult>& results, std::optional<Anchor> anchor) {
    Table table;
    const auto& people = spec.participants;
    const auto n       = people.size();

    std::map<int, std::size_t> index;
    for (std::size_t i = 0; i < n; ++i) {
        index[people[i].id] = i;
        Standing s;
        s.id   = people[i].id;
        s.name = people[i].engine.name;
        table.rows.push_back(s);
    }

    Matrix games(n, std::vector<double>(n, 0.0));
    Matrix points(n, std::vector<double>(n, 0.0));

    for (const auto& r : results) {
        const auto w = index.find(r.white);
        const auto b = index.find(r.black);
        if (w == index.end() || b == index.end()) continue;

        const double sw = r.scoreOf(r.white);
        auto& white     = table.rows[w->second];
        auto& black     = table.rows[b->second];
        auto& wb        = table.pairs[{r.white, r.black}];
        auto& bw        = table.pairs[{r.black, r.white}];

        white.games++;
        black.games++;
        white.points += sw;
        black.points += 1.0 - sw;
        if (sw == 1.0) {
            white.wins++, black.losses++, wb.wins++, bw.losses++;
        } else if (sw == 0.0) {
            white.losses++, black.wins++, wb.losses++, bw.wins++;
        } else {
            white.draws++, black.draws++, wb.draws++, bw.draws++;
        }

        games[w->second][b->second] += 1;
        games[b->second][w->second] += 1;
        points[w->second][b->second] += sw;
        points[b->second][w->second] += 1.0 - sw;

        if (sw != 0.5) {
            auto& loser = sw == 1.0 ? black : white;
            switch (r.termination) {
                case fastchess::MatchTermination::TIMEOUT:
                    loser.timeouts++;
                    break;
                case fastchess::MatchTermination::DISCONNECT:
                case fastchess::MatchTermination::STALL:
                    loser.crashes++;
                    break;
                case fastchess::MatchTermination::ILLEGAL_MOVE:
                    loser.illegal++;
                    break;
                default:
                    break;
            }
        }
    }

    // Sonneborn-Berger.
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (i != j) table.rows[i].sb += points[i][j] * table.rows[j].points;
        }
    }

    std::optional<std::pair<int, double>> fixed;
    if (anchor) {
        if (const auto it = index.find(anchor->id); it != index.end()) {
            fixed = std::make_pair(static_cast<int>(it->second), anchor->elo);
        }
    }
    const auto ratings = maximumLikelihood(games, points, fixed);
    for (std::size_t i = 0; i < n; ++i) {
        table.rows[i].rating = ratings[i];
        table.rows[i].rated  = table.rows[i].games > 0;
    }

    std::stable_sort(table.rows.begin(), table.rows.end(), [&](const Standing& a, const Standing& b) {
        if (a.points != b.points) return a.points > b.points;
        if (a.sb != b.sb) return a.sb > b.sb;
        const double ab = table.pair(a.id, b.id).points(), ba = table.pair(b.id, a.id).points();
        if (ab != ba) return ab > ba;
        return a.wins > b.wins;
    });
    return table;
}

std::string tableText(const Table& table, const std::string& title) {
    std::size_t width = 6;
    for (const auto& r : table.rows) width = std::max(width, r.name.size());

    std::string out = title + "\n\n";
    out += fmt::format("{:>3}  {:<{}}  {:>6}  {:>5}  {:>6}  {:>7}  {:>5}  {:>14}", "#", "Engine", width, "Points",
                       "Games", "%", "Elo", "+/-", "+ = -");
    for (std::size_t c = 0; c < table.rows.size(); ++c) out += fmt::format("  {:>4}", c + 1);
    out += "\n";

    for (std::size_t i = 0; i < table.rows.size(); ++i) {
        const auto& r = table.rows[i];
        out += fmt::format("{:>3}  {:<{}}  {:>6}  {:>5}  {:>5.1f}%  {:>+7.0f}  {:>5.0f}  {:>14}", i + 1, r.name, width,
                           half(r.points), r.games, 100.0 * r.score(), r.rating.elo, r.rating.error,
                           fmt::format("+{} ={} -{}", r.wins, r.draws, r.losses));
        for (const auto& c : table.rows) {
            if (c.id == r.id) {
                out += "     -";
                continue;
            }
            const auto p = table.pair(r.id, c.id);
            out += fmt::format("  {:>4}", p.games() ? half(p.points()) : ".");
        }
        out += "\n";
    }
    return out;
}

std::string tableHtml(const Table& table, const std::string& title) {
    std::string out = R"(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>)" + escapeHtml(title) +
                      R"(</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--text:#1b1e24;--muted:#626976;--line:#e0e3e8;--win:#1d7f4e;--loss:#c03b33}
@media (prefers-color-scheme:dark){:root{--bg:#121418;--card:#1a1d23;--text:#e8eaee;--muted:#a3aab6;--line:#262a32;--win:#5fd39a;--loss:#ff8f87}}
body{margin:0;padding:24px;background:var(--bg);color:var(--text);font:14px/1.45 system-ui,sans-serif}
h1{font-size:20px;margin:0 0 16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:8px;overflow-x:auto}
table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}
th,td{padding:6px 10px;border-bottom:1px solid var(--line);text-align:right;white-space:nowrap}
th{color:var(--muted);font-weight:500}td.l,th.l{text-align:left}
td.x{color:var(--muted)}
</style></head><body>
<h1>)" + escapeHtml(title) +
                      R"(</h1>
<div class="card"><table><thead><tr><th>#</th><th class="l">Engine</th><th>Points</th><th>Games</th><th>%</th><th>Elo</th><th>&plusmn;</th><th>+</th><th>=</th><th>&minus;</th><th>SB</th>)";
    for (std::size_t c = 0; c < table.rows.size(); ++c) out += fmt::format("<th>{}</th>", c + 1);
    out += "</tr></thead><tbody>\n";

    for (std::size_t i = 0; i < table.rows.size(); ++i) {
        const auto& r = table.rows[i];
        out += fmt::format(
            "<tr><td>{}</td><td class=\"l\">{}</td><td>{}</td><td>{}</td><td>{:.1f}</td><td>{:+.0f}</td><td>{:.0f}</td>"
            "<td>{}</td><td>{}</td><td>{}</td><td>{:.2f}</td>",
            i + 1, escapeHtml(r.name), half(r.points), r.games, 100.0 * r.score(), r.rating.elo, r.rating.error, r.wins,
            r.draws, r.losses, r.sb);
        for (const auto& c : table.rows) {
            if (c.id == r.id) {
                out += "<td class=\"x\">&ndash;</td>";
                continue;
            }
            const auto p = table.pair(r.id, c.id);
            out += fmt::format("<td title=\"+{} ={} -{}\">{}</td>", p.wins, p.draws, p.losses,
                               p.games() ? half(p.points()) : "");
        }
        out += "</tr>\n";
    }
    out += "</tbody></table></div></body></html>\n";
    return out;
}

}  // namespace fcg::tour
