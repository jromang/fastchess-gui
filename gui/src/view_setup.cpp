// The "Setup" window: everything a run is started with.

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include <algorithm>
#include <filesystem>

#include "app.hpp"
#include "i18n.hpp"
#include "run.hpp"
#include "storage.hpp"
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
    if (!ui::beginPanel(TR("Setup###setup"))) {
        ui::endPanel();
        return;
    }

    const bool locked = run_.running();
    if (locked) {
        ImGui::TextDisabled(TR("Settings are locked while a match is running."));
        ImGui::Separator();
    }

    ImGui::BeginDisabled(locked);

    setupFormat();
    if (setup_.format == TournamentFormat::Match) {
        setupEngines();
        setupMatch();
    } else {
        setupParticipants();
    }
    setupLimit();
    setupConcurrency();
    setupOpenings();
    setupAdjudication();
    setupPgn();

    ImGui::EndDisabled();
    ui::endPanel();
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
        changed |= inputDouble(TR("Base (s)"), "##tc_base", setup_.tc_base_s, 0, 36000, "%.2f");
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
    ui::help(
        TR("Pins every game to its own processor, shared by its two engines since they never think at the "
           "same time: 2 games keep 2 cores busy and leave the others idle. Useful with a clock, irrelevant to "
           "a node count."));

    // Warn about oversubscription, which distorts timed games.
    int threads         = 1;
    const auto consider = [&](const std::string& name, int own) {
        const int forced = own > 0 ? own : setup_.common_threads;
        for (const auto& e : engines_) {
            if (e.name == name) threads = std::max(threads, forced > 0 ? forced : e.threads());
        }
    };
    if (setup_.format == TournamentFormat::Match) {
        consider(setup_.engine_a, 0);
        consider(setup_.engine_b, 0);
    } else {
        for (const auto& p : setup_.participants) consider(p.name, p.threads);
    }

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
    if (setup_.book.empty()) {
        ImGui::TextDisabled("%s", setup_.chess960 ? TR("Without a book, the 960 starting positions, in random order.")
                                                  : TR("Without a book, every game starts from the initial position."));
    }

    changed |= ImGui::Checkbox(TR("Chess960"), &setup_.chess960);
    ui::help(
        TR("Fischer random chess. The engines must play it (the UCI_Chess960 option, or the fischerandom "
           "variant of a WinBoard engine). A book of Chess960 positions, or none for all 960."));

    changed |= ImGui::Checkbox(TR("Random order"), &setup_.book_random);
    changed |= inputInt(TR("Plies (PGN)"), "##plies", setup_.book_plies, -1, 1000);
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
        changed |= inputInt(TR("Score (cp)"), "##res_sc", setup_.resign_score, 0, 100000);
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
    if (changed) saveSetupSoon();
}

void App::setupFormat() {
    if (!ImGui::CollapsingHeader(TR("Format###sec_format"), ImGuiTreeNodeFlags_DefaultOpen)) return;

    bool changed = false;
    int format   = static_cast<int>(setup_.format);
    changed |= ImGui::RadioButton(TR("Match"), &format, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Round robin"), &format, 1);
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Gauntlet"), &format, 2);
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Swiss"), &format, 3);
    ImGui::SameLine();
    changed |= ImGui::RadioButton(TR("Knockout"), &format, 4);
    setup_.format = static_cast<TournamentFormat>(format);

    switch (setup_.format) {
        case TournamentFormat::Match:
            ImGui::TextDisabled("%s",
                                TR("Two engines: is the new version stronger? Statistics from A's point of view."));
            break;
        case TournamentFormat::RoundRobin:
            ImGui::TextDisabled("%s", TR("Every engine meets every other one, each time on two games with the same "
                                         "opening, colours reversed."));
            break;
        case TournamentFormat::Gauntlet:
            ImGui::TextDisabled("%s", TR("The first engines of the list (the tested ones) meet every other engine."));
            break;
        case TournamentFormat::Swiss:
            ImGui::TextDisabled("%s", TR("Each round pairs engines with similar scores, never twice the same; for "
                                         "large fields. The list order is the seeding."));
            break;
        case TournamentFormat::Knockout:
            ImGui::TextDisabled("%s", TR("Matches in a bracket, the winner goes through; tiebreak pairs of games when "
                                         "a match is tied. The list order is the seeding."));
            break;
    }

    ui::label(TR("Name"), labelWidth());
    {
        char buffer[256];
        const auto n = std::min(setup_.name.size(), sizeof(buffer) - 1);
        std::copy_n(setup_.name.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##tname", defaultTournamentName(setup_).c_str(), buffer, sizeof(buffer))) {
            setup_.name = buffer;
            changed     = true;
        }
    }

    if (setup_.format == TournamentFormat::Swiss) {
        changed |= inputInt(TR("Rounds"), "##rounds", setup_.cycles, 1, 1000);
        ui::help(TR("Each round is two games, colours reversed, against an engine with a similar score."));
    } else if (setup_.format == TournamentFormat::Knockout) {
        changed |= inputInt(TR("Game pairs per match"), "##ko_cycles", setup_.cycles, 1, 100);
        ui::help(TR("Each pair is two games on the same opening, colours reversed."));
    } else if (setup_.format != TournamentFormat::Match) {
        changed |= inputInt(TR("Cycles"), "##cycles", setup_.cycles, 1, 10000);
        ui::help(TR("How many times every pairing is played; each time is two games, colours reversed."));
        if (setup_.format == TournamentFormat::Gauntlet) {
            const int most = std::max(1, static_cast<int>(setup_.participants.size()) - 1);
            changed |= inputInt(TR("Tested engines"), "##seeds", setup_.seeds, 1, most);
            ui::help(TR("The first engines of the list play every other engine, not each other."));
        }
    }

    if (changed) saveSetupSoon();
}

void App::setupParticipants() {
    if (!ImGui::CollapsingHeader(TR("Participants###sec_participants"), ImGuiTreeNodeFlags_DefaultOpen)) return;

    bool changed = false;
    auto& list   = setup_.participants;

    // Settings forced on everyone unless an engine overrides them.
    ImGui::TextDisabled("%s", TR("Common settings, forced on every engine (0 = each engine's own):"));
    changed |= inputInt(TR("Hash (MB)"), "##common_hash", setup_.common_hash, 0, 1 << 20);
    changed |= inputInt(TR("Threads"), "##common_threads", setup_.common_threads, 0, 1024);

    const bool nodes     = setup_.limit == LimitKind::Nodes;
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    const float small    = 70.0f * ImGui::GetFontSize() / 16.0f;
    int remove = -1, up = -1;

    if (!list.empty() && ImGui::BeginTable("##participants", 7, flags)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn(TR("Engine"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Hash"), ImGuiTableColumnFlags_WidthFixed, small);
        ImGui::TableSetupColumn(TR("Threads"), ImGuiTableColumnFlags_WidthFixed, small);
        ImGui::TableSetupColumn(nodes ? TR("Nodes") : TR("Time %"), ImGuiTableColumnFlags_WidthFixed,
                                nodes ? small * 1.5f : small);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < list.size(); ++i) {
            auto& p = list[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool seed = setup_.format == TournamentFormat::Gauntlet && static_cast<int>(i) < setup_.seeds;
            if (seed) {
                ImGui::PushStyleColor(ImGuiCol_Text, ui::ACCENT);
                ImGui::Text("%d", static_cast<int>(i + 1));
                ImGui::PopStyleColor();
            } else {
                ImGui::TextDisabled("%d", static_cast<int>(i + 1));
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p.name.c_str());
            if (std::none_of(engines_.begin(), engines_.end(),
                             [&](const EngineEntry& e) { return e.name == p.name; })) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
                ImGui::TextUnformatted(TR("(not in the engine list)"));
                ImGui::PopStyleColor();
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputInt("##hash", &p.hash, 0, 0)) {
                p.hash  = std::max(0, p.hash);
                changed = true;
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputInt("##threads", &p.threads, 0, 0)) {
                p.threads = std::clamp(p.threads, 0, 1024);
                changed   = true;
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (nodes) {
                int value = static_cast<int>(std::min<uint64_t>(p.nodes, INT32_MAX));
                if (ImGui::InputInt("##nodes", &value, 0, 0)) {
                    p.nodes = static_cast<uint64_t>(std::max(0, value));
                    changed = true;
                }
            } else if (ImGui::InputInt("##time", &p.time_percent, 0, 0)) {
                p.time_percent = std::clamp(p.time_percent, 1, 1000);
                changed        = true;
            }
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(i == 0);
            if (ImGui::SmallButton("^")) up = static_cast<int>(i);
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
        ImGui::TextDisabled("%s", nodes ? TR("Hash, Threads, Nodes: 0 = the common value.")
                                        : TR("Hash, Threads: 0 = the common value. Time %: time odds, 100 = the "
                                             "same clock as the others."));
    }

    if (remove >= 0) {
        list.erase(list.begin() + remove);
        changed = true;
    }
    if (up > 0) {
        std::swap(list[up], list[up - 1]);
        changed = true;
    }

    // Adding engines.
    ImGui::SetNextItemWidth(260.0f * ImGui::GetFontSize() / 16.0f);
    if (ImGui::BeginCombo("##add_participant", TR("Add an engine…"))) {
        for (const auto& e : engines_) {
            const bool in = std::any_of(list.begin(), list.end(), [&](const auto& p) { return p.name == e.name; });
            if (in) continue;
            if (ImGui::Selectable(e.name.c_str())) {
                list.push_back({e.name});
                changed = true;
            }
        }
        if (engines_.empty()) ImGui::TextDisabled(TR("Add engines in the Engines window."));
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Add all"))) {
        for (const auto& e : engines_) {
            if (std::none_of(list.begin(), list.end(), [&](const auto& p) { return p.name == e.name; })) {
                list.push_back({e.name});
            }
        }
        changed = true;
    }
    // A whole group of engines, e.g. a pool.
    std::vector<std::string> groups;
    for (const auto& e : engines_) {
        for (const auto& g : e.groups) {
            if (std::find(groups.begin(), groups.end(), g) == groups.end()) groups.push_back(g);
        }
    }
    if (!groups.empty()) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f * ImGui::GetFontSize() / 16.0f);
        if (ImGui::BeginCombo("##add_group", TR("Add a group…"))) {
            std::sort(groups.begin(), groups.end());
            for (const auto& g : groups) {
                if (!ImGui::Selectable(g.c_str())) continue;
                for (const auto& e : engines_) {
                    const bool member = std::find(e.groups.begin(), e.groups.end(), g) != e.groups.end();
                    const bool in =
                        std::any_of(list.begin(), list.end(), [&](const auto& p) { return p.name == e.name; });
                    if (member && !in) list.push_back({e.name});
                }
                changed = true;
            }
            ImGui::EndCombo();
        }
    }
    if (!list.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(TR("Clear"))) {
            list.clear();
            changed = true;
        }
    }

    if (changed) saveSetupSoon();
}

void App::drawSetupSummary() {
    ui::beginCard("summary_card");
    ImGui::TextDisabled("%s", TR("Summary"));

    const auto n     = static_cast<int>(setup_.participants.size());
    const int cycles = std::max(1, setup_.cycles);
    const int seeds  = std::clamp(setup_.seeds, 1, std::max(1, n - 1));
    uint64_t games   = 0;
    std::string formula;
    std::string per_engine;

    switch (setup_.format) {
        case TournamentFormat::Match:
            if (setup_.mode == MatchMode::Fixed) {
                games = static_cast<uint64_t>((setup_.fixed_games + 1) / 2 * 2);
            } else if (setup_.mode == MatchMode::Sprt && setup_.sprt_max_games > 0) {
                games = static_cast<uint64_t>((setup_.sprt_max_games + 1) / 2 * 2);
            }
            formula = setup_.mode == MatchMode::Sprt       ? std::string(TR("until the SPRT decides"))
                      : setup_.mode == MatchMode::Infinite ? std::string(TR("until stopped"))
                                                           : trf("{} game pairs", games / 2);
            break;
        case TournamentFormat::RoundRobin: {
            const uint64_t pairs = n >= 2 ? static_cast<uint64_t>(n) * (n - 1) / 2 : 0;
            games                = pairs * 2 * cycles;
            formula              = trf("{} pairings × 2 games × {} cycles", pairs, cycles);
            per_engine           = trf("{} games per engine", n >= 2 ? (n - 1) * 2 * cycles : 0);
            break;
        }
        case TournamentFormat::Gauntlet: {
            const uint64_t pairs = n >= 2 ? static_cast<uint64_t>(seeds) * (n - seeds) : 0;
            games                = pairs * 2 * cycles;
            formula              = trf("{} pairings × 2 games × {} cycles", pairs, cycles);
            per_engine           = trf("{} games per tested engine", n >= 2 ? (n - seeds) * 2 * cycles : 0);
            break;
        }
        case TournamentFormat::Swiss: {
            const uint64_t pairs = static_cast<uint64_t>(n / 2);
            games                = pairs * 2 * cycles;
            formula              = trf("{} rounds × {} pairings × 2 games", cycles, pairs);
            per_engine           = trf("{} games per engine", 2 * cycles);
            break;
        }
        case TournamentFormat::Knockout: {
            const uint64_t matches = n >= 2 ? static_cast<uint64_t>(n - 1) : 0;
            games                  = matches * 2 * cycles;
            formula                = trf("{} matches × {} pairs of games, plus tiebreaks", matches, cycles);
            break;
        }
    }

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.8f);
    ImGui::TextUnformatted(games ? trf("{} games", games).c_str() : "∞");
    ImGui::PopFont();
    ImGui::TextDisabled("%s", formula.c_str());
    if (!per_engine.empty()) ImGui::TextDisabled("%s", per_engine.c_str());

    // A rough duration: a game lasts about two base times and 60 increments per side.
    const bool fixed_nodes = setup_.limit == LimitKind::Nodes;
    const int concurrency  = setup_.concurrency_auto ? topology_.defaultConcurrency(fixed_nodes) : setup_.concurrency;
    if (games > 0 && !fixed_nodes) {
        const double game_s  = 2.0 * (setup_.tc_base_s + 60.0 * setup_.tc_inc_s);
        const double seconds = static_cast<double>(games) * game_s / std::max(1, concurrency);
        ImGui::Spacing();
        ImGui::TextUnformatted(
            trf("About {} with {} games at a time", ui::formatDuration(seconds), concurrency).c_str());
    }

    // What could go wrong.
    ImGui::Spacing();
    const auto check = [](bool ok, const std::string& text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ok ? ui::GOOD : ui::WARN);
        ImGui::TextUnformatted(ok ? "✓" : "!");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", text.c_str());
    };
    if (setup_.format == TournamentFormat::Match) {
        check(!setup_.engine_a.empty() && !setup_.engine_b.empty() && setup_.engine_a != setup_.engine_b,
              TR("Two different engines chosen"));
    } else {
        check(n >= 2, trf("{} engines", n));
    }
    if (setup_.chess960) {
        check(true, setup_.book.empty() ? std::string(TR("Chess960: the 960 starting positions, in random order"))
                                        : trf("Chess960: {}", std::filesystem::path(setup_.book).filename().string()));
        std::vector<std::string> names;
        if (setup_.format == TournamentFormat::Match) {
            names = {setup_.engine_a, setup_.engine_b};
        } else {
            for (const auto& p : setup_.participants) names.push_back(p.name);
        }
        for (const auto& e : engines_) {
            const bool chosen = std::find(names.begin(), names.end(), e.name) != names.end();
            const bool plays =
                e.uci_options.empty() || std::any_of(e.uci_options.begin(), e.uci_options.end(),
                                                     [](const auto& o) { return o.name == "UCI_Chess960"; });
            if (chosen && !plays) check(false, trf("{} does not play Chess960.", e.name));
        }
    } else {
        check(!setup_.book.empty(),
              setup_.book.empty() ? std::string(TR("No opening book: every game starts from the initial position"))
                                  : trf("Openings: {}", std::filesystem::path(setup_.book).filename().string()));
    }
    if (setup_.common_hash > 0 || setup_.common_threads > 0) {
        std::string parts;
        if (setup_.common_hash > 0) parts += fmt::format("Hash {} MB", setup_.common_hash);
        if (setup_.common_threads > 0)
            parts += fmt::format("{}Threads {}", parts.empty() ? "" : ", ", setup_.common_threads);
        check(true, trf("Common settings: {}", parts));
    }
    for (const auto& p : setup_.participants) {
        if (setup_.format != TournamentFormat::Match && !fixed_nodes && p.time_percent != 100) {
            check(true, trf("{} plays with {} % of the time", p.name, p.time_percent));
        }
    }
    ui::endCard();

    ImGui::Spacing();
    drawTemplates();
}

void App::drawTemplates() {
    ui::beginCard("templates_card");
    ImGui::TextDisabled("%s", TR("Templates"));

    const auto dir = storage_.root() / "templates";
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".json") names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());

    for (const auto& name : names) {
        ImGui::PushID(name.c_str());
        if (ImGui::SmallButton(TR("Load"))) {
            if (auto loaded = readJson<MatchSetup>(dir / (name + ".json"))) {
                // The look of the program is not part of a template.
                loaded->language = setup_.language;
                loaded->theme    = setup_.theme;
                loaded->notify   = setup_.notify;
                setup_           = *loaded;
                saveSetupSoon();
            }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(name.c_str());
        ImGui::PopID();
    }
    if (names.empty()) ImGui::TextDisabled("%s", TR("No template yet."));

    char buffer[128];
    const auto n = std::min(template_name_.size(), sizeof(buffer) - 1);
    std::copy_n(template_name_.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize(TR("Save")).x - ImGui::GetStyle().FramePadding.x * 4);
    if (ImGui::InputTextWithHint("##template_name", TR("Template name"), buffer, sizeof(buffer)))
        template_name_ = buffer;
    ImGui::SameLine();
    ImGui::BeginDisabled(template_name_.empty());
    if (ImGui::Button(TR("Save"))) {
        std::filesystem::create_directories(dir, ec);
        std::string file = template_name_;
        for (auto& c : file) {
            if (c == '/' || c == '\\' || c == ':') c = '_';
        }
        if (writeJson(dir / (file + ".json"), setup_)) template_name_.clear();
    }
    ImGui::EndDisabled();
    ui::endCard();
}

}  // namespace fcg
