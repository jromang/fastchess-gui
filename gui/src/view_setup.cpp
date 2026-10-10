// The "Setup" window: everything a run is started with.

#include <imgui.h>

#include <fmt/include/fmt/core.h>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

constexpr float LABEL_WIDTH = 150.0f;

float labelWidth() { return LABEL_WIDTH * ImGui::GetFontSize() / 16.0f; }

bool inputInt(const char* label, const char* id, int& value, int min, int max, float width = 110.0f) {
    ui::label(label, labelWidth());
    ImGui::SetNextItemWidth(width * ImGui::GetFontSize() / 16.0f);
    const bool changed = ImGui::InputInt(id, &value, 0, 0);
    if (changed) value = std::clamp(value, min, max);
    return changed;
}

bool inputDouble(const char* label, const char* id, double& value, double min, double max, const char* fmt = "%.2f",
                 float width = 110.0f) {
    ui::label(label, labelWidth());
    ImGui::SetNextItemWidth(width * ImGui::GetFontSize() / 16.0f);
    const bool changed = ImGui::InputDouble(id, &value, 0, 0, fmt);
    if (changed) value = std::clamp(value, min, max);
    return changed;
}

bool inputText(const char* id, std::string& value, float width) {
    char buffer[2048];
    const auto n = std::min(value.size(), sizeof(buffer) - 1);
    std::copy_n(value.data(), n, buffer);
    buffer[n] = 0;

    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(id, buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

bool engineCombo(const char* id, std::string& value, const std::vector<EngineEntry>& engines) {
    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(id, value.empty() ? TR("(choose)") : value.c_str())) {
        for (const auto& e : engines) {
            const bool selected = e.name == value;
            if (ImGui::Selectable(e.name.c_str(), selected)) {
                value   = e.name;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        if (engines.empty()) ImGui::TextDisabled(TR("Add engines in the Engines window."));
        ImGui::EndCombo();
    }
    return changed;
}

}  // namespace

void App::drawSetup() {
    if (!ImGui::Begin(TR("Setup###setup"))) {
        ImGui::End();
        return;
    }

    const bool locked = run_.running();
    if (locked) {
        ImGui::TextDisabled(TR("Settings are locked while a match is running."));
        ImGui::Separator();
    }

    ImGui::BeginDisabled(locked);

    setupEngines();
    setupMatch();
    setupLimit();
    setupConcurrency();
    setupOpenings();
    setupAdjudication();
    setupPgn();

    ImGui::EndDisabled();
    ImGui::End();
}

void App::setupEngines() {
    if (!ImGui::CollapsingHeader(TR("Engines###sec_engines"), ImGuiTreeNodeFlags_DefaultOpen)) return;

    ui::label(TR("A (tested)"), labelWidth() * 0.75f);
    if (engineCombo("##engine_a", setup_.engine_a, engines_)) saveSetupSoon();

    ui::label(TR("B (baseline)"), labelWidth() * 0.75f);
    const float swap_w = ImGui::GetFrameHeight() * 1.6f;
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - swap_w - ImGui::GetStyle().ItemSpacing.x);
    {
        bool changed = false;
        if (ImGui::BeginCombo("##engine_b", setup_.engine_b.empty() ? TR("(choose)") : setup_.engine_b.c_str())) {
            for (const auto& e : engines_) {
                if (ImGui::Selectable(e.name.c_str(), e.name == setup_.engine_b)) {
                    setup_.engine_b = e.name;
                    changed         = true;
                }
            }
            if (engines_.empty()) ImGui::TextDisabled(TR("Add engines in the Engines window."));
            ImGui::EndCombo();
        }
        if (changed) saveSetupSoon();
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ui::iconButton("swap", ui::Icon::Swap, "")) {
        std::swap(setup_.engine_a, setup_.engine_b);
        saveSetupSoon();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(TR("Swap A and B"));

    ImGui::TextDisabled(TR("Statistics are given from A's point of view."));
}

void App::setupMatch() {
    if (!ImGui::CollapsingHeader(TR("Match###sec_match"), ImGuiTreeNodeFlags_DefaultOpen)) return;

    int mode     = static_cast<int>(setup_.mode);
    bool changed = false;
    changed |= ImGui::RadioButton(TR("Fixed games"), &mode, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Infinite"), &mode, 1);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("SPRT", &mode, 2);
    setup_.mode = static_cast<MatchMode>(mode);

    switch (setup_.mode) {
        case MatchMode::Fixed:
            changed |= inputInt(TR("Games"), "##games", setup_.fixed_games, 2, 100'000'000);
            ui::help(TR("Played in pairs: each opening is played twice, with colours reversed."));
            break;
        case MatchMode::Infinite:
            ImGui::TextDisabled(TR("Plays until you stop the match."));
            break;
        case MatchMode::Sprt: {
            changed |= inputDouble("elo0", "##elo0", setup_.sprt_elo0, -1000, 1000);
            changed |= inputDouble("elo1", "##elo1", setup_.sprt_elo1, -1000, 1000);
            changed |= inputDouble("alpha", "##alpha", setup_.sprt_alpha, 0.001, 0.5, "%.3f");
            changed |= inputDouble("beta", "##beta", setup_.sprt_beta, 0.001, 0.5, "%.3f");

            ui::label(TR("Model"), labelWidth());
            ImGui::SetNextItemWidth(140.0f * ImGui::GetFontSize() / 16.0f);
            const char* models[] = {"normalized", "logistic", "bayesian"};
            int model            = 0;
            for (int i = 0; i < 3; ++i) {
                if (setup_.sprt_model == models[i]) model = i;
            }
            if (ImGui::Combo("##model", &model, models, 3)) {
                setup_.sprt_model = models[model];
                changed           = true;
            }
            ui::help(
                TR("normalized: bounds in nElo (fishtest, OpenBench). logistic: classic Elo. bayesian: BayesElo, "
                   "without pentanomial."));

            changed |= inputInt(TR("Max games"), "##sprt_max", setup_.sprt_max_games, 0, 100'000'000);
            ui::help(TR("0 = no limit."));

            ImGui::TextDisabled(TR("Presets:"));
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Gainer [0, 3]"))) {
                setup_.sprt_elo0 = 0;
                setup_.sprt_elo1 = 3;
                changed          = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Non-regression [-5, 0]"))) {
                setup_.sprt_elo0 = -5;
                setup_.sprt_elo1 = 0;
                changed          = true;
            }
            break;
        }
    }

    if (changed) saveSetupSoon();
}

void App::setupLimit() {
    if (!ImGui::CollapsingHeader(TR("Time control###sec_limit"), ImGuiTreeNodeFlags_DefaultOpen)) return;

    int limit    = static_cast<int>(setup_.limit);
    bool changed = false;
    changed |= ImGui::RadioButton(TR("Time"), &limit, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Fixed nodes"), &limit, 1);
    setup_.limit = static_cast<LimitKind>(limit);

    if (setup_.limit == LimitKind::Time) {
        changed |= inputDouble("Base (s)", "##tc_base", setup_.tc_base_s, 0, 36000, "%.2f");
        changed |= inputDouble(TR("Increment (s)"), "##tc_inc", setup_.tc_inc_s, 0, 3600, "%.3f");
        changed |= inputInt(TR("Moves / period"), "##tc_moves", setup_.tc_moves, 0, 1000);
        ui::help(TR("0 = the base time covers the whole game. Otherwise it is given again every N moves."));
        changed |= inputInt(TR("Margin (ms)"), "##tc_margin", setup_.tc_margin_ms, 0, 100000);
        ui::help(TR("Overrun tolerated before a game is lost on time."));
    } else {
        int64_t nodes = static_cast<int64_t>(setup_.nodes);
        ui::label(TR("Nodes per move"), labelWidth());
        ImGui::SetNextItemWidth(140.0f * ImGui::GetFontSize() / 16.0f);
        if (ImGui::InputScalar("##nodes", ImGuiDataType_S64, &nodes)) {
            setup_.nodes = static_cast<uint64_t>(std::max<int64_t>(1, nodes));
            changed      = true;
        }
    }

    if (changed) saveSetupSoon();
}

void App::setupConcurrency() {
    if (!ImGui::CollapsingHeader(TR("Concurrency###sec_concurrency"), ImGuiTreeNodeFlags_DefaultOpen)) return;

    bool changed           = false;
    const bool fixed_nodes = setup_.limit == LimitKind::Nodes;
    const int auto_value   = topology_.defaultConcurrency(fixed_nodes);

    ImGui::TextDisabled(TR("Machine: %d physical cores, %d threads"), topology_.physical, topology_.logical);

    changed |= ImGui::Checkbox(TR("Automatic"), &setup_.concurrency_auto);
    ui::help(
        TR("Fixed nodes: one game per hardware thread (Hyper-Threading does not change a node count). With a clock: "
           "physical cores − 1, so the engines do not slow each other down."));

    int value = setup_.concurrency_auto ? auto_value : setup_.concurrency;
    ui::label(TR("Concurrent games"), labelWidth());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(setup_.concurrency_auto);
    if (ImGui::SliderInt("##concurrency", &value, 1, topology_.logical)) {
        setup_.concurrency = value;
        changed            = true;
    }
    ImGui::EndDisabled();
    if (!setup_.concurrency_auto) setup_.concurrency = std::clamp(setup_.concurrency, 1, topology_.logical);

    changed |= ImGui::Checkbox(TR("CPU affinity"), &setup_.affinity);
    ui::help(TR("Pins every game to its own processor. Useful with a clock, irrelevant to a node count."));

    // Warn about oversubscription, which distorts timed games.
    const EngineEntry* a = nullptr;
    const EngineEntry* b = nullptr;
    for (const auto& e : engines_) {
        if (e.name == setup_.engine_a) a = &e;
        if (e.name == setup_.engine_b) b = &e;
    }
    const int threads = std::max(a ? a->threads() : 1, b ? b->threads() : 1);

    if (setup_.affinity && threads > 1) {
        ImGui::TextColored(ui::MUTED4, TR("Affinity ignored: an engine uses %d threads."), threads);
    }
    if (value * threads > topology_.logical) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.3f, 1), TR("%d games × %d threads > %d available threads."), value,
                           threads, topology_.logical);
        ImGui::PopTextWrapPos();
    } else if (!fixed_nodes && value * threads > topology_.physical) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.3f, 1),
                           TR("With a clock, %d games × %d threads exceed the %d physical cores."), value, threads,
                           topology_.physical);
        ImGui::PopTextWrapPos();
    }

    if (changed) saveSetupSoon();
}

void App::setupOpenings() {
    if (!ImGui::CollapsingHeader(TR("Openings###sec_openings"), ImGuiTreeNodeFlags_DefaultOpen)) return;

    bool changed       = false;
    const float browse = ImGui::CalcTextSize("…").x + ImGui::GetStyle().FramePadding.x * 4;

    changed |=
        inputText("##book", setup_.book, ImGui::GetContentRegionAvail().x - browse - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::IsItemHovered() && !setup_.book.empty()) ImGui::SetTooltip("%s", setup_.book.c_str());
    ImGui::SameLine();
    if (ImGui::Button("…##book_browse")) {
        dialogs_.openFile(Dialogs::Purpose::Book, {{TR("Books (EPD, PGN)"), "epd;pgn"}, {TR("All files"), "*"}},
                          setup_.book);
    }
    if (setup_.book.empty()) ImGui::TextDisabled(TR("Without a book, every game starts from the initial position."));

    changed |= ImGui::Checkbox(TR("Random order"), &setup_.book_random);
    changed |= inputInt("Plies (PGN)", "##plies", setup_.book_plies, -1, 1000);
    ui::help(TR("For a PGN book: number of plies kept. -1 = all."));
    changed |= inputInt(TR("Start at"), "##book_start", setup_.book_start, 1, 100'000'000);

    int64_t seed = static_cast<int64_t>(setup_.seed);
    ui::label(TR("Seed"), labelWidth());
    ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 16.0f);
    if (ImGui::InputScalar("##seed", ImGuiDataType_S64, &seed)) {
        setup_.seed = static_cast<uint64_t>(std::max<int64_t>(0, seed));
        changed     = true;
    }
    ui::help(TR("0 = a new random order for every match."));

    if (changed) saveSetupSoon();
}

void App::setupAdjudication() {
    if (!ImGui::CollapsingHeader(TR("Adjudication###sec_adjudication"))) return;

    bool changed = false;

    changed |= ImGui::Checkbox(TR("Draw"), &setup_.draw_enabled);
    ui::help(
        TR("Draw when both engines report |score| ≤ threshold for N consecutive moves, after a given move number."));
    if (setup_.draw_enabled) {
        ImGui::Indent();
        changed |= inputInt(TR("From move"), "##draw_mn", setup_.draw_movenumber, 0, 1000);
        changed |= inputInt(TR("Consecutive moves"), "##draw_mc", setup_.draw_movecount, 1, 1000);
        changed |= inputInt(TR("Max score (cp)"), "##draw_sc", setup_.draw_score, 0, 10000);
        ImGui::Unindent();
    }

    changed |= ImGui::Checkbox(TR("Resign"), &setup_.resign_enabled);
    ui::help(TR("Loss when an engine reports a score ≤ -threshold for N consecutive moves."));
    if (setup_.resign_enabled) {
        ImGui::Indent();
        changed |= inputInt(TR("Consecutive moves"), "##res_mc", setup_.resign_movecount, 1, 1000);
        changed |= inputInt("Score (cp)", "##res_sc", setup_.resign_score, 0, 100000);
        changed |= ImGui::Checkbox(TR("Both engines must agree"), &setup_.resign_twosided);
        ImGui::Unindent();
    }

    changed |= ImGui::Checkbox(TR("Maximum number of moves"), &setup_.maxmoves_enabled);
    if (setup_.maxmoves_enabled) {
        ImGui::Indent();
        changed |= inputInt(TR("Moves"), "##maxmoves", setup_.maxmoves, 1, 100000);
        ImGui::Unindent();
    }

    changed |= ImGui::Checkbox(TR("Syzygy tablebases"), &setup_.tb_enabled);
    if (setup_.tb_enabled) {
        ImGui::Indent();
        const float browse = ImGui::CalcTextSize("…").x + ImGui::GetStyle().FramePadding.x * 4;
        changed |= inputText("##tb_paths", setup_.tb_paths,
                             ImGui::GetContentRegionAvail().x - browse - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::IsItemHovered() && !setup_.tb_paths.empty()) ImGui::SetTooltip("%s", setup_.tb_paths.c_str());
        ImGui::SameLine();
        if (ImGui::Button("…##tb_browse")) dialogs_.openFolder(Dialogs::Purpose::Tablebases);
#ifdef _WIN32
        ImGui::TextDisabled(TR("Directories separated by ';' (WDL files are enough)."));
#else
        ImGui::TextDisabled(TR("Directories separated by ':' (WDL files are enough)."));
#endif
        changed |= inputInt(TR("Max pieces"), "##tb_pieces", setup_.tb_pieces, 0, 7);
        ui::help(TR("0 = everything the tables cover."));
        changed |= ImGui::Checkbox(TR("Ignore the 50-move rule"), &setup_.tb_ignore50);

        ui::label(TR("Adjudicate"), labelWidth());
        ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 16.0f);
        const char* modes[] = {TR("wins and losses"), TR("draws"), TR("both")};
        int mode            = std::clamp(setup_.tb_result, 1, 3) - 1;
        if (ImGui::Combo("##tb_mode", &mode, modes, 3)) {
            setup_.tb_result = mode + 1;
            changed          = true;
        }
        ImGui::Unindent();
    }

    if (changed) saveSetupSoon();
}

void App::setupPgn() {
    if (!ImGui::CollapsingHeader(TR("PGN and misc###sec_misc"))) return;

    bool changed = false;

    ui::label(TR("Event"), labelWidth());
    changed |= inputText("##event", setup_.pgn_event, -FLT_MIN);

    ImGui::TextDisabled(TR("Every game is written to two files in the match folder:"));
    ImGui::BulletText("games.pgn : {+0.35/18 1.234s} (cutechess, fastchess)");
    ImGui::BulletText("games-chessbase.pgn : {[%%eval 0.35,18] [%%emt …]}");

    ImGui::TextUnformatted(TR("Extra fields in the comments:"));
    changed |= ImGui::Checkbox(TR("nodes"), &setup_.pgn_nodes);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("seldepth", &setup_.pgn_seldepth);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("nps", &setup_.pgn_nps);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("pv", &setup_.pgn_pv);

    ImGui::Spacing();
    changed |= ImGui::Checkbox(TR("Restart an engine that crashes"), &setup_.recover);
    ui::help(TR("Otherwise an engine that crashes or hangs stops the whole match."));
    changed |= ImGui::Checkbox(TR("Notify when the match ends"), &setup_.notify);

    ui::label(TR("Language"), labelWidth());
    ImGui::SetNextItemWidth(200.0f * ImGui::GetFontSize() / 16.0f);
    const char* languages[] = {TR("Automatic (system)"), "Français", "English"};
    int language            = setup_.language == "fr" ? 1 : setup_.language == "en" ? 2 : 0;
    if (ImGui::Combo("##language", &language, languages, 3)) {
        setup_.language = language == 1 ? "fr" : language == 2 ? "en" : "auto";
        i18n::setLanguage(i18n::resolve(setup_.language));
        changed = true;
    }

    if (changed) saveSetupSoon();
}

}  // namespace fcg
