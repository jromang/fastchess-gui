// The Board page: a game or a position, from a tournament, a PGN file or the clipboard, analysed by an engine of the
// library. Key A on every board opens its position here.

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <implot.h>

#include <fmt/include/fmt/format.h>

#include <tour/chess_text.hpp>

#include "app.hpp"
#include "board_text.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "run.hpp"
#include "storage.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// The best line's first move strong, the others lighter.
ImU32 arrowColour(int rank) {
    static constexpr int alpha[] = {215, 130, 95, 75};
    return IM_COL32(30, 110, 200, alpha[std::min(rank, 3)]);
}

// "+0.42", "#5", "#-3", from White's point of view, with ≥ or ≤ for a bound.
std::string whiteScoreText(const AnalysisLine& line, chess::Color stm) {
    if (!line.score) return "";
    auto text = ui::formatScore(whiteScore(*line.score, stm));
    // A lower bound of the side to move is a lower bound for White when White moves.
    const bool white = stm == chess::Color::WHITE;
    if (line.lowerbound) text = (white ? "≥" : "≤") + text;
    if (line.upperbound) text = (white ? "≤" : "≥") + text;
    return text;
}

// The eval bar's share for White, from a score of the side to move.
float barFraction(const fastchess::Score& score, chess::Color stm) {
    const auto white = whiteScore(score, stm);
    if (white.type == fastchess::ScoreType::MATE) return evalBarFraction(0, true, white.value >= 0 ? 1 : -1);
    return evalBarFraction(static_cast<int>(white.value), false, 0);
}

// White's evaluation in pawns, from a score of the side to move, within the chart's +-8; mated (mate 0) the lowest.
float reviewPawns(const fastchess::Score& score, chess::Color stm) {
    const float sign = stm == chess::Color::WHITE ? 1.0f : -1.0f;
    if (score.type == fastchess::ScoreType::MATE) return score.value > 0 ? sign * 8.0f : -sign * 8.0f;
    return std::clamp(sign * static_cast<float>(score.value) / 100.0f, -8.0f, 8.0f);
}

ImU32 judgementColour(Judgement judgement) {
    switch (judgement) {
        case Judgement::Inaccuracy:
            return IM_COL32(200, 160, 0, 255);
        case Judgement::Mistake:
            return ui::WARN;
        default:
            return ui::BAD;
    }
}

// The engines' own evaluation (when the game has it) and the analysis's, White's, in pawns, one value per ply.
// Returns the ply clicked, or -1.
int plotReview(const char* id, const std::vector<float>& engines, const std::vector<float>& analysis, int marker_ply,
               float height) {
    int clicked = -1;
    constexpr ImPlotFlags flags =
        ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText;
    if (ImPlot::BeginPlot(id, ImVec2(-1, height), flags)) {
        const double n = std::max<double>(20, static_cast<double>(std::max(engines.size(), analysis.size())));
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines,
                          ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, n, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -8.5, 8.5, ImPlotCond_Always);
        ImPlot::SetupAxisTicks(ImAxis_Y1, -8, 8, 5);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);
        const auto xs = [](std::size_t size) {
            std::vector<float> x(size);
            for (std::size_t i = 0; i < size; ++i) x[i] = static_cast<float>(i + 1);
            return x;
        };
        if (!engines.empty()) {
            const auto x = xs(engines.size());
            ImPlotSpec area;
            area.FillColor = ImVec4(0.85f, 0.85f, 0.85f, 0.25f);
            ImPlot::PlotShaded("##engines_area", x.data(), engines.data(), static_cast<int>(engines.size()), 0.0, area);
            ImPlotSpec line;
            line.LineColor  = ImVec4(0.43f, 0.66f, 0.87f, 1.0f);
            line.LineWeight = 1.5f;
            ImPlot::PlotLine(TR("The engines"), x.data(), engines.data(), static_cast<int>(engines.size()), line);
        }
        if (!analysis.empty()) {
            const auto x = xs(analysis.size());
            ImPlotSpec line;
            line.LineColor  = ImVec4(0.93f, 0.55f, 0.2f, 1.0f);
            line.LineWeight = 1.5f;
            ImPlot::PlotLine(TR("The analysis"), x.data(), analysis.data(), static_cast<int>(analysis.size()), line);
        }
        const double zero = 0;
        ImPlotSpec zero_spec;
        zero_spec.LineColor = ImVec4(0.5f, 0.5f, 0.5f, 0.6f);
        zero_spec.Flags     = ImPlotInfLinesFlags_Horizontal;
        ImPlot::PlotInfLines("##zero", &zero, 1, zero_spec);
        if (marker_ply > 0) {
            const double x = marker_ply;
            ImPlotSpec marker;
            marker.LineColor = ImVec4(0.95f, 0.75f, 0.3f, 0.8f);
            ImPlot::PlotInfLines("##marker", &x, 1, marker);
        }
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            clicked = static_cast<int>(std::lround(ImPlot::GetPlotMousePos().x));
        ImPlot::EndPlot();
    }
    return clicked;
}

}  // namespace

void App::loadBoardSettings() {
    if (board_.settings_loaded) return;
    board_.settings_loaded = true;
    if (const auto j = readJson<nlohmann::json>(storage_.root() / "board.json")) {
        try {
            board_.settings = j->get<BoardSettings>();
        } catch (...) {
        }
    }
}

void App::saveBoardSettings() { (void)writeJson(storage_.root() / "board.json", board_.settings); }

bool App::boardKey() {
    if (board_key_taken_) return false;
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || io.KeyCtrl || io.KeyAlt || io.KeySuper) return false;
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return false;
    if (!ImGui::IsKeyPressed(ImGuiKey_A, false)) return false;
    board_key_taken_ = true;
    return true;
}

std::string App::boardFen() { return boardAt(board_.game, board_.chess960, board_.ply).getFen(); }

int App::tournamentThreads() const {
    const auto* t = run_.tournament();
    if (!run_.running() || !t) return 0;
    // The two engines of a game take turns: the larger of their thread counts, for each game played.
    int per_game = 1;
    for (const auto& p : t->spec().participants) per_game = std::max(per_game, configThreads(p.engine));
    return gamesPlaying() * per_game;
}

void App::openOnBoard(ReplayGame game, int ply, bool chess960, std::string title, bool analyse) {
    if (page_ != Page::Board) {
        board_back_ = BoardBack{page_, page_ == Page::Tournament ? std::optional<Tab>(shown_tab_) : std::nullopt};
    }
    if (game.start_fen.empty()) game.start_fen = std::string(chess::constants::STARTPOS);
    board_.game     = std::move(game);
    board_.chess960 = chess960;
    board_.title    = std::move(title);
    board_.original.reset();
    board_.ply      = std::clamp(ply, 0, static_cast<int>(board_.game.moves.size()));
    board_.followed = -1;
    board_.selected.reset();
    board_.promotion.reset();
    board_.message.clear();
    board_.new_game = true;
    board_.watching = false;
    page_           = Page::Board;
    if (analyse) setBoardAnalysis(true);
}

void App::openPositionOnBoard(const std::string& fen, bool chess960, std::string title, bool analyse) {
    ReplayGame game;
    game.start_fen = fen;
    openOnBoard(std::move(game), 0, chess960, std::move(title), analyse);
}

void App::analyseLiveGame(int slot) {
    if (slot < 0 || slot >= static_cast<int>(slot_views_.size())) return;
    const auto& st = slot_views_[static_cast<std::size_t>(slot)].state;
    if (!st.has_game) return;
    const auto* t = run_.tournament();
    ReplayGame game;
    game.start_fen = st.start_fen;
    game.headers   = {{"White", st.white}, {"Black", st.black}};
    if (t) game.headers.emplace_back("Event", t->spec().name);
    game.result = st.result;
    for (const auto& m : st.moves) {
        ReplayMove r;
        r.san         = m.san;
        r.move        = m.move;
        r.mover       = m.mover;
        r.book        = m.book;
        r.score       = m.score;
        r.depth       = static_cast<int>(m.depth);
        r.time_ms     = m.time_ms;
        r.nps         = static_cast<int64_t>(m.nps);
        r.wdl         = m.wdl;
        r.nodes       = m.nodes;
        r.seldepth    = m.seldepth;
        r.tbhits      = m.tbhits;
        r.pv          = m.pv;
        r.decision_ms = m.decision_ms;
        game.moves.push_back(std::move(r));
    }
    const bool chess960 = t && t->spec().conditions.variant == fastchess::VariantType::FRC;
    const int ply       = static_cast<int>(game.moves.size());
    auto title          = fmt::format("{} – {}", st.white, st.black);
    if (t) title += " · " + t->spec().name;
    title += " · " + trf("game {}", st.game_id);
    openOnBoard(std::move(game), ply, chess960, std::move(title), true);
}

void App::analyseReplay() {
    if (!replay_ || selected_game_ < 0 || selected_game_ >= static_cast<int>(games_.size())) return;
    const auto* t       = run_.tournament();
    const bool chess960 = t && t->spec().conditions.variant == fastchess::VariantType::FRC;
    auto title          = fmt::format("{} – {}", replay_->header("White"), replay_->header("Black"));
    if (t) title += " · " + t->spec().name;
    title += " · " + trf("game {}", games_[static_cast<std::size_t>(selected_game_)].game_id);
    openOnBoard(*replay_, replay_ply_, chess960, std::move(title), true);
}

void App::setBoardAnalysis(bool on) {
    loadBoardSettings();
    if (!on) {
        board_.analysing = false;
        if (!board_.review_job) board_.analyser.halt();
        return;
    }
    if (board_.review_job) {
        board_.message       = TR("A game is being analysed move by move: wait for its end, or cancel it.");
        board_.message_error = true;
        return;
    }
    auto& s = board_.settings.analysis;
    // The engine chosen, or the first that works.
    const EngineEntry* engine = nullptr;
    for (const auto& e : engines_) {
        if (e.name == s.engine) engine = &e;
    }
    if (!engine) {
        for (const auto& e : engines_) {
            if (!e.inactive && engineWorks(e)) {
                engine = &e;
                break;
            }
        }
    }
    if (!engine && !engines_.empty()) engine = &engines_.front();
    if (!engine) {
        board_.message       = TR("No engine to analyse with: add one on the Engines page.");
        board_.message_error = true;
        return;
    }
    if (s.engine != engine->name) {
        s.engine = engine->name;
        saveBoardSettings();
    }
    if (!board_.analyser.active() || board_.analyser.engine() != engine->name) {
        board_.analyser.start(*engine, s);
        board_.new_game = true;
    }
    board_.analysing = true;
}

void App::pollBoard() {
    board_key_taken_ = false;
    board_.analyser.poll();
    pollBoardPgn();
    pollBoardGames();
    if (board_.analysing && board_.analyser.state() == Analyser::State::Failed) board_.analysing = false;
    // A game analysed move by move has the engine to itself, in the background.
    if (board_.review_job) {
        pollGameReview();
        return;
    }
    // The analysis follows the board while the page is shown, and waits meanwhile.
    if (board_.analysing && page_ == Page::Board && !board_.editor) {
        board_.analyser.analyse(analysisPosition(board_.game, board_.chess960, board_.ply), {}, board_.new_game);
        board_.new_game = false;
    } else if (board_.analyser.state() == Analyser::State::Searching) {
        board_.analyser.halt();
    }
}

// ---------------------------------------------------------------------------
// A game analysed move by move

const EngineEntry* App::analysisEngine() {
    loadBoardSettings();
    auto& s = board_.settings.analysis;
    for (const auto& e : engines_) {
        if (e.name == s.engine) return &e;
    }
    for (const auto& e : engines_) {
        if (!e.inactive && engineWorks(e)) {
            s.engine = e.name;
            saveBoardSettings();
            return &e;
        }
    }
    if (!engines_.empty()) {
        s.engine = engines_.front().name;
        saveBoardSettings();
        return &engines_.front();
    }
    return nullptr;
}

void App::startGameReview() {
    auto& b = board_;
    if (b.review_job || b.game.moves.empty() || (b.watching && b.match && b.match_state.running)) return;
    const auto* engine = analysisEngine();
    if (!engine) {
        b.message       = TR("No engine to analyse with: add one on the Engines page.");
        b.message_error = true;
        return;
    }
    // The analysis of the board gives way to it; the engine is the same, with one line.
    b.analysing = false;
    if (!b.analyser.active() || b.analyser.engine() != engine->name) b.analyser.start(*engine, b.settings.analysis);
    b.analyser.halt();
    auto one    = b.settings.analysis;
    one.multipv = 1;
    b.analyser.update(one);

    ReviewJob job;
    job.game     = b.game;
    job.chess960 = b.chess960;
    for (int i = 0; i <= static_cast<int>(b.game.moves.size()); ++i)
        job.positions.push_back(analysisPosition(b.game, b.chess960, i));
    job.limit              = b.settings.review_depth > 0 ? SearchLimit{0, b.settings.review_depth}
                                                         : SearchLimit{std::max(50, b.settings.review_ms), 0};
    job.review.engine      = engine->name;
    job.review.movetime_ms = job.limit.movetime_ms;
    job.review.depth       = job.limit.depth;
    b.review_job           = std::move(job);
    b.message.clear();
}

void App::cancelGameReview() {
    auto& b = board_;
    if (!b.review_job) return;
    b.review_job.reset();
    b.analyser.halt();
    b.analyser.update(b.settings.analysis);
}

void App::pollGameReview() {
    auto& b = board_;
    if (!b.review_job) return;
    auto& job = *b.review_job;
    auto& a   = b.analyser;
    if (a.state() == Analyser::State::Failed || a.state() == Analyser::State::Off) {
        b.message       = a.error().empty() ? std::string(TR("The analysis engine stopped.")) : a.error();
        b.message_error = true;
        b.review_job.reset();
        return;
    }
    auto& found = job.review.positions;
    // The positions the game ended in need no engine.
    while (!job.asked && found.size() < job.positions.size()) {
        const auto end = endScore(boardAt(job.game, job.chess960, static_cast<int>(found.size())));
        if (!end) break;
        found.push_back({end, "", 0});
    }
    if (found.size() < job.positions.size()) {
        const auto i = found.size();
        if (job.asked && a.finished() && a.position() == job.positions[i]) {
            PositionEval e;
            if (!a.lines().empty()) {
                e.score = a.lines().front().score;
                e.depth = static_cast<int>(a.lines().front().depth);
            }
            e.best = a.bestmove();
            found.push_back(std::move(e));
            job.asked = false;
            pollGameReview();  // the next one asked at once
            return;
        }
        if (!job.asked) {
            a.analyse(job.positions[i], job.limit, i == 0);
            job.asked = true;
        }
        return;
    }
    // Done: kept with the game, and shown if it is the game on the board.
    (void)saveReview(storage_.root() / "analyses", job.game, job.review);
    if (reviewKey(b.game) == reviewKey(job.game)) {
        b.review     = job.review;
        b.review_key = reviewKey(b.game);
        b.judged     = reviewMoves(b.game, b.chess960, *b.review);
    } else {
        b.review_key.clear();  // read again from its file when that game comes back
    }
    b.review_job.reset();
    a.update(b.settings.analysis);
}

void App::boardPlay(chess::Move move) {
    // Moves played over a game keep the game as it came, to go back to it.
    const auto ply = static_cast<std::size_t>(board_.ply);
    if (!board_.original && !board_.title.empty() && ply < board_.game.moves.size() &&
        board_.game.moves[ply].move != move) {
        board_.original = board_.game;
    }
    board_.ply = playOnGame(board_.game, board_.chess960, board_.ply, move);
    board_.selected.reset();
}

void App::boardClick(const chess::Board& board, chess::Square square) {
    if (board_.selected) {
        std::vector<chess::Move> moves;
        for (const auto& m : movesFrom(board, *board_.selected)) {
            if (shownTarget(m) == square) moves.push_back(m);
        }
        if (moves.size() == 1) {
            boardPlay(moves.front());
            return;
        }
        if (moves.size() > 1) {  // a promotion: the piece is chosen
            board_.promotion = moves.front();
            ImGui::OpenPopup("##promotion");
            return;
        }
    }
    const auto piece = board.at(square);
    if (piece != chess::Piece::NONE && piece.color() == board.sideToMove() && board_.selected != square &&
        !movesFrom(board, square).empty()) {
        board_.selected = square;
    } else {
        board_.selected.reset();
    }
}

void App::drawBoardPage() {
    loadBoardSettings();
    auto& b = board_;

    ui::pageTitle(TR("Board"), guideSection());
    // The tools, on the title's line: open, paste, set up a position, analyse.
    {
        const char* open    = TR("Open a PGN…");
        const char* paste   = TR("Paste");
        const char* edit    = TR("Edit the position");
        const char* play    = TR("Play a game…");
        const char* analyse = b.analysing ? TR("Stop the analysis") : TR("Analyse");
        float width         = 0;
        for (const char* label : {open, paste, edit, play, analyse}) width += ImGui::CalcTextSize(label).x + px(44);
        width += ImGui::CalcTextSize("Ctrl+V").x + ImGui::CalcTextSize("A").x + px(16);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(20), ImGui::GetContentRegionMax().x - width));
        ImGui::BeginDisabled(b.editor.has_value());
        if (ui::iconButton("board_open", ui::Icon::Folder, open)) {
            dialogs_.openFile(Dialogs::Purpose::BoardPgn, {{"PGN", "pgn"}}, storage_.runsDir().string());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("Any PGN file: its games listed and filtered by their tags, each replayed "
                                       "here. A file can also be dropped on the window."));
        }
        ImGui::SameLine();
        if (ui::iconButton("board_paste", ui::Icon::Paste, paste)) pasteOnBoard();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("A FEN, a PGN or a list of moves (e4 e5 Nf3, or e2e4 e7e5) from the "
                                       "clipboard."));
        }
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("Ctrl+V");
        ImGui::SameLine();
        if (ui::iconButton("board_edit", ui::Icon::Edit, edit)) openPositionEditor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("Set up a position: pieces, side to move, castling, en passant, Chess960."));
        }
        ImGui::SameLine();
        if (ui::iconButton("board_play", ui::Icon::Play, play)) b.play_open = !b.play_open;
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("A game, or two with the colours reversed, between two engines of the library "
                                       "from this position, with a clock for each."));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::iconButton("board_analyse", b.analysing ? ui::Icon::Stop : ui::Icon::Analyse, analyse))
            setBoardAnalysis(!b.analysing);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("A: an engine of the library analyses the position."));
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("A");
    }

    // Keys, unless a text field has them.
    const bool keys = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    const int total = static_cast<int>(b.game.moves.size());
    if (keys && !b.editor) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) b.ply = std::max(0, b.ply - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) b.ply = std::min(total, b.ply + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) b.ply = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End)) b.ply = total;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) b.selected.reset();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) pasteOnBoard();
    }
    if (keys && boardKey()) setBoardAnalysis(!b.analysing);

    if (b.reading) {
        const auto done = static_cast<double>(b.reading->done->load());
        ImGui::TextDisabled("%s", trf("Reading {}…", b.reading->file.filename().string()).c_str());
        ImGui::SameLine();
        ImGui::ProgressBar(b.reading->size ? static_cast<float>(done / static_cast<double>(b.reading->size)) : 0.0f,
                           ImVec2(px(200), 0));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel"))) *b.reading->cancel = true;
    }

    if (!b.title.empty()) ImGui::TextDisabled("%s", b.title.c_str());
    if (!b.message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, b.message_error ? ui::BAD : ui::MUTED);
        ImGui::TextWrapped("%s", b.message.c_str());
        ImGui::PopStyleColor();
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side   = std::clamp(avail.x * 0.42f, px(340), px(620));
    const float under  = ImGui::GetTextLineHeightWithSpacing() * 2 + ImGui::GetFrameHeightWithSpacing() * 2;
    const float size   = std::floor(
        std::max(px(200), std::min(avail.x - side - ImGui::GetStyle().ItemSpacing.x - px(20), avail.y - under)));

    if (b.editor) {
        drawPositionEditor(size);
        return;
    }
    ImGui::BeginGroup();
    drawBoardColumn(size);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginChild("##board_side", ImVec2(0, 0));
    if (b.play_open || (b.match && b.match_state.running)) drawBoardPlay();
    drawBoardAnalysis();
    drawBoardMoves();
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Games from a file or the clipboard

void App::openBoardPgn(const std::filesystem::path& file) {
    auto& b = board_;
    if (b.reading) *b.reading->cancel = true;  // its result is dropped
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) {
        b.message       = trf("Cannot read {}", file.string());
        b.message_error = true;
        page_           = Page::Board;
        return;
    }
    if (page_ != Page::Board) {
        board_back_ = BoardBack{page_, page_ == Page::Tournament ? std::optional<Tab>(shown_tab_) : std::nullopt};
    }
    page_ = Page::Board;
    PgnReading r;
    r.file   = file;
    r.size   = size;
    r.cancel = std::make_shared<std::atomic<bool>>(false);
    r.done   = std::make_shared<std::atomic<uint64_t>>(0);
    // Read in the background: a file of a rating list holds millions of games.
    r.result  = std::async(std::launch::async, [file, cancel = r.cancel, done = r.done] {
        std::ifstream in(file, std::ios::binary);
        return tour::indexPgn(in, cancel.get(), done.get());
    });
    b.reading = std::move(r);
}

void App::pollBoardPgn() {
    auto& b = board_;
    if (!b.reading || b.reading->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto index        = b.reading->result.get();
    const auto file   = b.reading->file;
    const bool cancel = *b.reading->cancel;
    b.reading.reset();
    if (cancel) return;
    if (index.games.empty()) {
        b.message       = trf("No game in {}.", file.filename().string());
        b.message_error = true;
        return;
    }
    PgnSource source;
    source.name  = file.filename().string();
    source.file  = file;
    source.index = std::make_shared<const tour::PgnIndex>(std::move(index));
    showPgnSource(std::move(source));
    if (review_when_read_) {
        review_when_read_ = false;
        board_.open_tab   = 0;
        startGameReview();
    }
}

void App::showPgnSource(PgnSource source) {
    auto& b         = board_;
    const auto n    = source.index->size();
    b.pgn           = std::move(source);
    b.message       = trn("{} game in {}", "{} games in {}", n, n, b.pgn->name);
    b.message_error = false;
    loadPgnGame(0);
    if (n > 1) b.open_tab = 1;  // the list of the games
}

void App::loadPgnGame(int index) {
    auto& b = board_;
    if (!b.pgn || index < 0 || index >= static_cast<int>(b.pgn->index->size())) return;
    b.pgn->selected = index;
    const auto text = pgnSourceGame(*b.pgn, static_cast<std::size_t>(index));
    std::string error;
    auto game = text ? readPgnGame(*text, error) : std::nullopt;
    if (!game) {
        b.message       = trf("Game {}: {}", index + 1, text ? error : std::string(TR("it cannot be read.")));
        b.message_error = true;
        return;
    }
    const bool chess960 = gameIsChess960(*game);
    auto title          = fmt::format("{} – {}", game->header("White"), game->header("Black"));
    if (const auto event = game->header("Event"); !event.empty() && event != "?") title += " · " + event;
    if (const auto round = game->header("Round"); !round.empty() && round != "?" && round != "-")
        title += " · " + trf("round {}", round);
    const auto back = board_back_;
    openOnBoard(std::move(*game), 0, chess960, std::move(title), false);
    board_back_ = back;
    b.message.clear();
}

void App::readOnBoard(const std::string& text, const std::string& name) {
    auto& b         = board_;
    const auto read = readBoardText(text);
    switch (read.kind) {
        case BoardText::Kind::Nothing:
            if (page_ != Page::Board) openPositionOnBoard(boardFen(), b.chess960, b.title, false);
            b.message       = read.error;
            b.message_error = true;
            return;
        case BoardText::Kind::Position:
            openPositionOnBoard(read.fen, read.chess960, name, false);
            b.pgn.reset();
            return;
        case BoardText::Kind::Games: {
            auto pasted = std::make_shared<const std::string>(read.pgn);
            std::istringstream in(*pasted);
            auto index = tour::indexPgn(in);
            if (index.games.empty()) {
                b.message       = TR("No position, game or move found in the text.");
                b.message_error = true;
                return;
            }
            if (page_ != Page::Board) {
                board_back_ =
                    BoardBack{page_, page_ == Page::Tournament ? std::optional<Tab>(shown_tab_) : std::nullopt};
            }
            page_ = Page::Board;
            PgnSource source;
            source.name  = name;
            source.text  = std::move(pasted);
            source.index = std::make_shared<const tour::PgnIndex>(std::move(index));
            if (source.index->size() == 1) {
                b.pgn = std::move(source);
                loadPgnGame(0);
                b.pgn.reset();  // one game: nothing to list
            } else {
                showPgnSource(std::move(source));
            }
            return;
        }
    }
}

void App::pasteOnBoard() {
    char* clip             = SDL_GetClipboardText();
    const std::string text = clip ? clip : "";
    SDL_free(clip);
    readOnBoard(text, TR("From the clipboard"));
}

void App::dropFile(const std::string& path) {
    const std::filesystem::path file(path);
    auto ext = file.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".pgn") {
        openBoardPgn(file);
        return;
    }
    if (ext == ".fen" || ext == ".epd" || ext == ".txt") {
        std::ifstream in(file, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        readOnBoard(text.substr(0, 1 << 20), file.filename().string());
        return;
    }
    Console::instance().add(trf("{}: only PGN files, FEN and EPD positions can be dropped here.", file.string()), true);
}

void App::drawPgnGames() {
    auto& src  = *board_.pgn;
    auto& list = *src.index;
    {
        char buffer[256];
        const auto n = std::min(src.filter.size(), sizeof(buffer) - 1);
        std::copy_n(src.filter.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(px(260));
        if (ImGui::InputTextWithHint("##pgn_filter", TR("Filter: players, event, opening, date…"), buffer,
                                     sizeof(buffer))) {
            src.filter       = buffer;
            src.filter_dirty = true;
        }
    }
    if (src.filter_dirty) {
        src.shown.clear();
        for (uint32_t i = 0; i < list.games.size(); ++i) {
            if (tour::pgnGameMatches(list, list.games[i], src.filter)) src.shown.push_back(i);
        }
        src.filter_dirty = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", trf("{} of {}", src.shown.size(), list.games.size()).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", src.file.empty() ? src.name.c_str() : src.file.string().c_str());

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##pgn_games", 6, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000000").x);
        ImGui::TableSetupColumn(TR("White"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Black"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Result"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("1/2-1/2").x);
        ImGui::TableSetupColumn(TR("Date"), ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0000.00.00").x);
        ImGui::TableSetupColumn(TR("Event"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(src.shown.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto i  = src.shown[static_cast<std::size_t>(row)];
                const auto& g = list.games[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(fmt::format("{}##pg{}", i + 1, i).c_str(), src.selected == static_cast<int>(i),
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    loadPgnGame(static_cast<int>(i));
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(list.str(g.white).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(list.str(g.black).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(list.str(g.result).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", list.str(g.date).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", list.str(g.event).c_str());
            }
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------
// The position editor

void App::openPositionEditor() {
    auto& b = board_;
    PositionEditor e;
    e.chess960 = b.chess960;
    (void)editorLoad(e, boardFen());
    b.editor = e;
    b.selected.reset();
}

void App::drawPositionEditor(float size) {
    auto& b = board_;
    auto& e = *b.editor;

    // The board: a click puts the piece chosen (again: takes it away), a right click takes it away.
    BoardDecor decor;
    decor.flipped    = b.flip;
    decor.eval_white = -1.0f;
    ImGui::BeginGroup();
    const ImVec2 top = ImGui::GetCursorScreenPos();
    drawBoard(atlas_, e.squares, size, decor);
    bool changed = false;
    if (ImGui::IsItemHovered()) {
        const bool left  = ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                   right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (left || right) {
            if (const auto square = boardSquareAt(ImGui::GetMousePos(), top, size, decor)) {
                auto& at = e.squares[static_cast<std::size_t>(square->index())];
                at       = right || e.erase || at == e.brush ? chess::Piece(chess::Piece::NONE) : e.brush;
                changed  = true;
            }
        }
    }
    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginChild("##editor_side", ImVec2(0, 0));
    ui::beginCard("editor_tools");
    ImGui::TextUnformatted(TR("Edit the position"));
    ImGui::TextDisabled("%s", TR("Click: put the piece chosen (again: take it away). Right click: take it away."));
    // The pieces, white then black, and the eraser.
    const float button = px(40);
    for (const auto colour : {chess::Color::WHITE, chess::Color::BLACK}) {
        for (const auto type : {chess::PieceType::KING, chess::PieceType::QUEEN, chess::PieceType::ROOK,
                                chess::PieceType::BISHOP, chess::PieceType::KNIGHT, chess::PieceType::PAWN}) {
            const auto piece = chess::Piece(type, colour);
            ImVec2 uv0, uv1;
            atlas_.uv(piece, uv0, uv1);
            const bool chosen = !e.erase && e.brush == piece;
            ImGui::PushID(static_cast<int>(piece));
            if (chosen) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
            if (ImGui::ImageButton("##brush", atlas_.texture(), ImVec2(button, button), uv0, uv1)) {
                e.brush = piece;
                e.erase = false;
            }
            if (chosen) ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::SameLine();
        }
        if (colour == chess::Color::WHITE) ImGui::NewLine();
    }
    if (e.erase) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    if (ImGui::Button(TR("Eraser"), ImVec2(0, button + ImGui::GetStyle().FramePadding.y * 2))) e.erase = true;
    if (e.erase) ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("To move"));
    ImGui::SameLine();
    if (ImGui::RadioButton(TR("White"), e.stm == chess::Color::WHITE)) e.stm = chess::Color::WHITE, changed = true;
    ImGui::SameLine();
    if (ImGui::RadioButton(TR("Black"), e.stm == chess::Color::BLACK)) e.stm = chess::Color::BLACK, changed = true;
    ImGui::SameLine(0, px(24));
    ImGui::TextUnformatted(TR("Move"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(60));
    if (ui::inputInt("##editor_move", &e.fullmove)) {
        e.fullmove = std::clamp(e.fullmove, 1, 9999);
        changed    = true;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Castling"));
    static constexpr const char* rights[] = {"O-O##wk", "O-O-O##wq", "O-O##bk", "O-O-O##bq"};
    for (int i = 0; i < 4; ++i) {
        ImGui::SameLine();
        if (i == 2) {
            ImGui::TextDisabled("%s", TR("Black"));
            ImGui::SameLine();
        } else if (i == 0) {
            ImGui::TextDisabled("%s", TR("White"));
            ImGui::SameLine();
        }
        const bool possible = editorCanCastle(e, i);
        ImGui::BeginDisabled(!possible);
        bool on = e.castle[i] && possible;
        if (ImGui::Checkbox(rights[i], &on)) {
            e.castle[i] = on;
            changed     = true;
        }
        ImGui::EndDisabled();
        if (!possible && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("The king and the rook are not where it needs them."));
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("En passant"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(80));
    {
        const std::string current =
            e.ep_file < 0 ? std::string(TR("none")) : std::string(1, static_cast<char>('a' + e.ep_file));
        if (ImGui::BeginCombo("##editor_ep", current.c_str())) {
            if (ImGui::Selectable(TR("none"), e.ep_file < 0)) e.ep_file = -1, changed = true;
            for (int f = 0; f < 8; ++f) {
                // Only where a pawn just moved two squares: the FEN's en passant field says it.
                auto test    = e;
                test.ep_file = f;
                std::istringstream fields(editorFen(test));
                std::string placement, side, castling, ep;
                fields >> placement >> side >> castling >> ep;
                if (ep == "-") continue;
                if (ImGui::Selectable(std::string(1, static_cast<char>('a' + f)).c_str(), e.ep_file == f))
                    e.ep_file = f, changed = true;
            }
            ImGui::EndCombo();
        }
    }
    ImGui::SameLine(0, px(24));
    if (ImGui::Checkbox("Chess960", &e.chess960)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("Castling as in Chess960: with the rooks where they stand."));

    // The position as a FEN: typed, it sets the board.
    if (changed) e.fen_text = editorFen(e);
    {
        char buffer[256];
        const auto n = std::min(e.fen_text.size(), sizeof(buffer) - 1);
        std::copy_n(e.fen_text.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##editor_fen", buffer, sizeof(buffer))) {
            e.fen_text  = buffer;
            auto loaded = e;
            if (editorLoad(loaded, e.fen_text)) {
                loaded.fen_text = e.fen_text;  // as typed, until it is left
                loaded.brush    = e.brush;
                loaded.erase    = e.erase;
                e               = loaded;
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) e.fen_text = editorFen(e);
    }
    const auto fen     = editorFen(e);
    const auto problem = positionProblem(fen, e.chess960);
    if (!problem.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::TextWrapped("%s", problem.c_str());
        ImGui::PopStyleColor();
    }

    if (ImGui::Button(TR("Initial position"))) {
        (void)editorLoad(e, std::string(chess::constants::STARTPOS));
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Empty board"))) {
        (void)editorLoad(e, "8/8/8/8/8/8/8/8 w - - 0 1");
    }
    ImGui::SameLine();
    ImGui::Checkbox(TR("Flip##editor"), &b.flip);
    ImGui::SameLine();
    if (ImGui::Button(TR("Copy FEN"))) ImGui::SetClipboardText(fen.c_str());

    ImGui::Spacing();
    ImGui::BeginDisabled(!problem.empty());
    if (ImGui::Button(TR("Done"))) {
        const bool chess960 = e.chess960;
        b.editor.reset();
        openPositionOnBoard(fen, chess960, TR("Position set up"), false);
        b.pgn.reset();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"))) b.editor.reset();
    ui::endCard();
    ImGui::EndChild();
}

void App::drawBoardColumn(float size) {
    auto& b          = board_;
    const auto board = boardAt(b.game, b.chess960, b.ply);
    const auto stm   = board.sideToMove();

    BoardDecor decor;
    decor.flipped = b.flip;
    if (b.ply > 0) decor.last_move = b.game.moves[static_cast<std::size_t>(b.ply - 1)].move;
    // The bar: the analysis of this position, else the score of the move that led to it.
    decor.eval_white  = 0.5f;
    const auto& lines = b.analyser.lines();
    const bool fresh  = b.analyser.active() && b.analyser.position() == analysisPosition(b.game, b.chess960, b.ply);
    if (fresh && !lines.empty() && lines.front().score) {
        decor.eval_white = barFraction(*lines.front().score, stm);
    } else {
        for (int i = b.ply - 1; i >= 0; --i) {
            const auto& m = b.game.moves[static_cast<std::size_t>(i)];
            if (m.book || !m.score) continue;
            decor.eval_white = barFraction(*m.score, m.mover);
            break;
        }
    }
    // The first move of each line, the best on top.
    if (fresh && b.settings.arrows) {
        for (int i = static_cast<int>(lines.size()) - 1; i >= 0; --i) {
            const auto moves = legalPrefix(board, lines[static_cast<std::size_t>(i)].pv, 1);
            if (!moves.empty()) decor.arrows.push_back({moves.front(), arrowColour(i)});
        }
    }
    decor.selected = b.selected;
    if (b.selected) {
        for (const auto& m : movesFrom(board, *b.selected)) decor.targets.push_back(shownTarget(m));
    }

    const ImVec2 top = ImGui::GetCursorScreenPos();
    drawBoard(atlas_, board, size, decor);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        if (const auto square = boardSquareAt(ImGui::GetMousePos(), top, size, decor)) boardClick(board, *square);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) b.selected.reset();

    // The piece a pawn becomes.
    if (ImGui::BeginPopup("##promotion")) {
        static constexpr chess::PieceType types[] = {chess::PieceType::QUEEN, chess::PieceType::ROOK,
                                                     chess::PieceType::BISHOP, chess::PieceType::KNIGHT};
        for (const auto type : types) {
            ImVec2 uv0, uv1;
            atlas_.uv(chess::Piece(type, stm), uv0, uv1);
            ImGui::PushID(static_cast<int>(type));
            if (ImGui::ImageButton("##piece", atlas_.texture(), ImVec2(px(44), px(44)), uv0, uv1) && b.promotion) {
                for (const auto& m : movesFrom(board, b.promotion->from())) {
                    if (shownTarget(m) == shownTarget(*b.promotion) && m.typeOf() == chess::Move::PROMOTION &&
                        m.promotionType() == type) {
                        boardPlay(m);
                    }
                }
                b.promotion.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::EndPopup();
    } else {
        b.promotion.reset();
    }

    drawBoardInfo(atlas_, board, size, 0);

    // Through the moves, and the position as text.
    const int total = static_cast<int>(b.game.moves.size());
    if (ImGui::Button("|<")) b.ply = 0;
    ImGui::SameLine();
    if (ImGui::Button("<")) b.ply = std::max(0, b.ply - 1);
    ImGui::SameLine();
    if (ImGui::Button(">")) b.ply = std::min(total, b.ply + 1);
    ImGui::SameLine();
    if (ImGui::Button(">|")) b.ply = total;
    ImGui::SameLine();
    ImGui::Checkbox(TR("Flip##board"), &b.flip);
    ImGui::SameLine();
    if (ImGui::Button(TR("Copy FEN"))) ImGui::SetClipboardText(board.getFen().c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", board.getFen().c_str());
    if (b.original) {
        ImGui::SameLine();
        if (ImGui::Button(TR("Back to the game"))) {
            b.game = *b.original;
            b.original.reset();
            b.ply = std::min(b.ply, static_cast<int>(b.game.moves.size()));
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("The game as it was, without the moves played over it."));
    }
    if (board_back_) {
        const char* back = board_back_->page == Page::Tournament ? TR("Back to the tournament")
                           : board_back_->page == Page::Books    ? TR("Back to the books")
                                                                 : TR("Back");
        ui::sameLineOrWrap(back);
        if (ui::iconButton("board_back", ui::Icon::Back, back)) {
            page_ = board_back_->page;
            if (board_back_->tab) tab_request_ = board_back_->tab;
            board_back_.reset();
        }
    }
}

void App::drawBoardAnalysis() {
    auto& b          = board_;
    auto& s          = b.settings.analysis;
    auto& a          = b.analyser;
    bool changed     = false;
    const auto board = boardAt(b.game, b.chess960, b.ply);

    ui::beginCard("board_analysis");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Analysis"));
    ImGui::SameLine();
    // The engine, among those of the library.
    ImGui::SetNextItemWidth(px(170));
    if (ImGui::BeginCombo("##analysis_engine", s.engine.empty() ? TR("Choose an engine") : s.engine.c_str(),
                          ImGuiComboFlags_HeightLargest)) {
        std::vector<std::string> names, more;
        for (const auto& e : engines_) {
            if (e.inactive && e.name != s.engine) continue;
            names.push_back(e.name);
            more.push_back(engineSearchText(e));
        }
        if (const int i = ui::searchComboItems(names, s.engine, &more); i >= 0 && names[i] != s.engine) {
            s.engine = names[i];
            saveBoardSettings();
            // Another engine: the one running ends, the new one starts if the analysis runs.
            a.stop();
            if (b.analysing) setBoardAnalysis(true);
        }
        ImGui::EndCombo();
    }
    // Threads, hash and lines: the analysis's own.
    const auto number = [&](const char* id, const char* label, int& value, int lo, int hi, const char* tip) {
        ui::sameLineOrWrap(ImGui::CalcTextSize(label).x + px(56));
        ImGui::TextDisabled("%s", label);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine(0, px(4));
        ImGui::SetNextItemWidth(px(52));
        if (ui::inputInt(id, &value)) {
            value   = std::clamp(value, lo, hi);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };
    number("##analysis_threads", TR("Threads"), s.threads, 1, std::max(1, topology_.logical),
           TR("The threads of the analysis: they count in the core budget, with a tournament's games."));
    number("##analysis_hash", TR("Hash"), s.hash, 0, 1 << 20, TR("MB; 0: the engine's own."));
    number("##analysis_lines", TR("Lines"), s.multipv, 1, 10, TR("How many best moves are shown (MultiPV)."));
    ui::sameLineOrWrap(TR("this analysis only"));
    ImGui::TextDisabled("%s", TR("this analysis only"));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("These settings are the analysis's: they change neither the engine in the library "
                                   "nor a tournament."));
    }
    if (changed) {
        saveBoardSettings();
        a.update(s);
    }

    // How the search goes.
    const auto stm = board.sideToMove();
    if (a.state() == Analyser::State::Failed) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", a.error().c_str());
        ImGui::PopStyleColor();
    } else if (b.review_job) {
        ImGui::TextDisabled("%s", trf("{} analyses the game move by move.", a.engine()).c_str());
    } else if (!a.active()) {
        ImGui::TextDisabled("%s", TR("Press A, or Analyse, for the engine's best moves here."));
    } else if (a.state() == Analyser::State::Starting) {
        ImGui::TextDisabled("%s", trf("Starting {}…", a.engine()).c_str());
    } else {
        const auto& lines = a.lines();
        std::string head  = a.idName().empty() ? a.engine() : a.idName();
        head += " · " + trn("{} thread", "{} threads", a.hasOption("Threads") ? s.threads : 1,
                            a.hasOption("Threads") ? s.threads : 1);
        if (a.hasOption("MultiPV")) head += fmt::format(" · MultiPV {}", s.multipv);
        if (!lines.empty()) {
            const auto& best = lines.front();
            head += fmt::format(" · d{}/{} · {} n · {} n/s · {}", best.depth, best.seldepth,
                                ui::formatCount(best.nodes), ui::formatCount(best.nps), ui::formatClock(best.time_ms));
        }
        if (!b.analysing) head += " · " + std::string(TR("stopped"));
        ImGui::TextDisabled("%s", head.c_str());

        const bool fresh = a.position() == analysisPosition(b.game, b.chess960, b.ply);
        if (!fresh) {
            ImGui::TextDisabled("%s", b.analysing ? "…" : TR("Not this position."));
        } else if (!lines.empty() && lines.front().wdl) {
            // The best line's chances, White's.
            auto wdl = *lines.front().wdl;
            if (stm == chess::Color::BLACK) std::swap(wdl[0], wdl[2]);
            wdlLine(wdl);
        }
        if (fresh) {
            for (std::size_t i = 0; i < lines.size(); ++i) {
                const auto& line = lines[i];
                if (line.pv.empty()) continue;
                const auto score = whiteScoreText(line, stm);
                const auto pv    = pvToSan(board, line.pv, 14);
                ImGui::PushID(static_cast<int>(i));
                // A click plays its first move.
                const auto label = fmt::format("{:>6}  d{}  {}", score, line.depth, pv);
                if (ImGui::Selectable(label.c_str(), false)) {
                    const auto first = legalPrefix(board, line.pv, 1);
                    if (!first.empty()) boardPlay(first.front());
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", TR("Click: play its first move. Scores are White's."));
                }
                ImGui::PopID();
            }
        }
    }

    // Its place among the cores.
    const int threads = boardThreads();
    ImGui::TextDisabled("%s", trf("Counts in the core budget · {} of {} threads", threads, topology_.logical).c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The threads the analysis and the games of this page use now, out of the "
                                   "machine's."));
    }
    if (const int busy = tournamentThreads(); busy > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::TextWrapped(
            "%s", trf("A tournament is running: the analysis competes with its games ({} threads).", busy).c_str());
        ImGui::PopStyleColor();
    }
    ui::endCard();
}

void App::drawBoardMoves() {
    auto& b         = board_;
    const int total = static_cast<int>(b.game.moves.size());

    ui::beginCard("board_game", ImVec2(0, std::max(px(160), ImGui::GetContentRegionAvail().y)));
    // The games of the file, when it has several, in a tab beside the moves.
    if (b.pgn && b.pgn->index->size() > 1) {
        if (ImGui::BeginTabBar("##board_game_tabs")) {
            const auto tab = [&](const char* label, int index) {
                const ImGuiTabItemFlags flags = b.open_tab == index ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(label, nullptr, flags)) {
                    b.game_tab = index;
                    ImGui::EndTabItem();
                }
            };
            tab(TR("Moves###board_moves_tab"), 0);
            tab(fmt::format("{} ({})###board_games_tab", b.pgn->name, b.pgn->index->size()).c_str(), 1);
            b.open_tab = -1;
            ImGui::EndTabBar();
        }
        if (b.game_tab == 1) {
            drawPgnGames();
            ui::endCard();
            return;
        }
    }
    {
        const auto white = b.game.header("White"), black = b.game.header("Black");
        ImGui::AlignTextToFramePadding();
        if (!white.empty() || !black.empty()) {
            ImGui::TextUnformatted(
                fmt::format("{} – {}", white.empty() ? "?" : white, black.empty() ? "?" : black).c_str());
            if (!b.game.result.empty() && b.game.result != "*") {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", b.game.result.c_str());
            }
        } else {
            ImGui::TextUnformatted(total > 0 ? TR("Moves") : TR("Position"));
        }
        // The game as PGN, to the clipboard.
        const char* copy = TR("Copy PGN");
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(8),
                                 ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(copy).x - px(12)));
        if (ImGui::SmallButton(copy)) ImGui::SetClipboardText(gamePgn(b.game, b.chess960).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The game, with its tags and moves, as PGN."));
    }

    // Its analysis move by move: kept with the game, read again when another game comes.
    if (const auto key = reviewKey(b.game); key != b.review_key) {
        b.review_key = key;
        b.review     = total > 0 ? loadReview(storage_.root() / "analyses", b.game) : std::nullopt;
        b.judged     = b.review ? reviewMoves(b.game, b.chess960, *b.review) : std::vector<MoveReview>{};
    }
    drawGameReview();

    // The evaluation the engines gave of their moves, when the game has them, and the analysis's.
    std::vector<float> evals, analysis;
    bool scored = false;
    {
        float previous = 0.0f;
        for (const auto& m : b.game.moves) {
            const float v = m.book ? std::nanf("") : ui::whitePawns(m.score, m.mover);
            if (std::isfinite(v)) {
                previous = v;
                scored   = true;
            }
            evals.push_back(previous);
        }
        if (!scored) evals.clear();
        if (b.review) {
            for (std::size_t i = 0; i < b.game.moves.size() && i + 1 < b.review->positions.size(); ++i) {
                const auto& score = b.review->positions[i + 1].score;
                analysis.push_back(score ? reviewPawns(*score, ~b.game.moves[i].mover)
                                         : (analysis.empty() ? 0.0f : analysis.back()));
            }
        }
    }
    const bool curve   = scored || !analysis.empty();
    const float chart  = curve ? std::max(px(70), ImGui::GetContentRegionAvail().y * 0.3f) : 0.0f;
    const float height = std::max(px(60), ImGui::GetContentRegionAvail().y - chart);

    ImGui::BeginChild("##board_moves", ImVec2(0, height), ImGuiChildFlags_Borders);
    if (total == 0) {
        ImGui::TextDisabled("%s", TR("Play a move on the board, or open a game."));
    } else {
        int number        = boardAt(b.game, b.chess960, 0).fullMoveNumber();
        const bool follow = b.ply != b.followed;
        if (follow && b.ply == 0) ImGui::SetScrollY(0);
        for (int i = 0; i < total; ++i) {
            const auto& m = b.game.moves[static_cast<std::size_t>(i)];
            if (m.mover == chess::Color::WHITE || i == 0) {
                if (i > 0) ImGui::NewLine();
                ImGui::TextDisabled(m.mover == chess::Color::WHITE ? "%d." : "%d...", number);
                ImGui::SameLine();
            }
            if (m.mover == chess::Color::BLACK) number++;
            const bool current = i + 1 == b.ply;
            if (m.book) ImGui::PushStyleColor(ImGuiCol_Text, ui::MUTED4);
            const auto label = fmt::format("{}##bm{}", m.san, i);
            if (ImGui::Selectable(label.c_str(), current, ImGuiSelectableFlags_None,
                                  ImGui::CalcTextSize(m.san.c_str())))
                b.ply = i + 1;
            if (m.book) ImGui::PopStyleColor();
            if (current && (ImGui::IsWindowAppearing() || (follow && !ImGui::IsItemVisible()))) ImGui::SetScrollHereY();
            const auto judged =
                static_cast<std::size_t>(i) < b.judged.size() ? b.judged[static_cast<std::size_t>(i)] : MoveReview{};
            if (ImGui::IsItemHovered()) {
                std::string tip;
                if (m.score && !m.book) {
                    tip = fmt::format("{}/{}  {}", ui::formatScore(m.score), m.depth,
                                      m.time_ms >= 0 ? ui::formatClock(m.time_ms) : "");
                }
                if (judged.judgement != Judgement::None) tip += (tip.empty() ? "" : "\n") + judgementTip(i, judged);
                if (!tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
            }
            // Its judgement, after it.
            if (judged.judgement != Judgement::None) {
                ImGui::SameLine(0, 0);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(judgementColour(judged.judgement)), "%s",
                                   judgementMark(judged.judgement));
            }
            ImGui::SameLine();
        }
        if (!b.game.result.empty() && b.game.result != "*") ImGui::TextDisabled("%s", b.game.result.c_str());
    }
    b.followed = b.ply;
    ImGui::EndChild();

    if (curve) {
        if (const int clicked = plotReview("##board_eval", evals, analysis, b.ply, ImGui::GetContentRegionAvail().y);
            clicked >= 0)
            b.ply = std::clamp(clicked, 0, total);
    }
    ui::endCard();
}

std::string App::judgementTip(int ply, const MoveReview& judged) const {
    const auto& b    = board_;
    const char* what = judged.judgement == Judgement::Blunder   ? TR("Blunder")
                       : judged.judgement == Judgement::Mistake ? TR("Mistake")
                                                                : TR("Inaccuracy");
    auto tip         = trf("{}: {:.0f} % of the winning chances lost.", what, judged.loss);
    // What the engine would have played.
    if (b.review && static_cast<std::size_t>(ply) < b.review->positions.size()) {
        const auto& before = b.review->positions[static_cast<std::size_t>(ply)];
        const auto board   = boardAt(b.game, b.chess960, ply);
        const auto best    = legalPrefix(board, {before.best}, 1);
        if (!best.empty() && before.score) {
            tip += "\n" + trf("Best: {} ({})", chess::uci::moveToSan(board, best.front()),
                              ui::formatScore(whiteScore(*before.score, board.sideToMove())));
        }
    }
    return tip;
}

void App::drawGameReview() {
    auto& b         = board_;
    const int total = static_cast<int>(b.game.moves.size());
    if (total == 0 && !b.review_job) return;

    if (b.review_job) {
        // Its progress, wherever the board is.
        const auto done = b.review_job->review.positions.size(), all = b.review_job->positions.size();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", trf("Analysing the game: {} of {} positions", done, all).c_str());
        ImGui::SameLine();
        ImGui::ProgressBar(static_cast<float>(done) / static_cast<float>(std::max<std::size_t>(1, all)),
                           ImVec2(px(120), 0), "");
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel##review"))) cancelGameReview();
        return;
    }

    // Start it: the time (or the depth) of each position.
    struct Choice {
        int ms, depth;
    };
    static constexpr Choice choices[] = {{200, 0},   {500, 0}, {1000, 0}, {2000, 0}, {5000, 0},
                                         {10000, 0}, {0, 12},  {0, 16},   {0, 20},   {0, 24}};
    const auto label                  = [](const Choice& c) {
        return c.depth > 0 ? trf("depth {}", c.depth) : trf("{:g} s per move", c.ms / 1000.0);
    };
    auto& s         = b.settings;
    const bool live = b.watching && b.match && b.match_state.running;  // not before its end
    ImGui::BeginDisabled(live);
    if (ImGui::SmallButton(b.review ? TR("Analyse the game again") : TR("Analyse the game"))) startGameReview();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", live ? TR("Once the game is over.")
                                     : TR("The analysis engine goes through every position of the game, in the "
                                          "background: a score per move, and its inaccuracies, mistakes and blunders "
                                          "marked."));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(130));
    const Choice current{s.review_depth > 0 ? 0 : s.review_ms, s.review_depth};
    if (ImGui::BeginCombo("##review_limit", label(current).c_str())) {
        for (const auto& c : choices) {
            if (ImGui::Selectable(label(c).c_str(), c.ms == current.ms && c.depth == current.depth)) {
                s.review_ms    = c.ms > 0 ? c.ms : s.review_ms;
                s.review_depth = c.depth;
                saveBoardSettings();
            }
        }
        ImGui::EndCombo();
    }

    // What it found, by side.
    if (b.review && b.review->complete(b.game.moves.size())) {
        const auto counts = judgementCounts(b.game, b.judged);
        const auto side   = [&](int colour) {
            const auto& c = counts[static_cast<std::size_t>(colour)];
            std::string parts;
            const auto add = [&](std::string part) { parts += (parts.empty() ? "" : ", ") + part; };
            if (c[2]) add(trn("{} blunder", "{} blunders", c[2], c[2]));
            if (c[1]) add(trn("{} mistake", "{} mistakes", c[1], c[1]));
            if (c[0]) add(trn("{} inaccuracy", "{} inaccuracies", c[0], c[0]));
            if (parts.empty()) parts = TR("no mistake");
            auto name = b.game.header(colour == 0 ? "White" : "Black");
            if (name.empty() || name == "?") name = colour == 0 ? TR("White") : TR("Black");
            return fmt::format("{}: {}", name, parts);
        };
        ImGui::TextWrapped("%s", trf("Analysis: {} · {}", side(0), side(1)).c_str());
        if (ImGui::IsItemHovered()) {
            const auto how = b.review->depth > 0 ? trf("depth {}", b.review->depth)
                                                 : trf("{:g} s per move", b.review->movetime_ms / 1000.0);
            ImGui::SetTooltip("%s", trf("By {}, {}. Losses of 10, 20 and 30 % of the winning chances are an "
                                        "inaccuracy (?!), a mistake (?) and a blunder (??).",
                                        b.review->engine, how)
                                        .c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// Games played from the board's position

int App::engineThreads(const std::string& name) const {
    for (const auto& e : engines_) {
        if (e.name == name) return e.threads();
    }
    return 1;
}

void App::startBoardGames() {
    auto& b                  = board_;
    auto& s                  = b.settings;
    const EngineEntry *first = nullptr, *second = nullptr;
    for (const auto& e : engines_) {
        if (e.name == s.game_first) first = &e;
        if (e.name == s.game_second) second = &e;
    }
    if (!first || !second) {
        b.message       = TR("Choose the two engines of the game.");
        b.message_error = true;
        return;
    }
    BoardMatchSetup setup;
    setup.first        = first->name;
    setup.second       = second->name;
    setup.first_clock  = s.first_clock;
    setup.second_clock = s.same_clock ? s.first_clock : s.second_clock;
    setup.pair         = s.game_pair;
    setup.fen          = boardFen();
    setup.chess960     = b.chess960;
    // The analysis would compete with the engines of the game.
    if (b.analysing) setBoardAnalysis(false);
    std::error_code ec;
    const auto dir = storage_.root() / "games";
    std::filesystem::create_directories(dir, ec);
    const auto pgn = dir / (makeRunDirName(first->name, second->name) + ".pgn");
    if (!b.match) b.match = std::make_unique<BoardMatch>();
    std::string error;
    if (!b.match->start(*first, *second, setup, pgn, error)) {
        b.message       = error;
        b.message_error = true;
        return;
    }
    b.match_version = 0;
    b.match_state   = {};
    b.watching      = true;
    b.play_anyway   = false;
    b.chess960      = setup.chess960;
    b.title         = fmt::format("{} – {} · {}", first->name, second->name, clockText(setup.first_clock));
    b.message.clear();
}

void App::pollBoardGames() {
    auto& b = board_;
    if (!b.match) return;
    const bool changed = b.match->state(b.match_state, b.match_version);
    if (!b.match_state.running) b.match->reap();
    if (!changed || !b.watching) return;
    // The board follows the game being played, unless the user went back in it.
    const auto& st  = b.match_state;
    const bool last = b.ply >= static_cast<int>(b.game.moves.size());
    b.game          = st.current;
    b.original.reset();
    if (last) b.ply = static_cast<int>(b.game.moves.size());
    b.ply = std::min(b.ply, static_cast<int>(b.game.moves.size()));
    b.selected.reset();
    if (st.games > 1) {
        b.title = trf("{} – {} · game {} of {}", st.current.header("White"), st.current.header("Black"), st.game + 1,
                      st.games);
    }
}

void App::drawBoardPlay() {
    auto& b             = board_;
    auto& s             = b.settings;
    const bool running  = b.match && b.match_state.running;
    const bool finished = b.match && !running && !b.match_state.finished.empty();
    bool changed        = false;

    ui::beginCard("board_play");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Play a game"));
    if (!running) {
        const char* close = TR("Close");
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(8),
                                 ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(close).x - px(12)));
        if (ImGui::SmallButton(close)) {
            b.play_open = false;
            if (finished) b.match.reset();
        }
    }

    if (running || finished) {
        const auto& st = b.match_state;
        if (running) {
            const auto& g = st.current;
            ImGui::TextUnformatted(
                trf("Game {} of {}: {} – {}", st.game + 1, st.games, g.header("White"), g.header("Black")).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Stop##games"))) b.match->stop();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The game being played ends without a result."));
            // The clocks, the one running counting down.
            const auto now = std::chrono::steady_clock::now();
            for (int side = 0; side < 2; ++side) {
                const auto colour = side == 0 ? chess::Color::WHITE : chess::Color::BLACK;
                auto ms           = st.clock_ms[static_cast<std::size_t>(side)];
                const bool thinks = st.thinking == colour;
                if (thinks) ms -= std::chrono::duration_cast<std::chrono::milliseconds>(now - st.think_start).count();
                if (side == 1) ImGui::SameLine(0, px(24));
                const auto text = fmt::format("{} {}", g.header(side == 0 ? "White" : "Black"),
                                              ui::formatClock(std::max<int64_t>(0, ms)));
                if (thinks)
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::ACCENT), "%s", text.c_str());
                else
                    ImGui::TextDisabled("%s", text.c_str());
            }
            // What the side to move searches.
            if (st.thinking && !st.info.empty()) {
                const auto info  = fastchess::engine::UciEngine::parseInfo(st.info);
                const auto board = boardAt(st.current, b.chess960, static_cast<int>(st.current.moves.size()));
                std::string score;
                if (info.score) score = ui::formatScore(whiteScore(*info.score, board.sideToMove()));
                ImGui::TextDisabled("%s  d%lld  %s", score.c_str(), static_cast<long long>(info.depth.value_or(0)),
                                    pvToSan(board, info.pv, 8).c_str());
            }
        }
        for (std::size_t i = 0; i < st.finished.size(); ++i) {
            const auto& g = st.finished[i];
            ImGui::TextUnformatted(
                trf("Game {}: {} – {}  {}", i + 1, g.header("White"), g.header("Black"), g.result).c_str());
            if (!g.final_comment.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", g.final_comment.c_str());
            }
        }
        if (!st.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
            ImGui::TextWrapped("%s", st.error.c_str());
            ImGui::PopStyleColor();
        }
        if (!st.finished.empty() || finished) {
            ImGui::TextDisabled("%s", trf("Saved in {}", st.pgn.filename().string()).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", st.pgn.string().c_str());
        }
        if (finished) {
            if (ImGui::Button(TR("Open the games"))) {
                const auto file = st.pgn;
                b.match.reset();
                b.play_open = false;
                openBoardPgn(file);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("Their PGN file, its games listed beside the moves."));
            ImGui::SameLine();
            if (ImGui::Button(TR("Folder##games"))) ui::openPath(st.pgn.parent_path());
            ImGui::SameLine();
            if (ImGui::Button(TR("New game"))) b.match.reset();
        }
        ui::endCard();
        return;
    }

    // The engines: the first has White in the first game.
    const auto engineCombo = [&](const char* id, std::string& name) {
        ImGui::SetNextItemWidth(px(150));
        if (ImGui::BeginCombo(id, name.empty() ? TR("Choose an engine") : name.c_str(),
                              ImGuiComboFlags_HeightLargest)) {
            std::vector<std::string> names, more;
            for (const auto& e : engines_) {
                if (e.inactive && e.name != name) continue;
                names.push_back(e.name);
                more.push_back(engineSearchText(e));
            }
            if (const int i = ui::searchComboItems(names, name, &more); i >= 0) {
                name    = names[static_cast<std::size_t>(i)];
                changed = true;
            }
            ImGui::EndCombo();
        }
    };
    if (s.game_first.empty() && !engines_.empty()) s.game_first = engines_.front().name;
    if (s.game_second.empty() && engines_.size() > 1) s.game_second = engines_[1].name;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("White"));
    ImGui::SameLine();
    engineCombo("##game_first", s.game_first);
    ImGui::SameLine();
    if (ImGui::SmallButton("<->")) {
        std::swap(s.game_first, s.game_second);
        std::swap(s.first_clock, s.second_clock);
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Swap the engines and their clocks."));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("Black"));
    ImGui::SameLine();
    engineCombo("##game_second", s.game_second);

    // The clocks: minutes and an increment in seconds.
    const auto clockFields = [&](const char* id, const char* label, GameClock& clock) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine();
        double minutes = clock.base_s / 60.0;
        ImGui::SetNextItemWidth(px(60));
        if (ui::inputDouble(fmt::format("##{}_base", id).c_str(), &minutes, "%g")) {
            clock.base_s = std::clamp(minutes, 0.01, 600.0) * 60.0;
            changed      = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Minutes for the game."));
        ImGui::SameLine(0, px(4));
        ImGui::TextUnformatted("+");
        ImGui::SameLine(0, px(4));
        ImGui::SetNextItemWidth(px(52));
        if (ui::inputDouble(fmt::format("##{}_inc", id).c_str(), &clock.inc_s, "%g")) {
            clock.inc_s = std::clamp(clock.inc_s, 0.0, 600.0);
            changed     = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Seconds added after each move."));
    };
    clockFields("game_first", s.same_clock ? TR("Clock") : TR("White's clock"), s.first_clock);
    ImGui::SameLine(0, px(16));
    if (ImGui::Checkbox(TR("The same for both"), &s.same_clock)) changed = true;
    if (!s.same_clock) clockFields("game_second", TR("Black's clock"), s.second_clock);
    if (ImGui::Checkbox(TR("A second game, the colours reversed"), &s.game_pair)) changed = true;
    if (changed) saveBoardSettings();

    // From the position on the board.
    const auto fen = boardFen();
    ImGui::TextDisabled("%s", TR("From the position on the board"));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fen.c_str());

    // Its place among the cores: refused, or played anyway, when the machine is full.
    const int need  = std::max(engineThreads(s.game_first), engineThreads(s.game_second));
    const int busy  = tournamentThreads() + boardThreads();
    const bool full = busy + need > topology_.logical;
    ImGui::TextDisabled("%s", trf("Counts in the core budget · {} of {} threads", need, topology_.logical).c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The larger thread count of the two engines: they take turns. Their games are "
                                   "played apart from any tournament."));
    }
    if (full) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::TextWrapped("%s", trf("The machine is full: the tournament's games and the analysis use {} of its {} "
                                     "threads, and this game needs {}. It would slow them down, and its own engines.",
                                     busy, topology_.logical, need)
                                     .c_str());
        ImGui::PopStyleColor();
    }
    const bool ready = !s.game_first.empty() && !s.game_second.empty() && positionProblem(fen, b.chess960).empty();
    ImGui::BeginDisabled(!ready);
    if (!full || b.play_anyway) {
        if (ui::iconButton("board_play_start", ui::Icon::Play, s.game_pair ? TR("Play the two games") : TR("Play")))
            startBoardGames();
    } else if (ImGui::Button(TR("Play anyway"))) {
        b.play_anyway = true;
        startBoardGames();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Shown here as they are played, and saved as PGN in games/ in the profile."));
    }
    ui::endCard();
}

}  // namespace fcg
