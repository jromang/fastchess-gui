// The Suites page: an EPD test suite run by several engines, as a grid of positions against engines.

#include <algorithm>
#include <cfloat>
#include <fstream>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "console.hpp"
#include "engines.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// An engine of the library as the suite runs it: its options, Hash and Threads forced when asked and it has them.
fastchess::EngineConfiguration suiteEngine(const EngineEntry& entry, int hash, int threads) {
    fastchess::EngineConfiguration config;
    config.name      = entry.name;
    config.cmd       = entry.cmd;
    config.args      = entry.args;
    config.dir       = engineWorkingDir(entry);
    config.options   = entry.options;
    config.protocol  = entry.protocol;
    const auto force = [&](const char* name, int value) {
        if (value <= 0) return;
        const bool has =
            entry.uci_options.empty() || std::any_of(entry.uci_options.begin(), entry.uci_options.end(),
                                                     [&](const UciOptionInfo& o) { return o.name == name; });
        if (!has) return;
        std::erase_if(config.options, [&](const auto& kv) { return kv.first == name; });
        config.options.emplace_back(name, std::to_string(value));
    };
    force("Hash", hash);
    force("Threads", threads);
    config.start_timeout_ms = entry.start_timeout * 1000LL;
    return config;
}

}  // namespace

void App::loadSuite() {
    suite_.reset();
    suite_error_.clear();
    suite_saved_.clear();
    suite_runs_.clear();
    if (suite_setup_.file.empty()) return;
    try {
        suite_ = tour::readSuite(suite_setup_.file);
        if (suite_->positions.empty()) suite_error_ = TR("No position with bm, am or c0 in this file.");
        suite_runs_ = savedSuiteRuns(storage_.root() / "suites", suite_setup_.file, suite_->positions.size());
    } catch (const std::exception& e) {
        suite_error_ = e.what();
        suite_.reset();
    }
}

void App::openSuite(const std::string& file) {
    suite_setup_.file = file;
    writeJson(storage_.root() / "suite.json", suite_setup_);
    loadSuite();
    page_ = Page::Suites;
}

void App::startSuite() {
    if (!suite_ || suite_->positions.empty() || suite_runner_.running()) return;
    std::vector<fastchess::EngineConfiguration> configs;
    int threads = 1;
    for (const auto& name : suite_setup_.engines) {
        const auto e = std::find_if(engines_.begin(), engines_.end(), [&](const auto& x) { return x.name == name; });
        if (e == engines_.end()) continue;
        configs.push_back(suiteEngine(*e, suite_setup_.hash, suite_setup_.threads));
        threads = std::max(threads, configThreads(configs.back()));
    }
    if (configs.empty()) return;
    const int concurrency = suite_setup_.concurrency_auto
                                ? std::max(1, topology_.defaultConcurrency(clockFree(suite_setup_.limit)) / threads)
                                : suite_setup_.concurrency;
    suite_runner_.start(*suite_, std::move(configs), suite_setup_, concurrency);
    suite_saved_run_ = false;
    Console::instance().add(trf("Test suite {}: {} positions, {} engines, {} at a time.",
                                fs::path(suite_setup_.file).filename().string(), suite_->positions.size(),
                                suite_setup_.engines.size(), concurrency));
}

void App::loadSuiteSetup() {
    if (suite_loaded_) return;
    suite_loaded_ = true;
    if (auto s = readJson<nlohmann::json>(storage_.root() / "suite.json")) {
        try {
            suite_setup_ = s->get<SuiteSetup>();
        } catch (...) {
        }
    }
    loadSuite();
}

void App::drawSuites() {
    loadSuiteSetup();

    // A run that just ended is kept, to compare later versions with.
    const bool running = suite_runner_.running();
    if (!running && !suite_saved_run_ && suite_runner_.total() > 0) {
        suite_saved_run_ = true;
        if (!suite_runner_.stopped() && suite_) {
            saveSuiteRun(storage_.root() / "suites", suite_setup_.file, suite_->positions.size(),
                         suite_runner_.columns());
            suite_runs_ = savedSuiteRuns(storage_.root() / "suites", suite_setup_.file, suite_->positions.size());
            Console::instance().add(TR("Test suite finished; its results are kept in suites/ in the profile."));
        }
    }

    bool changed        = false;
    const float setup_w = std::clamp(ImGui::GetContentRegionAvail().x * 0.25f, px(300), px(440));
    ImGui::BeginChild("##suite_setup", ImVec2(std::floor(setup_w), 0), ImGuiChildFlags_None);
    ImGui::BeginDisabled(running);

    // The suite: a file, or an EPD book of the library.
    ImGui::TextDisabled("%s", TR("Suite"));
    const auto stem =
        suite_setup_.file.empty() ? std::string(TR("(choose)")) : fs::path(suite_setup_.file).filename().string();
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("…").x - ImGui::GetStyle().FramePadding.x * 2 -
                            ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::BeginCombo("##suite_file", stem.c_str())) {
        for (const auto& b : books_) {
            if (fs::path(b.path).extension() != ".epd") continue;
            if (ImGui::Selectable(b.name.c_str(), b.path == suite_setup_.file)) openSuite(b.path);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("…##suite_browse")) {
        dialogs_.openFile(Dialogs::Purpose::SuiteFile, {{TR("EPD suites"), "epd"}, {TR("All files"), "*"}},
                          suite_setup_.file);
    }
    if (!suite_error_.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", suite_error_.c_str());
        ImGui::PopTextWrapPos();
    } else if (suite_) {
        ImGui::TextDisabled(
            "%s", trn("{} position", "{} positions", suite_->positions.size(), suite_->positions.size()).c_str());
        if (suite_->skipped > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", trf("· {} lines left out", suite_->skipped).c_str());
        }
        if (suite_->hasPoints()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", TR("· STS points"));
        }
    }

    // The engines.
    ImGui::Spacing();
    ImGui::TextDisabled("%s", TR("Engines"));
    ImGui::BeginChild("##suite_engines", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 6.5f), ImGuiChildFlags_Borders);
    for (const auto& e : engines_) {
        auto& list    = suite_setup_.engines;
        const auto it = std::find(list.begin(), list.end(), e.name);
        bool on       = it != list.end();
        if (ImGui::Checkbox(e.name.c_str(), &on)) {
            if (on) {
                list.push_back(e.name);
            } else {
                list.erase(it);
            }
            changed = true;
        }
    }
    ImGui::EndChild();

    // How long each search is.
    ImGui::Spacing();
    int limit = static_cast<int>(suite_setup_.limit);
    changed |= ImGui::RadioButton(TR("Time"), &limit, static_cast<int>(LimitKind::MoveTime));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Nodes"), &limit, static_cast<int>(LimitKind::Nodes));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Depth"), &limit, static_cast<int>(LimitKind::Depth));
    suite_setup_.limit = static_cast<LimitKind>(limit);
    // The inputs line up after the longest label.
    float label_w = 0;
    for (const char* text : {TR("Nodes per position"), TR("Plies"), TR("Seconds per position"), TR("Found for (plies)"),
                             TR("Hash (MB)"), TR("Threads"), TR("Searches at a time")}) {
        label_w = std::max(label_w, ImGui::CalcTextSize(text).x);
    }
    label_w += ImGui::GetStyle().ItemSpacing.x * 2;
    const auto label = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(px(100));
    };
    switch (suite_setup_.limit) {
        case LimitKind::Nodes: {
            label(TR("Nodes per position"));
            auto nodes = static_cast<int64_t>(suite_setup_.nodes);
            if (ImGui::InputScalar("##suite_nodes", ImGuiDataType_S64, &nodes)) {
                suite_setup_.nodes = static_cast<uint64_t>(std::max<int64_t>(1, nodes));
                changed            = true;
            }
            break;
        }
        case LimitKind::Depth:
            label(TR("Plies"));
            if (ImGui::InputInt("##suite_depth", &suite_setup_.depth, 0, 0)) {
                suite_setup_.depth = std::clamp(suite_setup_.depth, 1, 200);
                changed            = true;
            }
            break;
        default:
            suite_setup_.limit = LimitKind::MoveTime;
            label(TR("Seconds per position"));
            if (ImGui::InputDouble("##suite_time", &suite_setup_.move_s, 0, 0, "%.2f")) {
                suite_setup_.move_s = std::clamp(suite_setup_.move_s, 0.01, 3600.0);
                changed             = true;
            }
    }
    label(TR("Found for (plies)"));
    if (ImGui::InputInt("##suite_hold", &suite_setup_.hold, 0, 0)) {
        suite_setup_.hold = std::clamp(suite_setup_.hold, 0, 50);
        changed           = true;
    }
    ui::help(
        TR("The search stops once a solution has stayed the engine's first move for this many more plies of "
           "depth. 0: every search takes its whole time."));
    label(TR("Hash (MB)"));
    if (ImGui::InputInt("##suite_hash", &suite_setup_.hash, 0, 0)) {
        suite_setup_.hash = std::clamp(suite_setup_.hash, 0, 1 << 20);
        changed           = true;
    }
    label(TR("Threads"));
    if (ImGui::InputInt("##suite_threads", &suite_setup_.threads, 0, 0)) {
        suite_setup_.threads = std::clamp(suite_setup_.threads, 0, 1024);
        changed              = true;
    }
    ui::help(TR("0 = each engine's own setting."));
    changed |= ImGui::Checkbox(TR("Automatic concurrency"), &suite_setup_.concurrency_auto);
    ui::help(
        TR("With a time: physical cores − 1, divided by the engines' threads; with nodes or a depth, every "
           "hardware thread."));
    if (!suite_setup_.concurrency_auto) {
        label(TR("Searches at a time"));
        if (ImGui::InputInt("##suite_concurrency", &suite_setup_.concurrency, 0, 0)) {
            suite_setup_.concurrency = std::clamp(suite_setup_.concurrency, 1, topology_.logical);
            changed                  = true;
        }
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (running) {
        if (ui::iconButton("suite_stop", ui::Icon::Stop, TR("Stop"))) suite_runner_.stop();
    } else {
        const bool ready = suite_ && !suite_->positions.empty() && !suite_setup_.engines.empty();
        if (ui::iconButton("suite_start", ui::Icon::Play, TR("Start"), ready)) startSuite();
    }
    if (suite_runner_.total() > 0) {
        const float done = static_cast<float>(suite_runner_.answered()) / static_cast<float>(suite_runner_.total());
        ImGui::ProgressBar(done, ImVec2(-FLT_MIN, 0),
                           fmt::format("{} / {}", suite_runner_.answered(), suite_runner_.total()).c_str());
    }

    // Saved runs of this suite, added to the grid to compare with.
    if (!suite_runs_.empty()) {
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##suite_runs", TR("Compare with a saved run…"))) {
            for (const auto& [file, text] : suite_runs_) {
                if (ImGui::Selectable(text.c_str())) {
                    for (auto& c : loadSuiteRun(file)) suite_saved_.push_back(std::move(c));
                }
            }
            ImGui::EndCombo();
        }
        if (!suite_saved_.empty() && ImGui::SmallButton(TR("Remove the saved runs from the grid"))) {
            suite_saved_.clear();
        }
    }
    ImGui::EndChild();
    if (changed) writeJson(storage_.root() / "suite.json", suite_setup_);

    ImGui::SameLine(0, px(16));
    ImGui::BeginChild("##suite_grid");
    drawSuiteGrid();
    ImGui::EndChild();
}

void App::drawSuiteGrid() {
    if (!suite_ || suite_->positions.empty()) {
        ui::emptyState(
            TR("Choose an EPD file whose positions have their best move (bm), the moves to avoid (am), or "
               "STS points (c0)."));
        return;
    }
    auto columns = suite_runner_.columns();
    // A run of another suite is not this one's.
    if (!columns.empty() && columns.front().answers.size() != suite_->positions.size()) columns.clear();
    columns.insert(columns.end(), suite_saved_.begin(), suite_saved_.end());
    const bool points = suite_->hasPoints();

    if (!columns.empty()) {
        ImGui::BeginDisabled(suite_runner_.running());
        if (ImGui::Button(TR("Export CSV…"))) {
            suite_csv_ = suiteCsv(*suite_, columns);
            dialogs_.saveFile(Dialogs::Purpose::ExportSuiteCsv, {{"CSV", "csv"}},
                              (fs::path(suite_setup_.file).stem().string() + ".csv"));
        }
        ImGui::EndDisabled();
    }

    const int n_cols     = 3 + static_cast<int>(columns.size());
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##suite_table", n_cols, flags)) return;
    ImGui::TableSetupScrollFreeze(3, 2);
    ImGui::TableSetupColumn("#");
    ImGui::TableSetupColumn(TR("Position"));
    ImGui::TableSetupColumn(TR("Solution"));
    for (const auto& c : columns) {
        const auto title = c.when.empty() ? c.engine : fmt::format("{}\n{}", c.engine, c.when);
        ImGui::TableSetupColumn(title.c_str(), ImGuiTableColumnFlags_WidthFixed, px(120));
    }
    ImGui::TableHeadersRow();

    // The totals first, under the engines' names.
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", TR("Total"));
    ImGui::TableNextColumn();
    for (const auto& c : columns) {
        ImGui::TableNextColumn();
        const auto t = c.totals();
        ImGui::TextUnformatted(fmt::format("{} / {}", t.solved, suite_->positions.size()).c_str());
        if (t.solved > 0) ImGui::TextDisabled("%s", ui::formatClock(t.time_ms / t.solved).c_str());
        if (points) ImGui::TextDisabled("%s", trf("{} / {} points", t.points, suite_->maxPoints()).c_str());
        if (ImGui::IsItemHovered() && !c.limit.empty()) ImGui::SetTooltip("%s", c.limit.c_str());
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(suite_->positions.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& p = suite_->positions[static_cast<std::size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d", i + 1);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p.id.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.fen.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p.best_text.c_str());
            for (const auto& c : columns) {
                ImGui::TableNextColumn();
                const auto& a = static_cast<std::size_t>(i) < c.answers.size() ? c.answers[i] : std::nullopt;
                if (!a) {
                    ImGui::TextDisabled("·");
                    continue;
                }
                const auto text = a->solved ? fmt::format("✓ {}", ui::formatClock(a->time_ms))
                                            : fmt::format("✗ {}", a->move.empty() ? "—" : a->move);
                ImGui::PushStyleColor(ImGuiCol_Text, a->solved ? ui::GOOD : ui::BAD);
                ImGui::TextUnformatted((points ? fmt::format("{}  {}", text, a->points) : text).c_str());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", trf("{} · depth {} · {}", a->move.empty() ? "—" : a->move, a->depth,
                                                ui::formatClock(a->time_ms))
                                                .c_str());
                }
            }
        }
    }
    ImGui::EndTable();
}

void App::exportSuiteCsv(const std::string& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << suite_csv_;
    if (out) {
        Console::instance().add(trf("Test suite results written to {}", path));
    } else {
        Console::instance().add(trf("Cannot write {}", path), true);
    }
}

}  // namespace fcg
