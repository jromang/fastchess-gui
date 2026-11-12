// "Results" (numbers) and "Charts" (plots).

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <implot.h>

#include <fmt/include/fmt/format.h>
#include <types/match_data.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

void bigValue(const char* caption, const std::string& value, ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_Text)) {
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", caption);
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::EndGroup();
}

ImVec4 eloColour(double elo, double err) {
    // The theme's colours, so the value stays readable in light and dark.
    if (std::isfinite(elo) && std::isfinite(err) && elo - err > 0) return ImGui::ColorConvertU32ToFloat4(ui::GOOD);
    if (std::isfinite(elo) && std::isfinite(err) && elo + err < 0) return ImGui::ColorConvertU32ToFloat4(ui::BAD);
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

// fishtest's statistics of the match, games against pairs, in a table.
void statisticsTable(const MatchStatistics& m, bool penta, bool sprt) {
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##statistics", penta ? 3 : 2, flags)) return;
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(TR("Games"), ImGuiTableColumnFlags_WidthFixed);
    if (penta) ImGui::TableSetupColumn(TR("Pairs"), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    const auto interval = [](const Interval& i, double scale, const char* unit) {
        if (!std::isfinite(i.value)) return std::string("—");
        return fmt::format("{:+.2f}{}  [{:+.2f}, {:+.2f}]", scale * i.value, unit, scale * i.low, scale * i.high);
    };
    const auto row = [&](const char* label, const char* help, const std::string& games, const std::string& pairs) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", label);
        if (help && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", help);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(games.c_str());
        if (penta) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(pairs.c_str());
        }
    };
    const auto number = [](double v, const char* format) {
        return std::isfinite(v) ? fmt::format(fmt::runtime(format), v) : std::string("—");
    };

    row(TR("Count"), nullptr, std::to_string(m.games.count), std::to_string(m.pairs.count));
    row(TR("Draw ratio"), TR("Pairs: those scoring 1 for each engine, a win and a loss or two draws."),
        fmt::format("{:.1f} %", 100 * m.games.draw_ratio), fmt::format("{:.1f} %", 100 * m.pairs.draw_ratio));
    const auto score = [](const Interval& i) {
        return std::isfinite(i.value)
                   ? fmt::format("{:.2f} %  [{:.2f}, {:.2f}]", 100 * i.value, 100 * i.low, 100 * i.high)
                   : std::string("—");
    };
    row(TR("Score"), nullptr, score(m.games.score), score(m.pairs.score));
    row("Elo", TR("Logistic Elo, with its 95 % interval."), interval(m.games.elo, 1, ""), interval(m.pairs.elo, 1, ""));
    row("nElo", TR("Normalized Elo (fishtest, OpenBench), with its 95 % interval."), interval(m.games.nelo, 1, ""),
        interval(m.pairs.nelo, 1, ""));
    row("BayesElo", TR("From the games only, with the DrawElo that goes with it."),
        std::isfinite(m.bayeselo) ? trf("{:+.2f}  (DrawElo {:.0f})", m.bayeselo, m.drawelo) : std::string("—"), "");
    row(TR("Standard deviation"), TR("Of one game's score. Pairs: as measured on the pairs."),
        number(m.games.stdev, "{:.4f}"), number(m.pairs.stdev, "{:.4f}"));
    if (sprt) row("LLR", nullptr, number(m.games.llr, "{:.3f}"), number(m.pairs.llr, "{:.3f}"));
    if (penta) {
        row(TR("Variance ratio"),
            TR("The variance of a game when the games are counted in pairs, over the variance of single games: "
               "below 1, the pairs need that much fewer games for the same precision."),
            "", number(m.variance_ratio, "{:.3f}"));
        row(TR("RMS bias"),
            TR("The spread of the advantage the openings give, in Elo: what playing each opening twice, colours "
               "reversed, cancels out."),
            "", std::isfinite(m.rms_bias) ? fmt::format("{:.1f} Elo", m.rms_bias) : std::string("—"));
        row(TR("Pair kinds"), TR("Two draws, and a win and a loss, among the pairs scoring 1."), "",
            trf("two draws {:.1f} % · a win and a loss {:.1f} %", 100 * m.two_draws, 100 * m.win_and_loss));
        row(TR("Pairs won / lost"), TR("Pairs scoring more than 1 for A, over those scoring less."), "",
            number(m.pairs_ratio, "{:.2f}"));
    }
    ImGui::EndTable();
}

// Keeps at most `limit` evenly spaced points (always the last one), so long runs plot fast.
std::vector<std::size_t> decimate(std::size_t n, std::size_t limit) {
    std::vector<std::size_t> idx;
    if (n == 0) return idx;
    const std::size_t step = std::max<std::size_t>(1, n / limit);
    for (std::size_t i = 0; i < n; i += step) idx.push_back(i);
    if (idx.back() != n - 1) idx.push_back(n - 1);
    return idx;
}

ImPlotSpec lineSpec() {
    ImPlotSpec spec;
    spec.LineColor  = ImVec4(0.43f, 0.66f, 0.87f, 1.0f);
    spec.LineWeight = 1.5f;
    return spec;
}

ImPlotSpec bandSpec() {
    ImPlotSpec spec;
    spec.FillColor = ImVec4(0.43f, 0.66f, 0.87f, 0.25f);
    return spec;
}

ImPlotSpec hlineSpec(ImVec4 colour) {
    ImPlotSpec spec;
    spec.LineColor = colour;
    spec.Flags     = ImPlotInfLinesFlags_Horizontal;
    return spec;
}

ImPlotSpec barSpec() {
    ImPlotSpec spec;
    spec.FillColor = ImVec4(0.43f, 0.66f, 0.87f, 0.85f);
    spec.LineColor = ImVec4(0.43f, 0.66f, 0.87f, 1.0f);
    return spec;
}

}  // namespace

void App::drawResults() {
    // As tall as its contents: the charts below take the rest.
    if (!ImGui::BeginChild("results", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY)) {
        ui::endPanel();
        return;
    }

    if (run_.state() == RunController::State::Idle) {
        ImGui::TextDisabled("%s", TR("No results yet."));
        ui::endPanel();
        return;
    }

    const auto& info = run_.info();
    const auto& s    = stats_;
    const int n      = s.sum();

    ImGui::Text(TR("%s  vs  %s"), info.engine_a.c_str(), info.engine_b.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(TR("· %s · %d in parallel%s"), info.limit_text.c_str(), info.concurrency,
                        info.affinity ? TR(" · affinity") : "");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(TR("Copy")).x - ImGui::GetFontSize() * 1.5f +
                    ImGui::GetCursorPosX() - ImGui::GetStyle().WindowPadding.x);
    if (const auto as = ui::copyButton("summary", true); as != ui::CopyAs::None) {
        const auto text = summaryText();
        ImGui::SetClipboardText((as == ui::CopyAs::Text ? text : tour::bbcode("", "", text)).c_str());
    }

    ImGui::Separator();

    const float column = 150.0f * ImGui::GetFontSize() / 16.0f;

    bigValue(TR("Elo (95% CI)"), current_.valid ? formatElo(current_.elo, current_.elo_err) : "—",
             eloColour(current_.elo, current_.elo_err));
    ImGui::SameLine(column * 1.2f);
    bigValue("nElo", current_.valid ? formatElo(current_.nelo, current_.nelo_err) : "—");
    ImGui::SameLine(column * 2.4f);
    bigValue("LOS", current_.valid ? fmt::format("{:.1f} %", current_.los) : "—");
    ImGui::SameLine(column * 3.2f);
    bigValue(TR("Score"), n ? fmt::format("{:.1f} %", s.pointsRatio()) : "—");
    ImGui::SameLine(column * 4.0f);
    bigValue(TR("Games"), fmt::format("{}", n));

    ImGui::Spacing();
    ImGui::Text(TR("A: +%d  =%d  -%d"), s.wins, s.draws, s.losses);
    ImGui::SameLine();
    ImGui::TextDisabled(TR("· draws %.1f %%"), n ? 100.0 * s.draws / n : 0.0);
    if (info.penta) {
        ImGui::SameLine();
        ImGui::TextDisabled(TR("· pairs [0-2]: %d  %d  %d  %d  %d"), s.penta_LL, s.penta_LD, s.penta_WL + s.penta_DD,
                            s.penta_WD, s.penta_WW);
        ui::help(TR("Results per game pair (same opening, colours reversed), in points for A: 0, 0.5, 1, 1.5, 2."));
    }

    if (info.sprt && sprt_) {
        const double lo = sprt_->getLowerBound();
        const double hi = sprt_->getUpperBound();
        const double t  = std::clamp((current_.llr - lo) / (hi - lo), 0.0, 1.0);

        ImGui::Text("SPRT [%.2f, %.2f] %s, α %.3f β %.3f", info.elo0, info.elo1, info.model.c_str(), info.alpha,
                    info.beta);
        ImGui::SameLine();
        const auto overlay = fmt::format("LLR {:.2f}  ({:.2f} … {:.2f})", current_.llr, lo, hi);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              current_.llr >= 0 ? ImVec4(0.47f, 0.78f, 0.47f, 1) : ImVec4(0.9f, 0.43f, 0.39f, 1));
        ImGui::ProgressBar(static_cast<float>(t), ImVec2(-FLT_MIN, 0), overlay.c_str());
        ImGui::PopStyleColor();
    }

    if (open_statistics_ && n > 0) {
        ImGui::SetNextItemOpen(true);
        open_statistics_ = false;
    }
    if (n > 0 && ImGui::TreeNode(TR("Statistics"))) {
        statisticsTable(matchStatistics(s, sprt_ ? &*sprt_ : nullptr), info.penta, info.sprt && sprt_);
        ImGui::TreePop();
    }

    drawPgnOutError();

    // Engine health, shown only when something happened.
    for (int i = 0; i < std::min(2, static_cast<int>(totals_.size())); ++i) {
        const auto& t    = totals_[i];
        const auto* name = i == 0 ? info.engine_a.c_str() : info.engine_b.c_str();
        if (t.timeouts + t.crashes + t.illegal > 0) {
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.3f, 1),
                               TR("%s: %d loss(es) on time, %d crash(es), %d illegal move(s)"), name, t.timeouts,
                               t.crashes, t.illegal);
        }
        if (const auto* tour = run_.tournament(); tour && i < static_cast<int>(tour->spec().participants.size())) {
            const auto problem = cpuProblem(t, configThreads(tour->spec().participants[i].engine));
            if (!problem.empty()) ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.3f, 1), "%s: %s", name, problem.c_str());
        }
    }

    ui::endPanel();
}

void App::drawCharts() {
    if (!ui::beginPanel(TR("Charts###charts"))) {
        ui::endPanel();
        return;
    }

    if (history_.empty()) {
        ui::emptyState(TR("The charts appear with the first results."));
        ui::endPanel();
        return;
    }

    const auto& info   = run_.info();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float half_w = (avail.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const float row_h  = std::max(160.0f, (avail.y - ImGui::GetStyle().ItemSpacing.y * 2) / 3.0f);

    // Elo with its confidence band, and the LLR, against the number of games.
    const auto idx = decimate(history_.size(), 2000);
    // The Elo series skips points whose variance is not defined yet; the LLR always exists.
    std::vector<double> x, elo, lo, hi, llr_x, llr;
    x.reserve(idx.size());
    for (auto i : idx) {
        const auto& p = history_[i];
        llr_x.push_back(p.games);
        llr.push_back(p.llr);
        if (!p.valid) continue;
        x.push_back(p.games);
        elo.push_back(p.elo);
        lo.push_back(p.elo - p.elo_err);
        hi.push_back(p.elo + p.elo_err);
    }

    constexpr ImPlotFlags plot_flags = ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect;

    if (ImPlot::BeginPlot(TR("Elo of A (95% CI)###plot_elo"), ImVec2(half_w, row_h),
                          plot_flags | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes(TR("games"), "Elo", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        if (!x.empty()) {
            ImPlot::PlotShaded("IC", x.data(), lo.data(), hi.data(), static_cast<int>(x.size()), bandSpec());
            ImPlot::PlotLine("Elo", x.data(), elo.data(), static_cast<int>(x.size()), lineSpec());
        }
        const double zero = 0;
        ImPlot::PlotInfLines("##zero", &zero, 1, hlineSpec(ImVec4(0.6f, 0.6f, 0.6f, 0.6f)));
        ImPlot::EndPlot();
    }

    ImGui::SameLine();

    if (info.sprt && sprt_) {
        if (ImPlot::BeginPlot("LLR###plot_llr", ImVec2(half_w, row_h), plot_flags | ImPlotFlags_NoLegend)) {
            const double lo_b = sprt_->getLowerBound();
            const double hi_b = sprt_->getUpperBound();
            ImPlot::SetupAxes(TR("games"), "LLR", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_Y1, lo_b * 1.15, hi_b * 1.15, ImPlotCond_Always);
            if (!llr.empty())
                ImPlot::PlotLine("LLR", llr_x.data(), llr.data(), static_cast<int>(llr.size()), lineSpec());
            const double bounds[] = {lo_b, hi_b};
            ImPlot::PlotInfLines("bounds", bounds, 2, hlineSpec(ImVec4(0.9f, 0.6f, 0.3f, 0.8f)));
            ImPlot::EndPlot();
        }
    } else {
        std::vector<double> nelo, nlo, nhi;
        for (auto i : idx) {
            const auto& p = history_[i];
            if (!p.valid) continue;
            nelo.push_back(p.nelo);
            nlo.push_back(p.nelo - p.nelo_err);
            nhi.push_back(p.nelo + p.nelo_err);
        }
        if (ImPlot::BeginPlot(TR("nElo of A (95% CI)###plot_nelo"), ImVec2(half_w, row_h),
                              plot_flags | ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes(TR("games"), "nElo", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            if (!x.empty()) {
                ImPlot::PlotShaded("IC", x.data(), nlo.data(), nhi.data(), static_cast<int>(x.size()), bandSpec());
                ImPlot::PlotLine("nElo", x.data(), nelo.data(), static_cast<int>(x.size()), lineSpec());
            }
            ImPlot::EndPlot();
        }
    }

    // Pentanomial distribution and game lengths.
    const auto& s = stats_;
    if (info.penta) {
        if (ImPlot::BeginPlot(TR("Pairs (points of A out of 2)###plot_pairs"), ImVec2(half_w, row_h),
                              plot_flags | ImPlotFlags_NoLegend)) {
            static const char* labels[] = {"0", "0.5", "1", "1.5", "2"};
            const double positions[]    = {0, 1, 2, 3, 4};
            const double values[] = {static_cast<double>(s.penta_LL), static_cast<double>(s.penta_LD),
                                     static_cast<double>(s.penta_WL + s.penta_DD), static_cast<double>(s.penta_WD),
                                     static_cast<double>(s.penta_WW)};
            ImPlot::SetupAxes(nullptr, TR("pairs"), ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxisTicks(ImAxis_X1, positions, 5, labels);
            ImPlot::SetupAxisLimits(ImAxis_X1, -0.6, 4.6, ImPlotCond_Always);
            ImPlot::PlotBars("pairs", values, 5, 0.6, 0, barSpec());
            ImPlot::EndPlot();
        }
    } else {
        if (ImPlot::BeginPlot(TR("Results of A###plot_results"), ImVec2(half_w, row_h),
                              plot_flags | ImPlotFlags_NoLegend)) {
            const char* labels[]     = {TR("losses"), TR("draws"), TR("wins")};
            const double positions[] = {0, 1, 2};
            const double values[]    = {static_cast<double>(s.losses), static_cast<double>(s.draws),
                                        static_cast<double>(s.wins)};
            ImPlot::SetupAxes(nullptr, TR("games"), ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxisTicks(ImAxis_X1, positions, 3, labels);
            ImPlot::SetupAxisLimits(ImAxis_X1, -0.6, 2.6, ImPlotCond_Always);
            ImPlot::PlotBars("results", values, 3, 0.6, 0, barSpec());
            ImPlot::EndPlot();
        }
    }

    ImGui::SameLine();

    if (ImPlot::BeginPlot(TR("Game length###plot_length"), ImVec2(half_w, row_h), plot_flags | ImPlotFlags_NoLegend)) {
        std::vector<double> lengths;
        lengths.reserve(games_.size());
        for (const auto& g : games_) lengths.push_back((g.plies + 1) / 2);
        ImPlot::SetupAxes(TR("moves"), TR("games"), ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        if (!lengths.empty()) {
            ImPlot::PlotHistogram("moves", lengths.data(), static_cast<int>(lengths.size()), 30, 1.0, ImPlotRange(),
                                  barSpec());
        }
        ImPlot::EndPlot();
    }

    // How games ended, and how deep and fast each engine searched.
    if (ImPlot::BeginPlot(TR("How games ended###plot_ends"), ImVec2(half_w, row_h),
                          plot_flags | ImPlotFlags_NoLegend)) {
        std::vector<double> counts(reasons_.size(), 0.0);
        for (const auto& g : games_) {
            if (g.reason < counts.size()) counts[g.reason] += 1;
        }

        std::vector<int> order(reasons_.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return counts[a] > counts[b]; });
        if (order.size() > 8) order.resize(8);
        std::reverse(order.begin(), order.end());

        std::vector<double> values, positions;
        std::vector<const char*> labels;
        for (std::size_t i = 0; i < order.size(); ++i) {
            values.push_back(counts[order[i]]);
            positions.push_back(static_cast<double>(i));
            labels.push_back(TR(reasons_[order[i]]));
        }

        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        if (!values.empty()) {
            ImPlot::SetupAxisTicks(ImAxis_Y1, positions.data(), static_cast<int>(positions.size()), labels.data());
            auto spec  = barSpec();
            spec.Flags = ImPlotBarsFlags_Horizontal;
            ImPlot::PlotBars("ends", values.data(), static_cast<int>(values.size()), 0.6, 0, spec);
        }
        ImPlot::EndPlot();
    }

    ImGui::SameLine();

    ImGui::BeginChild("##engine_stats", ImVec2(half_w, row_h), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(TR("Search per engine"));
    ImGui::Separator();
    if (ImGui::BeginTable("##search", 6, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn(TR("Engine"));
        ImGui::TableSetupColumn(TR("Avg depth"));
        ImGui::TableSetupColumn(TR("Avg NPS"));
        ImGui::TableSetupColumn(TR("Time / move"));
        ImGui::TableSetupColumn(TR("Decision"));
        ImGui::TableSetupColumn(TR("Processor"));
        ImGui::TableHeadersRow();
        for (int i = 0; i < std::min(2, static_cast<int>(totals_.size())); ++i) {
            const auto& t = totals_[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(i == 0 ? info.engine_a.c_str() : info.engine_b.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", t.moves ? static_cast<double>(t.depth_sum) / t.moves : 0.0);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(
                ui::formatCount(t.nps_samples ? static_cast<uint64_t>(t.nps_sum / t.nps_samples) : 0).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ui::formatClock(t.moves ? static_cast<int64_t>(t.time_ms / t.moves) : 0).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(
                t.decision_samples ? ui::formatClock(static_cast<int64_t>(t.decision_ms / t.decision_samples)).c_str()
                                   : "—");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", TR("When it settled on the move it played, on average: a decision long "
                                           "before the end of the time spent says the time was not used well."));
            }
            ImGui::TableNextColumn();
            const auto use = cpuUse(t);
            ImGui::TextUnformatted(
                use.measured ? fmt::format("{:.1f} · {:.0f} %", use.thinking, 100 * use.waiting).c_str() : "—");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", TR("Cores kept busy while it thinks · share of a core used while its opponent "
                                           "thinks (0 % without pondering). Measured after 20 s of each."));
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ui::endPanel();
}

}  // namespace fcg
