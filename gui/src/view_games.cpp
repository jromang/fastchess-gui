// The "Games" window: finished games, replay and export.

#include <cmath>

#include <SDL3/SDL_misc.h>

#include <imgui.h>

#include <fmt/include/fmt/core.h>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

const char* FILTERS[] = {"All",          "Wins of A", "Draws", "Losses of A", "Losses on time", "Crash / illegal move",
                         "Adjudications"};

bool matches(const GameRecord& g, int filter) {
    using T         = fastchess::MatchTermination;
    const auto term = static_cast<T>(g.termination);

    switch (filter) {
        case 1:
            return g.a_result > 0;
        case 2:
            return g.a_result == 0;
        case 3:
            return g.a_result < 0;
        case 4:
            return term == T::TIMEOUT;
        case 5:
            return term == T::DISCONNECT || term == T::STALL || term == T::ILLEGAL_MOVE;
        case 6:
            return term == T::ADJUDICATION;
        default:
            return true;
    }
}

const char* resultText(const GameRecord& g) {
    if (g.a_result == 0) return "½-½";
    return (g.a_result > 0) == g.a_white ? "1-0" : "0-1";
}

}  // namespace

void App::rebuildGameFilter() {
    if (filtered_for_ == games_.size()) return;

    filtered_.clear();
    if (games_filter_ != 0) {
        for (std::size_t i = 0; i < games_.size(); ++i) {
            if (matches(games_[i], games_filter_)) filtered_.push_back(static_cast<int>(i));
        }
    }
    filtered_for_ = games_.size();
}

void App::selectGame(int index) {
    selected_game_ = index;
    replay_.reset();
    replay_error_.clear();

    auto* obs = run_.observer();
    if (!obs || index < 0 || index >= static_cast<int>(games_.size())) return;

    const auto text = obs->fastchessPgn().read(games_[index].fastchess_span);
    if (!text) {
        replay_error_ = trf("Cannot read the game back from {}", obs->fastchessPgn().path().string());
        return;
    }

    replay_ = parseReplay(*text);
    if (!replay_) {
        replay_error_ = TR("Unreadable PGN.");
        return;
    }
    replay_ply_ = static_cast<int>(replay_->moves.size());
}

void App::exportGames(const std::string& path, bool all) {
    auto* obs = run_.observer();
    if (!obs) return;

    const bool chessbase = setup_.export_style == PgnStyle::ChessBase;
    const auto& file     = chessbase ? obs->chessbasePgn() : obs->fastchessPgn();

    std::vector<PgnSpan> spans;
    if (all) {
        if (games_filter_ == 0) {
            for (const auto& g : games_) spans.push_back(chessbase ? g.chessbase_span : g.fastchess_span);
        } else {
            for (int i : filtered_) spans.push_back(chessbase ? games_[i].chessbase_span : games_[i].fastchess_span);
        }
    } else if (selected_game_ >= 0 && selected_game_ < static_cast<int>(games_.size())) {
        const auto& g = games_[selected_game_];
        spans.push_back(chessbase ? g.chessbase_span : g.fastchess_span);
    }

    const auto error = exportSpans(file.path(), spans, path);
    if (error.empty()) {
        Console::instance().add(spans.size() == 1 ? trf("1 game exported to {}", path)
                                                  : trf("{} games exported to {}", spans.size(), path));
    } else {
        Console::instance().add(error, true);
    }
}

void App::drawGames() {
    if (!ImGui::Begin(TR("Games###games"))) {
        ImGui::End();
        return;
    }

    auto* obs = run_.observer();
    if (!obs) {
        ImGui::TextDisabled("%s", TR("The finished games of the match will appear here."));
        ImGui::End();
        return;
    }

    // Toolbar: filter, export, folder.
    ImGui::SetNextItemWidth(190.0f * ImGui::GetFontSize() / 16.0f);
    const char* filters[IM_ARRAYSIZE(FILTERS)];
    for (int i = 0; i < IM_ARRAYSIZE(FILTERS); ++i) filters[i] = TR(FILTERS[i]);
    if (ImGui::Combo("##filter", &games_filter_, filters, IM_ARRAYSIZE(FILTERS))) filtered_for_ = ~std::size_t(0);
    rebuildGameFilter();

    const std::size_t shown = games_filter_ == 0 ? games_.size() : filtered_.size();
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
    if (ImGui::Button(games_filter_ == 0 ? TR("Export all…") : TR("Export filtered…"))) {
        dialogs_.saveFile(Dialogs::Purpose::ExportAll, {{"PGN", "pgn"}}, (run_.info().dir / "export.pgn").string());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TR("Folder"))) {
        auto path = run_.info().dir.generic_string();  // forward slashes, also on Windows
        if (!path.empty() && path.front() != '/') path.insert(path.begin(), '/');
        SDL_OpenURL(("file://" + path).c_str());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", run_.info().dir.string().c_str());

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool replay  = selected_game_ >= 0;
    const float list_w = replay ? avail.x * 0.48f : avail.x;

    ImGui::BeginChild("##game_list", ImVec2(list_w, 0));
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##games", 7, flags)) {
        const float name_w =
            std::max(ImGui::CalcTextSize(obs->engineA().c_str()).x, ImGui::CalcTextSize(obs->engineB().c_str()).x);
        const auto width = [](const char* sample) { return ImGui::CalcTextSize(sample).x; };

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, width("000000"));
        ImGui::TableSetupColumn(TR("White"), ImGuiTableColumnFlags_WidthFixed, std::max(name_w, width(TR("White"))));
        ImGui::TableSetupColumn(TR("Black"), ImGuiTableColumnFlags_WidthFixed, std::max(name_w, width(TR("Black"))));
        ImGui::TableSetupColumn(TR("Result"), ImGuiTableColumnFlags_WidthFixed, width(TR("Result")));
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
                const int index = games_filter_ == 0 ? pos : filtered_[pos];
                const auto& g   = games_[index];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                const auto id = fmt::format("{}##g{}", g.game_id, index);
                if (ImGui::Selectable(id.c_str(), selected_game_ == index, ImGuiSelectableFlags_SpanAllColumns)) {
                    selectGame(index);
                }

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(g.a_white ? obs->engineA().c_str() : obs->engineB().c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(g.a_white ? obs->engineB().c_str() : obs->engineA().c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(g.a_result > 0   ? ImVec4(0.47f, 0.78f, 0.47f, 1)
                                   : g.a_result < 0 ? ImVec4(0.9f, 0.43f, 0.39f, 1)
                                                    : ui::MUTED4,
                                   "%s", resultText(g));
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
        ImGui::End();
        return;
    }

    ImGui::SameLine();
    ImGui::BeginChild("##replay", ImVec2(0, 0));

    if (!replay_) {
        ImGui::TextColored(ImVec4(0.9f, 0.43f, 0.39f, 1), "%s", replay_error_.c_str());
        ImGui::EndChild();
        ImGui::End();
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

    ImGui::Text("%s – %s  %s", game.header("White").c_str(), game.header("Black").c_str(), game.result.c_str());
    if (!game.final_comment.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR(game.final_comment));
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
    ImGui::BeginChild("##replay_moves", ImVec2(0, board_size + ImGui::GetFrameHeightWithSpacing()),
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
            if (m.book) ImGui::PushStyleColor(ImGuiCol_Text, ui::MUTED4);
            if (ImGui::Selectable(label.c_str(), current, ImGuiSelectableFlags_None,
                                  ImGui::CalcTextSize(m.san.c_str()))) {
                replay_ply_ = i + 1;
            }
            if (m.book) ImGui::PopStyleColor();
            if (current && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            if (ImGui::IsItemHovered() && !m.book) {
                ImGui::SetTooltip("%s/%d  %s", ui::formatScore(m.score).c_str(), m.depth,
                                  m.time_ms >= 0 ? ui::formatClock(m.time_ms).c_str() : "");
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
    const int clicked = plotEval("##replay_eval", evals, replay_ply_, -1);
    if (clicked >= 0) replay_ply_ = std::clamp(clicked, 0, total);

    ImGui::EndChild();
    ImGui::End();
}

}  // namespace fcg
