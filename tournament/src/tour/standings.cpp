#include "standings.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <tuple>

#include <fmt/include/fmt/format.h>

#include "ratinglist.hpp"

namespace fcg::tour {

namespace {

constexpr double K           = 0.0057564627324851142;  // ln(10) / 400
constexpr double PRIOR_SIGMA = 1000.0;
constexpr double Z95         = 1.959963984540054;

using Matrix = std::vector<std::vector<double>>;

// Up to this many players the covariance of the ratings is the exact inverse of the information matrix, O(n³);
// beyond, an approximation from each rating's own games and its opponents', within a few percent of it on a rating
// list of well connected engines.
constexpr std::size_t EXACT_ERRORS = 1000;

// The information matrix of the ratings (the negated Hessian of the log-likelihood with the prior), sparse: the
// prior and the weights of each player's games on the diagonal, minus the weight of each pair off it.
struct Information {
    std::vector<double> diagonal;
    std::vector<double> weight;  // by pair, as in `pairs`

    // y = I x
    void times(const std::vector<PairGames>& pairs, const std::vector<double>& x, std::vector<double>& y) const {
        for (std::size_t i = 0; i < x.size(); ++i) y[i] = diagonal[i] * x[i];
        for (std::size_t e = 0; e < pairs.size(); ++e) {
            const auto a = pairs[e].a, b = pairs[e].b;
            y[a] -= weight[e] * x[b];
            y[b] -= weight[e] * x[a];
        }
    }
};

// Solves I x = b by conjugate gradients preconditioned by the diagonal: each step is one pass over the pairs, so
// that thousands of engines take a moment where a dense solve would take minutes.
std::vector<double> solve(const Information& info, const std::vector<PairGames>& pairs, const std::vector<double>& b) {
    const auto n = b.size();
    std::vector<double> x(n, 0.0), r = b, z(n), p(n), q(n);
    double norm_b = 0;
    for (const double v : b) norm_b += v * v;
    if (norm_b == 0) return x;

    double rz = 0;
    for (std::size_t i = 0; i < n; ++i) {
        z[i] = r[i] / info.diagonal[i];
        rz += r[i] * z[i];
    }
    p                = z;
    const auto steps = std::max<std::size_t>(200, 4 * n);
    for (std::size_t step = 0; step < steps; ++step) {
        info.times(pairs, p, q);
        double pq = 0;
        for (std::size_t i = 0; i < n; ++i) pq += p[i] * q[i];
        if (pq <= 0) break;
        const double alpha = rz / pq;
        double norm_r      = 0;
        for (std::size_t i = 0; i < n; ++i) {
            x[i] += alpha * p[i];
            r[i] -= alpha * q[i];
            norm_r += r[i] * r[i];
        }
        if (norm_r <= 1e-24 * norm_b) break;
        double rz_next = 0;
        for (std::size_t i = 0; i < n; ++i) {
            z[i] = r[i] / info.diagonal[i];
            rz_next += r[i] * z[i];
        }
        const double beta = rz_next / rz;
        rz                = rz_next;
        for (std::size_t i = 0; i < n; ++i) p[i] = z[i] + beta * p[i];
    }
    return x;
}

// a = L Lᵀ: a, symmetric positive definite, n x n and row major, becomes L (its lower triangle).
void cholesky(std::vector<double>& a, std::size_t n) {
    for (std::size_t j = 0; j < n; ++j) {
        double* row_j = &a[j * n];
        double d      = row_j[j];
        for (std::size_t k = 0; k < j; ++k) d -= row_j[k] * row_j[k];
        d        = std::sqrt(std::max(d, 1e-300));
        row_j[j] = d;
        for (std::size_t i = j + 1; i < n; ++i) {
            double* row_i = &a[i * n];
            double s      = row_i[j];
            for (std::size_t k = 0; k < j; ++k) s -= row_i[k] * row_j[k];
            row_i[j] = s / d;
        }
    }
}

// x = (L Lᵀ)⁻¹ b
std::vector<double> choleskySolve(const std::vector<double>& l, std::size_t n, std::vector<double> b) {
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < i; ++k) b[i] -= l[i * n + k] * b[k];
        b[i] /= l[i * n + i];
    }
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t k = i + 1; k < n; ++k) b[i] -= l[k * n + i] * b[k];
        b[i] /= l[i * n + i];
    }
    return b;
}

// What the errors need of the covariance of the ratings: its diagonal, its row sums and the anchor's row; and, kept
// when asked for the differences between two ratings, the columns of L⁻¹ (I = L Lᵀ), column i from row i at i n.
struct Covariance {
    std::vector<double> diagonal, row_sum, anchor_row, inverse;
};

Covariance covariance(const Information& info, const std::vector<PairGames>& pairs, std::optional<std::size_t> anchor,
                      bool keep_inverse) {
    const auto n = info.diagonal.size();
    Covariance cov;
    if (n > EXACT_ERRORS) {
        // The first terms of I⁻¹ = D⁻¹ + D⁻¹ W D⁻¹ + D⁻¹ W D⁻¹ W D⁻¹ + …, W the pairs' weights: a rating's own
        // information, and its opponents' uncertainty.
        cov.diagonal.resize(n);
        for (std::size_t i = 0; i < n; ++i) cov.diagonal[i] = 1.0 / info.diagonal[i];
        auto own = cov.diagonal;
        for (std::size_t e = 0; e < pairs.size(); ++e) {
            const auto a = pairs[e].a, b = pairs[e].b;
            const double w2 = info.weight[e] * info.weight[e];
            cov.diagonal[a] += w2 * own[a] * own[a] * own[b];
            cov.diagonal[b] += w2 * own[b] * own[b] * own[a];
        }
        cov.row_sum = cov.diagonal;
        cov.anchor_row.assign(n, 0.0);
        if (anchor) cov.anchor_row[*anchor] = cov.diagonal[*anchor];
        return cov;
    }

    std::vector<double> l(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) l[i * n + i] = info.diagonal[i];
    for (std::size_t e = 0; e < pairs.size(); ++e) {
        l[pairs[e].a * n + pairs[e].b] -= info.weight[e];
        l[pairs[e].b * n + pairs[e].a] -= info.weight[e];
    }
    cholesky(l, n);

    // The diagonal of (L Lᵀ)⁻¹ = L⁻ᵀ L⁻¹ holds the squared norms of the columns of L⁻¹, column i being zero above
    // row i: n³/6 steps instead of a whole inverse.
    cov.diagonal.assign(n, 0.0);
    if (keep_inverse) cov.inverse.assign(n * n, 0.0);
    std::vector<double> column(n);
    for (std::size_t i = 0; i < n; ++i) {
        column[i] = 1.0 / l[i * n + i];
        double sq = column[i] * column[i];
        for (std::size_t r = i + 1; r < n; ++r) {
            double s = 0;
            for (std::size_t k = i; k < r; ++k) s -= l[r * n + k] * column[k];
            column[r] = s / l[r * n + r];
            sq += column[r] * column[r];
        }
        cov.diagonal[i] = sq;
        if (keep_inverse) std::copy(column.begin() + i, column.end(), cov.inverse.begin() + i * n + i);
    }
    cov.row_sum = choleskySolve(l, n, std::vector<double>(n, 1.0));
    if (anchor) {
        std::vector<double> e(n, 0.0);
        e[*anchor]     = 1.0;
        cov.anchor_row = choleskySolve(l, n, e);
    }
    return cov;
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

namespace {

RatingSolution solveRatings(std::size_t n, const std::vector<PairGames>& pairs,
                            std::optional<std::pair<int, double>> anchor, bool differences) {
    RatingSolution solution;
    auto& out = solution.ratings;
    out.resize(n);
    if (n == 0) return solution;

    std::vector<double> r(n, 0.0), g(n);
    Information info;
    info.diagonal.resize(n);
    info.weight.resize(pairs.size());

    for (int iter = 0; iter < 200; ++iter) {
        for (std::size_t i = 0; i < n; ++i) {
            g[i]             = -r[i] / (PRIOR_SIGMA * PRIOR_SIGMA);
            info.diagonal[i] = 1.0 / (PRIOR_SIGMA * PRIOR_SIGMA);
        }
        for (std::size_t e = 0; e < pairs.size(); ++e) {
            const auto& pg = pairs[e];
            const double p = 1.0 / (1.0 + std::pow(10.0, -(r[pg.a] - r[pg.b]) / 400.0));
            const double d = K * (pg.points_a - pg.games * p);
            g[pg.a] += d;
            g[pg.b] -= d;
            const double w = K * K * pg.games * p * (1.0 - p);
            info.weight[e] = w;
            info.diagonal[pg.a] += w;
            info.diagonal[pg.b] += w;
        }

        // Newton step: I delta = g
        const auto delta = solve(info, pairs, g);

        double largest = 0;
        for (std::size_t i = 0; i < n; ++i) {
            r[i] += std::clamp(delta[i], -400.0, 400.0);  // damped far from the optimum
            largest = std::max(largest, std::abs(delta[i]));
        }
        if (largest < 1e-7) break;
    }

    // Covariance from the information matrix (with the prior's weight on the common level, which the differences
    // below cancel).
    std::optional<std::size_t> fixed;
    if (anchor && anchor->first >= 0 && static_cast<std::size_t>(anchor->first) < n) {
        fixed = static_cast<std::size_t>(anchor->first);
    }
    auto cov = covariance(info, pairs, fixed, differences);

    if (differences) {
        // The variance of r_a - r_b is C_aa + C_bb - 2 C_ab, whatever the common level: exact from the columns of
        // L⁻¹, C_ab being the dot product of columns a and b; else C_ab taken as the first term of the series, the
        // weight of their games over their own information, and 0 for two engines that did not meet.
        solution.exact = cov.inverse.size() == n * n;
        std::map<std::pair<std::size_t, std::size_t>, double> met;
        if (!solution.exact) {
            for (std::size_t e = 0; e < pairs.size(); ++e) {
                const auto a = std::min(pairs[e].a, pairs[e].b), b = std::max(pairs[e].a, pairs[e].b);
                met[{a, b}] += info.weight[e] / (info.diagonal[a] * info.diagonal[b]);
            }
        }
        solution.difference_error = [n, diagonal = cov.diagonal, inverse = std::move(cov.inverse),
                                     met = std::move(met)](std::size_t a, std::size_t b) {
            if (a == b || a >= n || b >= n) return 0.0;
            double c = 0;
            if (!inverse.empty()) {
                const double* ca = &inverse[a * n];
                const double* cb = &inverse[b * n];
                for (std::size_t k = std::max(a, b); k < n; ++k) c += ca[k] * cb[k];
            } else if (const auto it = met.find({std::min(a, b), std::max(a, b)}); it != met.end()) {
                c = it->second;
            }
            return Z95 * std::sqrt(std::max(0.0, diagonal[a] + diagonal[b] - 2 * c));
        };
    }

    if (fixed) {
        const auto a = *fixed;
        for (std::size_t i = 0; i < n; ++i) {
            out[i].elo       = anchor->second + r[i] - r[a];
            const double var = cov.diagonal[i] + cov.diagonal[a] - 2 * cov.anchor_row[i];
            out[i].error     = i == a ? 0.0 : Z95 * std::sqrt(std::max(0.0, var));
        }
        return solution;
    }

    double mean = 0;
    for (const double v : r) mean += v;
    mean /= static_cast<double>(n);

    // Variance of r_i - mean, i.e. of the i-th coordinate of (I - J/n) r.
    double total = 0;
    for (const double v : cov.row_sum) total += v;
    const double nn = static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double var = cov.diagonal[i] - 2.0 * cov.row_sum[i] / nn + total / (nn * nn);
        out[i].elo       = r[i] - mean;
        out[i].error     = Z95 * std::sqrt(std::max(0.0, var));
    }
    return solution;
}

}  // namespace

std::vector<Rating> maximumLikelihood(std::size_t n, const std::vector<PairGames>& pairs,
                                      std::optional<std::pair<int, double>> anchor) {
    return solveRatings(n, pairs, anchor, false).ratings;
}

RatingSolution ratingSolution(std::size_t n, const std::vector<PairGames>& pairs,
                              std::optional<std::pair<int, double>> anchor) {
    return solveRatings(n, pairs, anchor, true);
}

double RatingSolution::differenceError(std::size_t a, std::size_t b) const {
    return difference_error ? difference_error(a, b) : 0.0;
}

double likelihoodOfSuperiority(double difference, double error) {
    // The normal law of the difference, its standard deviation the error over the 97.5 % quantile: the formula of
    // fastchess' LOS of a match (EloWDL::los), there of a score over 50 %, here of a rating difference.
    if (!(error > 0)) return difference > 0 ? 1.0 : difference < 0 ? 0.0 : 0.5;
    return (1 - std::erf(-difference / (error / Z95) / std::sqrt(2.0))) / 2;
}

std::vector<Rating> maximumLikelihood(const Matrix& games, const Matrix& points,
                                      std::optional<std::pair<int, double>> anchor) {
    std::vector<PairGames> pairs;
    for (std::size_t i = 0; i < games.size(); ++i) {
        for (std::size_t j = i + 1; j < games.size(); ++j) {
            if (games[i][j] > 0) pairs.push_back({i, j, games[i][j], points[i][j]});
        }
    }
    return maximumLikelihood(games.size(), pairs, anchor);
}

PairRecord Table::pair(int a, int b) const {
    const auto it = pairs.find({a, b});
    return it == pairs.end() ? PairRecord{} : it->second;
}

Table computeTable(const TournamentSpec& spec, const std::vector<GameResult>& results, std::optional<Anchor> anchor,
                   const std::map<int, int>& byes) {
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

    // The games that count of each encounter, to find the pairs with one game only.
    struct Encounter {
        int a = 0, b = 0, games = 0;
    };
    std::map<uint32_t, Encounter> encounters;

    for (const auto& r : results) {
        if (r.voided) continue;
        const auto w = index.find(r.white);
        const auto b = index.find(r.black);
        if (w == index.end() || b == index.end()) continue;

        const double sw = r.scoreOf(r.white);
        auto& white     = table.rows[w->second];
        auto& black     = table.rows[b->second];
        auto& wb        = table.pairs[{r.white, r.black}];
        auto& bw        = table.pairs[{r.black, r.white}];
        auto& e         = encounters[r.encounter];
        e               = {r.white, r.black, e.games + 1};

        wb.white++;
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

    for (const auto& [number, e] : encounters) {
        if (e.games != 1) continue;
        table.pairs[{e.a, e.b}].unpaired++;
        table.pairs[{e.b, e.a}].unpaired++;
    }

    // A bye is worth a drawn pair of games.
    for (const auto& [id, count] : byes) {
        if (const auto it = index.find(id); it != index.end()) {
            table.rows[it->second].byes = count;
            table.rows[it->second].points += count;
        }
    }

    // Sonneborn-Berger and Buchholz; the pairs that met, once each, for the ratings.
    std::vector<PairGames> met;
    for (const auto& [ids, record] : table.pairs) {
        const auto i = index.at(ids.first), j = index.at(ids.second);
        table.rows[i].sb += record.points() * table.rows[j].points;
        table.rows[i].buchholz += record.games() * table.rows[j].points;
        if (i < j) met.push_back({i, j, static_cast<double>(record.games()), record.points()});
    }

    std::optional<std::pair<int, double>> fixed;
    if (anchor) {
        if (const auto it = index.find(anchor->id); it != index.end()) {
            fixed = std::make_pair(static_cast<int>(it->second), anchor->elo);
        }
    }
    const auto ratings = maximumLikelihood(n, met, fixed);
    for (std::size_t i = 0; i < n; ++i) {
        table.rows[i].rating = ratings[i];
        table.rows[i].rated  = table.rows[i].games > 0;
    }

    // Points and the tiebreaks every row has its own value of, so that the order is a strict weak ordering.
    const bool swiss = spec.format == Format::Swiss;
    const auto key   = [&](const Standing& s) { return std::make_tuple(s.points, swiss ? s.buchholz : 0.0, s.sb); };
    std::stable_sort(table.rows.begin(), table.rows.end(),
                     [&](const Standing& a, const Standing& b) { return key(a) > key(b); });

    // Head to head is not transitive (A beats B beats C beats A): it only orders the rows still tied, by the points
    // each scored against the others of its group, then by wins.
    for (auto first = table.rows.begin(); first != table.rows.end();) {
        auto last = std::find_if(first, table.rows.end(), [&](const Standing& s) { return key(s) != key(*first); });
        if (last - first > 1) {
            std::map<int, double> among;
            for (auto a = first; a != last; ++a) {
                for (auto b = first; b != last; ++b) {
                    if (a != b) among[a->id] += table.pair(a->id, b->id).points();
                }
            }
            std::stable_sort(first, last, [&](const Standing& a, const Standing& b) {
                if (among[a.id] != among[b.id]) return among[a.id] > among[b.id];
                return a.wins > b.wins;
            });
        }
        first = last;
    }
    return table;
}

Table ordered(Table table, const TournamentSpec& spec, Order order) {
    if (order == Order::Points) return table;
    if (order == Order::Score) {
        std::stable_sort(table.rows.begin(), table.rows.end(),
                         [](const Standing& a, const Standing& b) { return a.score() > b.score(); });
        return table;
    }
    std::map<int, std::size_t> place;
    for (std::size_t i = 0; i < spec.participants.size(); ++i) place[spec.participants[i].id] = i;
    std::stable_sort(table.rows.begin(), table.rows.end(),
                     [&](const Standing& a, const Standing& b) { return place[a.id] < place[b.id]; });
    return table;
}

std::string tableText(const Table& table, const std::string& title, const std::string& footer) {
    std::size_t width = 6;
    for (const auto& r : table.rows) width = std::max(width, r.name.size());

    std::string out = title.empty() ? "" : title + "\n\n";
    // TF: the losses on time, as in the view.
    out += fmt::format("{:>3}  {:<{}}  {:>6}  {:>5}  {:>6}  {:>7}  {:>5}  {:>14}  {:>3}", "#", "Engine", width,
                       "Points", "Games", "%", "Elo", "+/-", "+ = -", "TF");
    for (std::size_t c = 0; c < table.rows.size(); ++c) out += fmt::format("  {:>4}", c + 1);
    out += "\n";

    for (std::size_t i = 0; i < table.rows.size(); ++i) {
        const auto& r = table.rows[i];
        out += fmt::format("{:>3}  {:<{}}  {:>6}  {:>5}  {:>5.1f}%  {:>+7}  {:>5.0f}  {:>14}  {:>3}", i + 1, r.name,
                           width, half(r.points), r.games, 100.0 * r.score(), std::lround(r.rating.elo), r.rating.error,
                           fmt::format("+{} ={} -{}", r.wins, r.draws, r.losses), r.timeouts);
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
    if (!footer.empty()) out += "\n" + footer + "\n";
    return out;
}

std::string bbcode(const std::string& title, const std::string& details, const std::string& text) {
    std::string out;
    if (!title.empty()) out += "[b]" + title + "[/b]\n";
    if (!details.empty()) out += details + "\n";
    out += "[code]\n" + text;
    if (!text.empty() && text.back() != '\n') out += '\n';
    return out + "[/code]\n";
}

std::string forumDetails(const TournamentSpec& spec, const Table& table) {
    static constexpr const char* FORMATS[] = {"Match", "Round robin", "Gauntlet", "Swiss", "Knockout"};
    int games                              = 0;
    for (const auto& r : table.rows) games += r.games;
    games /= 2;

    std::string out = FORMATS[static_cast<int>(spec.format)];
    if (spec.format != Format::Match) out += fmt::format(", {} engines", table.rows.size());
    if (!spec.participants.empty()) out += ", " + timeControlText(spec.participants.front().engine.limit);
    out += fmt::format(", {} game{}", games, games == 1 ? "" : "s");
    if (!spec.conditions.opening.file.empty()) {
        out += ", book " + std::filesystem::path(spec.conditions.opening.file).filename().string();
    }
    if (spec.conditions.variant == fastchess::VariantType::FRC) out += ", Chess960";
    return out;
}

std::string tableHtml(const Table& table, const std::string& title, const std::string& footer) {
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
p.foot{color:var(--muted);margin:12px 4px 0}
</style></head><body>
<h1>)" + escapeHtml(title) +
                      R"(</h1>
<div class="card"><table><thead><tr><th>#</th><th class="l">Engine</th><th>Points</th><th>Games</th><th>%</th><th>Elo</th><th>&plusmn;</th><th>+</th><th>=</th><th>&minus;</th><th title="Losses on time">TF</th><th>SB</th>)";
    for (std::size_t c = 0; c < table.rows.size(); ++c) out += fmt::format("<th>{}</th>", c + 1);
    out += "</tr></thead><tbody>\n";

    for (std::size_t i = 0; i < table.rows.size(); ++i) {
        const auto& r = table.rows[i];
        out += fmt::format(
            "<tr><td>{}</td><td class=\"l\">{}</td><td>{}</td><td>{}</td><td>{:.1f}</td><td>{:+}</td><td>{:.0f}</td>"
            "<td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{:.2f}</td>",
            i + 1, escapeHtml(r.name), half(r.points), r.games, 100.0 * r.score(), std::lround(r.rating.elo),
            r.rating.error, r.wins, r.draws, r.losses, r.timeouts, r.sb);
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
    out += "</tbody></table></div>\n";
    if (!footer.empty()) out += "<p class=\"foot\">" + escapeHtml(footer) + "</p>\n";
    out += "</body></html>\n";
    return out;
}

ScoreElo scoreElo(int wins, int draws, int losses) {
    const int n = wins + draws + losses;
    if (n == 0) return {};
    const double s = (wins + 0.5 * draws) / n;
    if (s <= 0 || s >= 1) return {};

    // The variance of one game's result around the mean (sample variance), then of the mean.
    const auto sq     = [&](double r) { return (r - s) * (r - s); };
    const double var  = (wins * sq(1.0) + draws * sq(0.5) + losses * sq(0.0)) / std::max(1, n - 1);
    const double sd   = std::sqrt(var / n);
    const auto elo    = [](double score) { return -400.0 * std::log10(1.0 / score - 1.0); };
    const double low  = elo(std::max(s - Z95 * sd, 1e-9));
    const double high = elo(std::min(s + Z95 * sd, 1.0 - 1e-9));
    return {elo(s) + 0.0, (high - low) / 2.0};  // + 0.0: an even score is +0, not -0
}

std::string footerLine(const TournamentSpec& spec, const Table& table, std::optional<uint64_t> planned) {
    int games = 0, draws = 0;
    for (const auto& r : table.rows) games += r.games, draws += r.draws;
    games /= 2;
    draws /= 2;

    std::string out = planned ? fmt::format("{}/{} games", games, std::max<uint64_t>(*planned, games))
                              : fmt::format("{} games", games);
    out += fmt::format(", {:.0f}% draws", games ? 100.0 * draws / games : 0.0);
    if (!spec.participants.empty()) {
        out += ", time control:" + timeControlText(spec.participants.front().engine.limit);
    }
    if (!spec.conditions.opening.file.empty()) {
        out += ", book:" + std::filesystem::path(spec.conditions.opening.file).filename().string();
    }
    if (!spec.name.empty()) out += ", event: " + spec.name;
    return out;
}

std::string gauntletText(const TournamentSpec& spec, const Table& table, const std::string& title,
                         std::optional<uint64_t> planned) {
    struct Row {
        std::string name;
        int wins = 0, draws = 0, losses = 0;
        [[nodiscard]] int games() const { return wins + draws + losses; }
        [[nodiscard]] double points() const { return wins + 0.5 * draws; }
    };

    std::string out  = title.empty() ? "" : title + "\n\n";
    const auto seeds = std::min<std::size_t>(std::max(1, spec.seeds), spec.participants.size());
    for (std::size_t s = 0; s < seeds; ++s) {
        const auto& seed = spec.participants[s];
        const auto it =
            std::find_if(table.rows.begin(), table.rows.end(), [&](const Standing& r) { return r.id == seed.id; });
        if (it == table.rows.end()) continue;

        // The seed, then whoever it met with its score against it, the best first.
        std::vector<Row> rows = {{it->name, it->wins, it->draws, it->losses}};
        std::vector<Row> opponents;
        for (const auto& r : table.rows) {
            const auto p = table.pair(r.id, seed.id);
            if (r.id != seed.id && p.games() > 0) opponents.push_back({r.name, p.wins, p.draws, p.losses});
        }
        std::stable_sort(opponents.begin(), opponents.end(), [](const Row& a, const Row& b) {
            return a.points() != b.points() ? a.points() > b.points() : a.name < b.name;
        });
        rows.insert(rows.end(), opponents.begin(), opponents.end());

        std::size_t name_w = 4, points_w = 6;
        const auto points = [](const Row& r) { return fmt::format("{:.1f}/{}", r.points(), r.games()); };
        for (const auto& r : rows) {
            name_w   = std::max(name_w, r.name.size());
            points_w = std::max(points_w, points(r).size());
        }
        const auto rank_w = fmt::format("{}", rows.size()).size();

        if (s > 0) out += "\n";
        const auto head = fmt::format("{:>{}}  {:<{}}  {:>{}}  {:>5}  {:>7}  {:>6}  W-L-D", "#", rank_w, "Name", name_w,
                                      "Points", points_w, "%", "Elo", "+/-");
        out += head + "\n" + std::string(head.size() + 8, '-') + "\n";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& r  = rows[i];
            const auto elo = scoreElo(r.wins, r.draws, r.losses);
            out += fmt::format("{:>{}}  {:<{}}  {:>{}}  {:>5.1f}  {:>7}  {:>6}  +{}-{}={}\n", i + 1, rank_w, r.name,
                               name_w, points(r), points_w, r.games() ? 100.0 * r.points() / r.games() : 0.0,
                               elo.elo ? fmt::format("{:+.1f}", *elo.elo) : "-",
                               elo.error ? fmt::format("{:.1f}", *elo.error) : "-", r.wins, r.losses, r.draws);
        }
    }
    return out + "\n" + footerLine(spec, table, planned) + "\n";
}

}  // namespace fcg::tour
