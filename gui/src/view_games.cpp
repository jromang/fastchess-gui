// The "Games" window: finished games, replay and export.

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include <tour/engine_log.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

const char* FILTERS[] = {"All",
                         "Wins of A",
                         "Draws",
                         "Losses of A",
                         "Losses on time",
                         "Crash / illegal move",
                         "Adjudications",
                         "The engines disagree",
                         "An evaluation swings",
                         "Lost after a winning score",
                         "Wrong PVs"};

// In a tournament, results are seen from the engine chosen in the filter, or from White when none is.
const char* TOURNAMENT_FILTERS[] = {"All",
                                    "Wins",
                                    "Draws",
                                    "Losses",
                                    "Losses on time",
                                    "Crash / illegal move",
                                    "Adjudications",
                                    "The engines disagree",
                                    "An evaluation swings",
                                    "Lost after a winning score",
                                    "Wrong PVs"};

// The filters on what the evaluations tell, which take a threshold in pawns.
constexpr int FIRST_EVAL_FILTER = 7;
constexpr int LAST_EVAL_FILTER  = 9;

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
        default:
            return true;
    }
}

const char* resultText(const GameRecord& g) {
    if (g.white_result == 0) return "½-½";
    return g.white_result > 0 ? "1-0" : "0-1";
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

    auto* obs = run_.observer();
    if (!obs || index < 0 || index >= static_cast<int>(games_.size())) return;

    if (const auto* t = run_.tournament(); t && games_[index].pv_warnings > 0) {
        for (const auto& r : t->results()) {
            if (r.pgn.offset == games_[index].fastchess_span.offset) replay_pv_ = r.pv_warnings;
        }
    }

    std::error_code ec;
    if (const auto log = tour::engineLogPath(run_.info().dir, games_[index].game_id);
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
    replay_ply_ = static_cast<int>(replay_->moves.size());
    // Filtered on what the evaluations tell: the board shows the moment it happens.
    if (const int ply = pliesOfInterest(games_[index], games_filter_); ply >= 0) {
        replay_ply_ = std::min(ply + 1, static_cast<int>(replay_->moves.size()));
    }
}

void App::exportGames(const std::string& path, bool all) {
    auto* obs = run_.observer();
    if (!obs) return;

    const bool chessbase = setup_.export_style == PgnStyle::ChessBase;
    const auto& file     = chessbase ? obs->chessbasePgn() : obs->fastchessPgn();

    std::vector<PgnSpan> spans;
    if (all) {
        if (!gamesFiltered()) {
            for (const auto& g : games_) spans.push_back(chessbase ? g.chessbase_span : g.fastchess_span);
        } else {
            for (int i : filtered_) spans.push_back(chessbase ? games_[i].chessbase_span : games_[i].fastchess_span);
        }
    } else if (selected_game_ >= 0 && selected_game_ < static_cast<int>(games_.size())) {
        const auto& g = games_[selected_game_];
        spans.push_back(chessbase ? g.chessbase_span : g.fastchess_span);
    }

    const auto error = exportSpans(file, spans, path);
    if (error.empty()) {
        Console::instance().add(spans.size() == 1 ? trf("1 game exported to {}", path)
                                                  : trf("{} games exported to {}", spans.size(), path));
    } else {
        Console::instance().add(error, true);
    }
}

void App::drawGames() {
    if (!ui::beginPanel(TR("Games###games"))) {
        ui::endPanel();
        return;
    }

    auto* obs = run_.observer();
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
    ImGui::TextDisabled("%s", (shown == 1 ? std::string(TR("1 game")) : trf("{} games", shown)).c_str());

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
            (run_.info().dir / fmt::format("game-{}.pgn", selected_game_ >= 0 ? games_[selected_game_].game_id : 0))
                .string());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(shown == 0);
    if (ImGui::Button(all_games ? TR("Export all…") : TR("Export filtered…"))) {
        dialogs_.saveFile(Dialogs::Purpose::ExportAll, {{"PGN", "pgn"}}, (run_.info().dir / "export.pgn").string());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TR("Folder"))) ui::openPath(run_.info().dir);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", run_.info().dir.string().c_str());

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

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(obs->name(g.white).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(obs->name(g.black).c_str());
                ImGui::TableNextColumn();
                // Coloured from A's point of view in a match, from the chosen engine's (or White's) otherwise.
                const int r = resultFor(g, games_subject_, match);
                ImGui::PushStyleColor(ImGuiCol_Text, r > 0 ? ui::GOOD : r < 0 ? ui::BAD : ui::MUTED);
                ImGui::TextUnformatted(resultText(g));
                ImGui::PopStyleColor();
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

    // Keyboard navigation while the window has focus.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) replay_ply_--;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) replay_ply_++;
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) replay_ply_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End)) replay_ply_ = total;
    }
    replay_ply_ = std::clamp(replay_ply_, 0, total);

    ImGui::AlignTextToFramePadding();  // the same height with or without the engine log button
    ImGui::Text("%s – %s  %s", game.header("White").c_str(), game.header("Black").c_str(), game.result.c_str());
    if (!game.final_comment.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR(game.final_comment));
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

    const ImVec2 area      = ImGui::GetContentRegionAvail();
    const float board_size = std::max(140.0f, std::min(area.x * 0.62f, area.y * 0.62f));

    BoardDecor decor;
    decor.flipped    = flip_replay_;
    decor.eval_white = bar;
    if (replay_ply_ > 0) decor.last_move = game.moves[replay_ply_ - 1].move;

    ImGui::BeginGroup();
    drawBoard(atlas_, board, board_size, decor);
    drawBoardInfo(atlas_, board, board_size, tbPieces());
    if (ImGui::Button("|<")) replay_ply_ = 0;
    ImGui::SameLine();
    if (ImGui::Button("<")) replay_ply_ = std::max(0, replay_ply_ - 1);
    ImGui::SameLine();
    if (ImGui::Button(">")) replay_ply_ = std::min(total, replay_ply_ + 1);
    ImGui::SameLine();
    if (ImGui::Button(">|")) replay_ply_ = total;
    ImGui::SameLine();
    ImGui::Checkbox(TR("Flip##replay"), &flip_replay_);
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginChild(
        "##replay_moves",
        ImVec2(0, board_size + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing()),
        ImGuiChildFlags_Borders);
    {
        chess::Board start;
        start.setFen(game.start_fen);
        int number = start.fullMoveNumber();

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
            if (current && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            if (ImGui::IsItemHovered() && !m.book) {
                auto wdl = m.wdl ? fmt::format("  W/D/L {}/{}/{}", (*m.wdl)[0], (*m.wdl)[1], (*m.wdl)[2]) : "";
                if (m.decision_ms) wdl += trf("  decided at {}", ui::formatClock(*m.decision_ms));
                ImGui::SetTooltip("%s/%d  %s%s%s", ui::formatScore(m.score).c_str(), m.depth,
                                  m.time_ms >= 0 ? ui::formatClock(m.time_ms).c_str() : "", wdl.c_str(),
                                  pv_problems.c_str());
            }
            ImGui::SameLine();
        }
    }
    ImGui::EndChild();

    std::vector<float> evals;
    evals.reserve(game.moves.size());
    float previous = 0.0f;
    for (const auto& m : game.moves) {
        const float v = m.book ? std::nanf("") : ui::whitePawns(m.score, m.mover);
        if (std::isfinite(v)) previous = v;
        evals.push_back(previous);
    }
    moveChartChoice("##replay_chart", replay_chart_);
    const float height = ImGui::GetContentRegionAvail().y;
    int clicked        = -1;
    if (replay_chart_ != MoveChart::Eval) {
        clicked = plotMoves("##replay_moves", replay_chart_, moveSeries(game.moves, replay_chart_),
                            {game.header("White"), game.header("Black")}, evals.size(), replay_ply_, height);
    } else {
        const auto wdl     = wdlSeries(game.moves);
        const float eval_h = std::floor(height * 0.62f);
        clicked            = plotEval("##replay_eval", evals, replay_ply_, wdl.empty() ? -1 : eval_h);
        if (!wdl.empty()) {
            const float wdl_h = std::max(30.0f, height - eval_h - ImGui::GetStyle().ItemSpacing.y - 1.0f);
            if (const int ply = plotWdl("##replay_wdl", wdl, evals.size(), replay_ply_, wdl_h); ply >= 0) clicked = ply;
        }
    }
    if (clicked >= 0) replay_ply_ = std::clamp(clicked, 0, total);

    ImGui::EndChild();
    ui::endPanel();
}

}  // namespace fcg
