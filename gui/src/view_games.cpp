// The "Games" window: finished games, replay and export.

#include <algorithm>
#include <cmath>
#include <map>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include <tour/engine_log.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

const char* FILTERS[] = {N_("All"),
                         N_("Wins of A"),
                         N_("Draws"),
                         N_("Losses of A"),
                         N_("Losses on time"),
                         N_("Crash / illegal move"),
                         N_("Adjudications"),
                         N_("The engines disagree"),
                         N_("An evaluation swings"),
                         N_("Lost after a winning score"),
                         N_("Wrong PVs"),
                         N_("Unbalanced opening")};

// In a tournament, results are seen from the engine chosen in the filter, or from White when none is.
const char* TOURNAMENT_FILTERS[] = {N_("All"),
                                    N_("Wins"),
                                    N_("Draws"),
                                    N_("Losses"),
                                    N_("Losses on time"),
                                    N_("Crash / illegal move"),
                                    N_("Adjudications"),
                                    N_("The engines disagree"),
                                    N_("An evaluation swings"),
                                    N_("Lost after a winning score"),
                                    N_("Wrong PVs"),
                                    N_("Unbalanced opening")};
static_assert(IM_ARRAYSIZE(FILTERS) == IM_ARRAYSIZE(TOURNAMENT_FILTERS));

// The filters on what the evaluations tell, which take a threshold in pawns.
constexpr int FIRST_EVAL_FILTER = 7;
constexpr int LAST_EVAL_FILTER  = 9;
static_assert(UNBALANCED_FILTER == IM_ARRAYSIZE(FILTERS) - 1);

// The ply an evaluation filter is about, -1 for the others.
int pliesOfInterest(const GameRecord& g, int filter) {
    switch (filter) {
        case 7:
            return g.facts.disagree_ply;
        case 8:
            return g.facts.swing_ply;
        case 9:
            return g.facts.loser_ply;
        case 10:
            return g.pv_ply;
        default:
            return -1;
    }
}

// Result of `g` for participant `subject` (+1, 0, -1); a match uses A, a tournament without subject uses White.
int resultFor(const GameRecord& g, int subject, bool match) {
    if (match) return g.a_result;
    if (subject < 0 || g.white == subject) return g.white_result;
    return -g.white_result;
}

bool matches(const GameRecord& g, int filter, int subject, bool match, float pawns) {
    using T         = fastchess::MatchTermination;
    const auto term = static_cast<T>(g.termination);

    if (!match && subject >= 0 && g.white != subject && g.black != subject) return false;

    switch (filter) {
        case 1:
            return resultFor(g, subject, match) > 0;
        case 2:
            return resultFor(g, subject, match) == 0;
        case 3:
            return resultFor(g, subject, match) < 0;
        case 4:
            return term == T::TIMEOUT;
        case 5:
            return term == T::DISCONNECT || term == T::STALL || term == T::ILLEGAL_MOVE;
        case 6:
            return term == T::ADJUDICATION;
        case 7:
            return g.facts.disagreement >= pawns;
        case 8:
            return g.facts.swing >= pawns;
        case 9:
            return g.facts.loser_best >= pawns;
        case 10:
            return g.pv_warnings > 0;
        case UNBALANCED_FILTER:
            return g.unbalanced != 0;
        default:
            return true;
    }
}

const char* resultText(const GameRecord& g) {
    if (g.white_result == 0) return "½-½";
    return g.white_result > 0 ? "1-0" : "0-1";
}

// What the tooltip of a result set by hand tells: how the game ended as played, and why.
std::string resultSetTip(const tour::ResultSet& set) {
    std::string tip;
    if (set.played_result == "*") {
        tip = TR("Ended by the operator while it was played.");
    } else {
        const auto played = set.played_result == "1/2-1/2" ? std::string("½-½") : set.played_result;
        tip               = set.played_reason.empty()
                                ? trf("Result set by hand. As played: {}", played)
                                : trf("Result set by hand. As played: {} ({})", played, TR(set.played_reason.c_str()));
    }
    if (!set.why.empty()) tip += "\n" + trf("Why: {}", set.why);
    return tip + "\n" + TR("Right click: change it, or take it back.");
}

}  // namespace

bool App::gamesFiltered() const {
    return games_filter_ != 0 || (!isMatch() && games_subject_ >= 0) || !opening_filter_.empty();
}

void App::rebuildGameFilter() {
    if (filtered_for_ == games_.size()) return;

    filtered_.clear();
    if (gamesFiltered()) {
        const auto opening = [&](const GameRecord& g) {
            if (opening_filter_.empty()) return true;
            if (g.opening == 0 || g.opening >= openings_.size()) return false;
            return openingMatches(openings_[g.opening].eco, openings_[g.opening].name, opening_filter_);
        };
        for (std::size_t i = 0; i < games_.size(); ++i) {
            if (matches(games_[i], games_filter_, games_subject_, isMatch(), eval_threshold_) && opening(games_[i])) {
                filtered_.push_back(static_cast<int>(i));
            }
        }
    }
    filtered_for_ = games_.size();
}

void App::selectGame(int index) {
    selected_game_ = index;
    replay_.reset();
    replay_error_.clear();
    replay_log_.clear();
    replay_pv_.clear();

    auto* obs = run_->observer();
    if (!obs || index < 0 || index >= static_cast<int>(games_.size())) return;

    if (const auto* t = run_->tournament(); t && games_[index].pv_warnings > 0) {
        for (const auto& r : t->results()) {
            if (r.pgn.offset == games_[index].fastchess_span.offset) replay_pv_ = r.pv_warnings;
        }
    }

    std::error_code ec;
    if (const auto log = tour::engineLogPath(run_->info().dir, games_[index].game_id);
        std::filesystem::is_regular_file(log, ec)) {
        replay_log_ = log;
    }

    const auto text = tour::readSpan(obs->fastchessPgn(), games_[index].fastchess_span);
    if (!text) {
        replay_error_ = trf("Cannot read the game back from {}", obs->fastchessPgn().string());
        return;
    }

    replay_ = parseReplay(*text);
    if (!replay_) {
        replay_error_ = TR("Unreadable PGN.");
        return;
    }
    // A result set by hand, as it counts: the PGN file has the game as played.
    if (const auto* r = games_[index].result_set ? gameResult(games_[index].game_id - 1) : nullptr; r && r->set) {
        replay_->result        = r->result;
        replay_->final_comment = tour::OPERATOR_REASON;
    }
    replay_ply_ = static_cast<int>(replay_->moves.size());
    // Filtered on what the evaluations tell: the board shows the moment it happens.
    if (const int ply = pliesOfInterest(games_[index], games_filter_); ply >= 0) {
        replay_ply_ = std::min(ply + 1, static_cast<int>(replay_->moves.size()));
    }
}

void App::exportGames(const std::string& path, bool all) {
    auto* obs = run_->observer();
    if (!obs) return;

    const bool chessbase = setup_.export_style == PgnStyle::ChessBase;
    const auto& file     = chessbase ? obs->chessbasePgn() : obs->fastchessPgn();

    std::vector<PgnSpan> spans;
    std::map<std::size_t, tour::GameResult> set;  // the games whose result was set by hand, by place in `spans`
    const auto add = [&](const GameRecord& g) {
        if (const auto* r = g.result_set ? gameResult(g.game_id - 1) : nullptr; r && r->set) set[spans.size()] = *r;
        spans.push_back(chessbase ? g.chessbase_span : g.fastchess_span);
    };
    if (all) {
        // In schedule order, not in the order they ended: the two games of an opening side by side.
        std::vector<std::size_t> order;
        if (!gamesFiltered()) {
            for (std::size_t i = 0; i < games_.size(); ++i) order.push_back(i);
        } else {
            for (int i : filtered_) order.push_back(static_cast<std::size_t>(i));
        }
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) { return games_[a].game_id < games_[b].game_id; });
        for (auto i : order) add(games_[i]);
    } else if (selected_game_ >= 0 && selected_game_ < static_cast<int>(games_.size())) {
        add(games_[selected_game_]);
    }

    // The games as they count: a result set by hand is in the export, not in the tournament's PGN files.
    const auto counted = [&](std::size_t i, std::string text) {
        const auto it = set.find(i);
        return it == set.end() ? text : tour::countedPgn(std::move(text), it->second);
    };
    const auto error = exportSpans(file, spans, path, {obs->fastchessPgn(), obs->chessbasePgn()}, counted);
    if (error.empty()) {
        Console::instance().add(spans.size() == 1 ? trf("1 game exported to {}", path)
                                                  : trn("{} game exported to {}", "{} games exported to {}",
                                                        spans.size(), spans.size(), path));
    } else {
        Console::instance().add(error, true);
    }
}

void App::drawGames() {
    if (!ui::beginPanel(TR("Games###games"))) {
        ui::endPanel();
        return;
    }

    auto* obs = run_->observer();
    if (!obs) {
        ImGui::TextDisabled("%s", TR("The finished games of the match will appear here."));
        ui::endPanel();
        return;
    }

    // Toolbar: filter, export, folder.
    ImGui::SetNextItemWidth(190.0f * ImGui::GetFontSize() / 16.0f);
    const bool match = isMatch();
    const char* filters[IM_ARRAYSIZE(FILTERS)];
    for (int i = 0; i < IM_ARRAYSIZE(FILTERS); ++i) filters[i] = TR(match ? FILTERS[i] : TOURNAMENT_FILTERS[i]);
    if (ImGui::Combo("##filter", &games_filter_, filters, IM_ARRAYSIZE(FILTERS))) filtered_for_ = ~std::size_t(0);
    if (games_filter_ >= FIRST_EVAL_FILTER && games_filter_ <= LAST_EVAL_FILTER) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f * ImGui::GetFontSize() / 16.0f);
        if (ImGui::SliderFloat("##eval_threshold", &eval_threshold_, 0.5f, 10.0f, TR("%.1f pawns"))) {
            filtered_for_ = ~std::size_t(0);
        }
        ui::help(
            TR("The engines disagree: their evaluations differ by this much for 3 moves each in a row.\nAn "
               "evaluation swings: an engine's evaluation changes by this much from one of its moves to the "
               "next.\nLost after a winning score: the loser thought it was this much better.\nA game opens at "
               "that moment."));
    }

    // A tournament: the games of one engine.
    if (!match) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(190.0f * ImGui::GetFontSize() / 16.0f);
        const auto& names   = obs->names();
        const char* current = games_subject_ >= 0 && games_subject_ < static_cast<int>(names.size())
                                  ? names[games_subject_].c_str()
                                  : TR("All engines");
        if (ImGui::BeginCombo("##subject", current)) {
            if (ImGui::Selectable(TR("All engines"), games_subject_ < 0)) {
                games_subject_ = -1;
                filtered_for_  = ~std::size_t(0);
            }
            for (int i = 0; i < static_cast<int>(names.size()); ++i) {
                if (ImGui::Selectable(names[i].c_str(), games_subject_ == i)) {
                    games_subject_ = i;
                    filtered_for_  = ~std::size_t(0);
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::SameLine();
    {
        char buffer[64];
        const auto n = std::min(opening_filter_.size(), sizeof(buffer) - 1);
        std::copy_n(opening_filter_.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(150.0f * ImGui::GetFontSize() / 16.0f);
        if (ImGui::InputTextWithHint("##opening_filter", TR("Opening"), buffer, sizeof(buffer))) {
            opening_filter_ = buffer;
            filtered_for_   = ~std::size_t(0);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("An ECO code or its beginning (B9), a range (B20-B99), or words of the name "
                                       "(sicilian najdorf)."));
        }
    }
    rebuildGameFilter();

    const bool all_games    = !gamesFiltered();
    const std::size_t shown = all_games ? games_.size() : filtered_.size();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", trn("{} game", "{} games", shown, shown).c_str());

    ImGui::SameLine();
    ImGui::TextUnformatted(TR(" Export:"));
    ImGui::SameLine();
    int style          = setup_.export_style == PgnStyle::ChessBase ? 1 : 0;
    bool style_changed = ImGui::RadioButton("ChessBase / Lichess", &style, 1);
    ImGui::SameLine();
    style_changed |= ImGui::RadioButton("cutechess", &style, 0);
    if (style_changed) {
        setup_.export_style = style == 1 ? PgnStyle::ChessBase : PgnStyle::Fastchess;
        saveSetupSoon();
    }
    ui::help(
        TR("ChessBase / Lichess: {[%eval 0.35,18] [%emt 0:00:01.234]}, eval from White's point of view.\ncutechess: "
           "{+0.35/18 1.234s}, score from the point of view of the side that moved."));

    ImGui::SameLine();
    ImGui::BeginDisabled(selected_game_ < 0);
    if (ImGui::Button(TR("Export game…"))) {
        dialogs_.saveFile(
            Dialogs::Purpose::ExportGames, {{"PGN", "pgn"}},
            (run_->info().dir / fmt::format("game-{}.pgn", selected_game_ >= 0 ? games_[selected_game_].game_id : 0))
                .string());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(shown == 0);
    if (ImGui::Button(all_games ? TR("Export all…") : TR("Export filtered…"))) {
        // Named after the tournament, next to its folder: in it, <name>.pgn is the tournament's own file.
        auto stem = tour::fileStemOf(run_->info().name);
        if (stem.empty()) stem = "export";
        dialogs_.saveFile(Dialogs::Purpose::ExportAll, {{"PGN", "pgn"}},
                          (run_->info().dir.parent_path() / (stem + ".pgn")).string());
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("A PGN file of the games shown, in the comment style chosen: the voided games left "
                                   "out, the results set by hand applied."));
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Folder"))) ui::openPath(run_->info().dir);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", run_->info().dir.string().c_str());
    // On the same line when it fits.
    const char* submit = TR("Export for a rating list…");
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(submit).x + 2 * ImGui::GetStyle().FramePadding.x) {
        ImGui::NewLine();
    }
    ImGui::BeginDisabled(!run_->tournament() || games_.empty());
    if (ImGui::Button(submit)) openSubmissionExport();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The games as rating list testers send them: the file named after you, the date and "
                                   "the conditions, the players as the lists write them, the rounds numbered again, "
                                   "the voided games and the duplicates left out."));
    }
    drawSubmissionExport();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool replay  = selected_game_ >= 0;
    const float list_w = replay ? avail.x * 0.48f : avail.x;

    ImGui::BeginChild("##game_list", ImVec2(list_w, 0));
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##games", 8, flags)) {
        float name_w = 0;
        for (const auto& n : obs->names()) name_w = std::max(name_w, ImGui::CalcTextSize(n.c_str()).x);
        const auto width = [](const char* sample) { return ImGui::CalcTextSize(sample).x; };

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, width("000000"));
        ImGui::TableSetupColumn(TR("White"), ImGuiTableColumnFlags_WidthFixed, std::max(name_w, width(TR("White"))));
        ImGui::TableSetupColumn(TR("Black"), ImGuiTableColumnFlags_WidthFixed, std::max(name_w, width(TR("Black"))));
        ImGui::TableSetupColumn(TR("Result"), ImGuiTableColumnFlags_WidthFixed, width(TR("Result")));
        ImGui::TableSetupColumn(TR("Opening"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("End"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Moves"), ImGuiTableColumnFlags_WidthFixed, width(TR("Moves")));
        ImGui::TableSetupColumn(TR("Duration"), ImGuiTableColumnFlags_WidthFixed, width("00 min 00 s"));
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                // Newest first.
                const int pos   = static_cast<int>(shown) - 1 - row;
                const int index = all_games ? pos : filtered_[pos];
                const auto& g   = games_[index];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                const auto id = fmt::format("{}##g{}", g.game_id, index);
                if (ImGui::Selectable(id.c_str(), selected_game_ == index, ImGuiSelectableFlags_SpanAllColumns)) {
                    selectGame(index);
                }
                // Once the tab is in front: bringing it there would close the menu.
                if (open_game_menu_ == g.game_id && run_->tournament() && !panel_request_) {
                    open_game_menu_     = 0;
                    result_menu_        = {};
                    result_menu_.game   = g.game_id - 1;
                    result_menu_.result = g.white_result == 0 ? "1-0" : "1/2-1/2";
                    ImGui::OpenPopup(ImGui::GetItemID());
                }
                if (run_->tournament() && ImGui::BeginPopupContextItem()) {
                    drawGameMenu(g.round_id - 1, static_cast<int>((g.game_id - 1) % 2));
                    ImGui::EndPopup();
                }

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(obs->name(g.white).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(obs->name(g.black).c_str());
                ImGui::TableNextColumn();
                // Coloured from A's point of view in a match, from the chosen engine's (or White's) otherwise.
                const int r = resultFor(g, games_subject_, match);
                ImGui::PushStyleColor(ImGuiCol_Text, r > 0 ? ui::GOOD : r < 0 ? ui::BAD : ui::MUTED);
                ImGui::TextUnformatted(g.result_set ? fmt::format("{} *", resultText(g)).c_str() : resultText(g));
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    const auto* set = g.result_set ? gameResult(g.game_id - 1) : nullptr;
                    if (set && set->set) {
                        ImGui::SetTooltip("%s", resultSetTip(*set->set).c_str());
                    } else {
                        ImGui::SetTooltip("%s", TR("Right click: play this game again, void it or set its result."));
                    }
                }
                ImGui::TableNextColumn();
                if (g.opening != 0 && g.opening < openings_.size()) {
                    const auto& o = openings_[g.opening];
                    ImGui::TextUnformatted(o.eco.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", o.name.c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s %s", o.eco.c_str(), o.name.c_str());
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(g.reason < reasons_.size() ? TR(reasons_[g.reason]) : "");
                ImGui::TableNextColumn();
                ImGui::Text("%d", (g.plies + 1) / 2);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(ui::formatDuration(g.duration_ms / 1000.0).c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (!replay) {
        ui::endPanel();
        return;
    }

    ImGui::SameLine();
    ImGui::BeginChild("##replay", ImVec2(0, 0));

    if (!replay_) {
        ImGui::TextColored(ImVec4(0.9f, 0.43f, 0.39f, 1), "%s", replay_error_.c_str());
        ImGui::EndChild();
        ui::endPanel();
        return;
    }

    const auto& game = *replay_;
    const int total  = static_cast<int>(game.moves.size());

    // Keyboard navigation while the window has focus, unless the keys go to a text field (the opening filter).
    replay_ply_ = std::clamp(replay_ply_, 0, total);
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        replay_ply_ = stepPly(navKeys(flip_replay_), replay_ply_, total);

    ImGui::AlignTextToFramePadding();  // the same height with or without the engine log button
    ImGui::Text("%s – %s  %s", game.header("White").c_str(), game.header("Black").c_str(), game.result.c_str());
    if (!game.final_comment.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR(game.final_comment));
    }
    if (const auto& g = games_[selected_game_]; g.result_set) {
        const auto* r = gameResult(g.game_id - 1);
        if (r && r->set && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", resultSetTip(*r->set).c_str());
        // Set from here a moment ago: taken back from here too.
        if (const auto* t = run_->tournament(); t && result_set_game_ == g.game_id - 1 && t->canUndo()) {
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Undo"))) undoTournamentEdit();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The game counts again as it was."));
        }
    }
    if (!replay_log_.empty()) {
        ImGui::SameLine();
        if (ui::iconButton("engine_log", ui::Icon::Log, TR("Engine log"))) ui::openPath(replay_log_);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s", trf("Every line exchanged with the engines during this game:\n{}", replay_log_.string()).c_str());
        }
    }
    if (const auto& g = games_[selected_game_]; g.opening != 0 && g.opening < openings_.size()) {
        ImGui::TextDisabled("%s %s", openings_[g.opening].eco.c_str(), openings_[g.opening].name.c_str());
    }
    if (!replay_pv_.empty()) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("! {} wrong PVs, on the moves in this colour", replay_pv_.size()).c_str());
    }

    chess::Board board;
    board.setFen(game.start_fen);
    for (int i = 0; i < replay_ply_; ++i) board.makeMove<true>(game.moves[i].move);

    float bar = 0.5f;
    for (int i = replay_ply_ - 1; i >= 0; --i) {
        const auto& m = game.moves[i];
        if (m.book || !m.score) continue;
        const int sign = m.mover == chess::Color::WHITE ? 1 : -1;
        bar            = m.score->type == fastchess::ScoreType::MATE
                             ? evalBarFraction(0, true, sign * (m.score->value >= 0 ? 1 : -1))
                             : evalBarFraction(sign * static_cast<int>(m.score->value), false, 0);
        break;
    }

    // The board, its info line and buttons (unless hidden), then the chart (its menu and a plot of at least 110 pixels)
    // in the height left: nothing under a scroll bar of its own beside the moves' one.
    const ImVec2 area      = ImGui::GetContentRegionAvail();
    const float nav_h      = setup_.nav_buttons ? boardNavHeight() : 0.0f;
    const float under      = ImGui::GetTextLineHeightWithSpacing() + nav_h;
    const float chart_min  = ImGui::GetFrameHeightWithSpacing() + 110.0f * ImGui::GetFontSize() / 16.0f;
    const float board_size = std::floor(std::max(
        120.0f,
        std::min({area.x * 0.62f, area.y * 0.62f, area.y - under - chart_min - ImGui::GetStyle().ItemSpacing.y})));

    BoardDecor decor;
    decor.flipped    = flip_replay_;
    decor.eval_white = bar;
    if (replay_ply_ > 0) decor.last_move = game.moves[replay_ply_ - 1].move;

    ImGui::BeginGroup();
    drawBoard(atlas_, board, board_size, decor);
    // A: the game on the Board page, at this move, analysed.
    if ((ImGui::IsItemHovered() || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) && boardKey()) {
        analyseReplay();
        ImGui::EndGroup();
        ImGui::EndChild();
        ui::endPanel();
        return;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", TR("A: analyse it on the Board page"));
    drawBoardInfo(atlas_, board, board_size, tbPieces());
    if (setup_.nav_buttons) {
        const auto click = drawBoardNav("##replay_nav", replay_ply_, total, flip_replay_, true, false, board_size);
        replay_ply_      = stepPly(click.step, replay_ply_, total);
        if (click.hide) {
            setup_.nav_buttons = false;
            saveSetupSoon();
        }
    }
    ImGui::EndGroup();

    // The moves beside the board; the wheel scrolls them.
    ImGui::SameLine();
    ImGui::BeginChild("##replay_moves", ImVec2(0, board_size + ImGui::GetTextLineHeightWithSpacing() + nav_h),
                      ImGuiChildFlags_Borders);
    {
        chess::Board start;
        start.setFen(game.start_fen);
        int number = start.fullMoveNumber();
        // The move list follows the current move when it moves out of sight (keys, buttons, a click in the chart).
        const bool follow = replay_ply_ != replay_followed_;
        if (follow && replay_ply_ == 0) ImGui::SetScrollY(0);

        for (int i = 0; i < total; ++i) {
            const auto& m = game.moves[i];
            if (m.mover == chess::Color::WHITE || i == 0) {
                if (i > 0) ImGui::NewLine();
                ImGui::TextDisabled(m.mover == chess::Color::WHITE ? "%d." : "%d...", number);
                ImGui::SameLine();
            }
            if (m.mover == chess::Color::BLACK) number++;

            const bool current = i + 1 == replay_ply_;
            const auto label   = fmt::format("{}##rm{}", m.san, i);
            // A move whose PV fastchess found wrong: in the warning colour, the warnings in its tooltip.
            std::string pv_problems;
            for (const auto& [ply, warning] : replay_pv_) {
                if (ply == static_cast<uint32_t>(i)) pv_problems += "\n" + warning;
            }
            if (m.book || !pv_problems.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, m.book ? ui::MUTED4 : ImGui::ColorConvertU32ToFloat4(ui::WARN));
            }
            if (ImGui::Selectable(label.c_str(), current, ImGuiSelectableFlags_None,
                                  ImGui::CalcTextSize(m.san.c_str()))) {
                replay_ply_ = i + 1;
            }
            if (m.book || !pv_problems.empty()) ImGui::PopStyleColor();
            if (current && (ImGui::IsWindowAppearing() || (follow && !ImGui::IsItemVisible()))) ImGui::SetScrollHereY();
            if (ImGui::IsItemHovered() && !m.book) {
                auto wdl = m.wdl ? fmt::format("  W/D/L {}/{}/{}", (*m.wdl)[0], (*m.wdl)[1], (*m.wdl)[2]) : "";
                if (m.decision_ms) wdl += trf("  decided at {}", ui::formatClock(*m.decision_ms));
                ImGui::SetTooltip("%s/%d  %s%s%s", ui::formatScore(m.score).c_str(), m.depth,
                                  m.time_ms >= 0 ? ui::formatClock(m.time_ms).c_str() : "", wdl.c_str(),
                                  pv_problems.c_str());
            }
            ImGui::SameLine();
        }
        replay_followed_ = replay_ply_;
    }
    ImGui::EndChild();
    if (ImGui::GetContentRegionAvail().y < chart_min) {  // a window too low for it, said when a line fits (#194)
        if (ImGui::GetContentRegionAvail().y >= ImGui::GetTextLineHeight())
            ImGui::TextDisabled("%s", TR("The charts are left out: the window is too low for them."));
        ImGui::EndChild();
        ui::endPanel();
        return;
    }

    // The charts read the PGN: a field its tournament did not write is told, with the option that writes it.
    std::optional<bool> on_known;
    const auto* tournament = run_->tournament();
    if (const uint8_t extra = chartExtraField(replay_graph_.chart); extra && !(game.fields & extra) && total > 0) {
        ImGui::TextDisabled("%s", trf("No {} in this game's PGN", pgnFieldOption(extra)).c_str());
        if (ImGui::IsItemHovered()) {
            // With the option on, the decision time is still only written when the engines' lines tell it.
            if (tournament) on_known = pgnFieldOn(tournament->spec().conditions.pgn, extra);
            ImGui::SetTooltip("%s", missingPgnFieldTip(extra, on_known).c_str());
        }
    }
    const bool chess960 = tournament && tournament->spec().conditions.variant == fastchess::VariantType::FRC;
    const auto graph    = buildGameGraph(game, chess960, replay_graph_.chart, {}, {});
    GraphDraw draw;
    draw.current     = replay_ply_;
    draw.tip         = [&game](int ply) { return graphMoveTip(game, ply); };
    draw.pgn_options = true;
    draw.missing     = [&](GraphChart chart) -> std::string {
        const uint8_t field = chartField(chart);
        if (field && !(game.fields & field)) {
            return trf(
                "Not in this game's PGN: its tournament was played with \"{}\" off in the PGN options of the "
                "setup.",
                pgnFieldOption(field));
        }
        return chart == GraphChart::TbHits ? TR("No tablebase hits in this game.")
                                           : TR("The engines did not report it in this game.");
    };
    const auto ev = drawGameGraph("##replay_graph", graph, replay_graph_, draw, ImGui::GetContentRegionAvail());
    if (ev.pgn_options) {
        page_           = Page::NewMatch;
        open_pgn_setup_ = true;
    }
    const int clicked = ev.go_to;
    if (clicked >= 0) replay_ply_ = std::clamp(clicked, 0, total);

    ImGui::EndChild();
    ui::endPanel();
}

}  // namespace fcg
