// "Results" (numbers) and "Charts" (plots).

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <implot.h>

#include <fmt/include/fmt/core.h>
#include <types/match_data.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

void bigValue(const char* caption, const std::string& value, ImVec4 colour = ImVec4(1, 1, 1, 1)) {
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
    if (!std::isfinite(elo) || !std::isfinite(err)) return ImVec4(1, 1, 1, 1);
    if (elo - err > 0) return ImVec4(0.47f, 0.78f, 0.47f, 1);
    if (elo + err < 0) return ImVec4(0.9f, 0.43f, 0.39f, 1);
    return ImVec4(1, 1, 1, 1);
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
    if (!ImGui::Begin(TR("Results###results"))) {
        ImGui::End();
        return;
    }

    if (run_.state() == RunController::State::Idle) {
        ImGui::TextDisabled("%s", TR("No results yet."));
        ImGui::End();
        return;
    }

    const auto& info = run_.info();
    const auto& s    = stats_;
    const int n      = s.sum();

    ImGui::Text(TR("%s  vs  %s"), info.engine_a.c_str(), info.engine_b.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(TR("· %s · %d in parallel%s"), info.limit_text.c_str(), info.concurrency,
                        info.affinity ? TR(" · affinity") : "");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(TR("Copy summary")).x -
                    ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetCursorPosX() - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::SmallButton(TR("Copy summary"))) ImGui::SetClipboardText(summaryText().c_str());

    ImGui::Separator();

    const float column = 150.0f * ImGui::GetFontSize() / 16.0f;

    bigValue(TR("Elo (95% CI)"), current_.valid ? formatElo(current_.elo, current_.elo_err) : "—",
             eloColour(current_.elo, current_.elo_err));
    ImGui::SameLine(column * 1.2f);
    bigValue("nElo", current_.valid ? formatElo(current_.nelo, current_.nelo_err) : "—");
    ImGui::SameLine(column * 2.4f);
    bigValue("LOS", current_.valid ? fmt::format("{:.1f} %", current_.los) : "—");
    ImGui::SameLine(column * 3.2f);
    bigValue("Score", n ? fmt::format("{:.1f} %", s.pointsRatio()) : "—");
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

    // Engine health, shown only when something happened.
    for (int i = 0; i < 2; ++i) {
        const auto& t = totals_[i];
        if (t.timeouts + t.crashes + t.illegal == 0) continue;
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.3f, 1),
                           TR("%s: %d loss(es) on time, %d crash(es), %d illegal move(s)"),
                           i == 0 ? info.engine_a.c_str() : info.engine_b.c_str(), t.timeouts, t.crashes, t.illegal);
    }

    ImGui::End();
}

void App::drawCharts() {
    if (!ImGui::Begin(TR("Charts###charts"))) {
        ImGui::End();
        return;
    }

    if (history_.empty()) {
        ImGui::TextDisabled("%s", TR("The charts appear with the first results."));
        ImGui::End();
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
    if (ImGui::BeginTable("##search", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn(TR("Engine"));
        ImGui::TableSetupColumn(TR("Avg depth"));
        ImGui::TableSetupColumn(TR("Avg NPS"));
        ImGui::TableSetupColumn(TR("Time / move"));
        ImGui::TableHeadersRow();
        for (int i = 0; i < 2; ++i) {
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
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace fcg
