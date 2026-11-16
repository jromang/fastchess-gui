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
#include "notation_view.hpp"
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

ImU32 judgementBadgeColour(Judgement judgement) {
    switch (judgement) {
        case Judgement::Inaccuracy:
            return IM_COL32(212, 169, 21, 255);
        case Judgement::Mistake:
            return IM_COL32(226, 116, 31, 255);
        default:
            return IM_COL32(200, 45, 40, 255);
    }
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

// The colour of each analysis on the graph and the board: the judging one in the text's, the others apart.
ImU32 analysisColour(std::size_t index) {
    if (index == 0) return ImGui::GetColorU32(ImGuiCol_Text);
    const bool light          = ui::currentTheme() == ui::Theme::Light;
    static const ImU32 lit[]  = {IM_COL32(138, 79, 214, 255), IM_COL32(0, 140, 128, 255), IM_COL32(205, 45, 125, 255),
                                 IM_COL32(120, 120, 0, 255), IM_COL32(150, 90, 40, 255)};
    static const ImU32 dark[] = {IM_COL32(180, 140, 255, 255), IM_COL32(80, 205, 190, 255),
                                 IM_COL32(245, 120, 185, 255), IM_COL32(205, 205, 90, 255),
                                 IM_COL32(215, 160, 110, 255)};
    return (light ? lit : dark)[(index - 1) % 5];
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
    int threads = 0;
    for (const auto& r : runs_) {
        const auto* t = r.run->tournament();
        if (!r.run->running() || !t) continue;
        // A tournament in a process of its own tells what its games in flight take.
        if (r.run->remote()) {
            threads += r.run->inFlight().threads;
            continue;
        }
        // The two engines of a game take turns: the larger of their thread counts, for each game played.
        int per_game = 1;
        for (const auto& p : t->spec().participants) per_game = std::max(per_game, configThreads(p.engine));
        threads += r.run->runningGames() * per_game;
    }
    return threads;
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
    const auto* t = run_->tournament();
    auto game     = replayOfSlot(st);
    if (t) game.headers.emplace_back("Event", t->spec().name);
    const bool chess960 = t && t->spec().conditions.variant == fastchess::VariantType::FRC;
    // At the position the board shows: an earlier one when it was stepped back.
    const int ply = slot_views_[static_cast<std::size_t>(slot)].nav.shown(static_cast<int>(game.moves.size()));
    auto title    = fmt::format("{} – {}", st.white, st.black);
    if (t) title += " · " + t->spec().name;
    title += " · " + trf("game {}", st.game_id);
    openOnBoard(std::move(game), ply, chess960, std::move(title), true);
}

void App::analyseReplay() {
    if (!replay_ || selected_game_ < 0 || selected_game_ >= static_cast<int>(games_.size())) return;
    const auto* t       = run_->tournament();
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
        board_.analyser.halt();
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

std::optional<AnalysisPosition> App::threatPosition() const {
    const auto& b = board_;
    auto board    = boardAt(b.game, b.chess960, b.ply);
    if (board.inCheck() || board.isGameOver().second != chess::GameResult::NONE) return std::nullopt;
    board.makeNullMove();
    return AnalysisPosition{board.getFen(), {}, b.chess960};
}

void App::setBoardThreat(bool on) {
    auto& b = board_;
    if (!on) {
        b.threat_on = false;
        b.threat.halt();
        return;
    }
    const auto* engine = analysisEngine();
    if (!engine) {
        b.message       = TR("No engine to analyse with: add one on the Engines page.");
        b.message_error = true;
        return;
    }
    auto one    = b.settings.analysis;
    one.multipv = 1;
    if (!b.threat.active() || b.threat.engine() != engine->name) b.threat.start(*engine, one);
    b.threat_on = true;
}

void App::pollBoard() {
    board_key_taken_ = false;
    board_.analyser.poll();
    board_.threat.poll();
    if (board_.threat_on && board_.threat.state() == Analyser::State::Failed) board_.threat_on = false;
    if (board_.threat_on && page_ == Page::Board && !board_.editor) {
        if (const auto position = threatPosition())
            board_.threat.analyse(*position);
        else
            board_.threat.halt();
    } else if (board_.threat.state() == Analyser::State::Searching) {
        board_.threat.halt();
    }
    pollBoardPgn();
    pollBoardGames();
    if (board_.analysing && board_.analyser.state() == Analyser::State::Failed) board_.analysing = false;
    // A game analysed in passes runs on its own thread; the board's analysis (A) pauses it.
    auto& b = board_;
    pollGameReview();
    // The analysis follows the board while the page is shown, and waits meanwhile.
    if (b.analysing && page_ == Page::Board && !b.editor) {
        const auto position = analysisPosition(b.game, b.chess960, b.ply);
        if (!(b.analyser.position() == position)) b.review_offered = 0;
        b.analyser.analyse(position, {}, b.new_game);
        b.new_game = false;
        // A position of the game analysed in passes, searched for longer than its pass did: its value offered.
        BoardPage::ReviewTask* same = nullptr;
        for (auto& t : b.review_tasks)
            if (t.engine == b.analyser.engine()) same = &t;
        if (same && b.review_game_key == b.review_key && b.analyser.position() == position &&
            !b.analyser.lines().empty() && b.analyser.lines().front().score && !b.analyser.lines().front().pv.empty()) {
            const auto& line = b.analyser.lines().front();
            const auto ply   = static_cast<std::size_t>(b.ply);
            // Once a second at most, and when it went further than what the analysis has.
            if (ply < same->state.live.positions.size() &&
                static_cast<int64_t>(line.time_ms) > same->state.live.positions[ply].time_ms &&
                static_cast<int64_t>(line.time_ms) >= b.review_offered + 1000) {
                b.review_offered = static_cast<int64_t>(line.time_ms);
                PositionEval value;
                value.score   = line.score;
                value.best    = line.pv.front();
                value.depth   = static_cast<int>(line.depth);
                value.time_ms = static_cast<int64_t>(line.time_ms);
                value.wdl     = line.wdl;
                for (const auto& m : line.pv) value.pv += (value.pv.empty() ? "" : " ") + m;
                same->runner->offer(b.ply, std::move(value));
            }
        }
    } else if (b.analyser.state() == Analyser::State::Searching) {
        b.analyser.halt();
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

void App::startReviewTask(const EngineEntry& engine, const std::vector<GameReview>& kept) {
    auto& b = board_;
    ReviewRunner::Setup setup;
    setup.engine     = engine;
    setup.settings   = b.settings.analysis;
    setup.game       = b.review_game;
    setup.chess960   = b.review_chess960;
    setup.ceiling_ms = b.settings.review_ceiling_ms;
    // Resumed after the passes this engine completed, unless it went as far as asked: then again from the start.
    for (const auto& r : kept) {
        if (r.engine == engine.name && r.passes > 0 && r.complete(b.review_game.moves.size()) &&
            r.passes < reviewPasses(setup.ceiling_ms))
            setup.resume = r;
    }
    BoardPage::ReviewTask task;
    task.engine = engine.name;
    task.saved  = setup.resume.passes;
    task.runner = std::make_unique<ReviewRunner>();
    task.runner->start(std::move(setup));
    (void)task.runner->state(task.state, task.version);
    b.review_tasks.push_back(std::move(task));
}

void App::startGameReview() {
    auto& b = board_;
    if (b.reviewing() || b.game.moves.empty() || (b.watching && b.match && b.match_state.running)) return;
    // Every analysis the game has goes on; without one, the analysis engine's.
    const auto kept = loadReviews(storage_.root() / "analyses", b.game);
    std::vector<const EngineEntry*> engines;
    for (const auto& r : kept) {
        const auto it =
            std::find_if(engines_.begin(), engines_.end(), [&](const auto& e) { return e.name == r.engine; });
        if (it != engines_.end()) engines.push_back(&*it);
    }
    if (engines.empty()) {
        const auto* engine = analysisEngine();
        if (!engine) {
            b.message       = TR("No engine to analyse with: add one on the Engines page.");
            b.message_error = true;
            return;
        }
        engines.push_back(engine);
    }
    b.review_game     = b.game;
    b.review_chess960 = b.chess960;
    b.review_game_key = reviewKey(b.game);
    for (const auto* e : engines) startReviewTask(*e, kept);
    b.message.clear();
}

void App::addReviewEngine(const std::string& name) {
    auto& b       = board_;
    const auto it = std::find_if(engines_.begin(), engines_.end(), [&](const auto& e) { return e.name == name; });
    if (it == engines_.end() || b.game.moves.empty()) return;
    // The analyses of another game end first, their passes kept.
    if (b.reviewing() && b.review_game_key != reviewKey(b.game)) cancelGameReview();
    if (std::any_of(b.review_tasks.begin(), b.review_tasks.end(), [&](const auto& t) { return t.engine == name; }))
        return;
    const auto kept = loadReviews(storage_.root() / "analyses", b.game);
    if (!b.reviewing()) {
        b.review_game     = b.game;
        b.review_chess960 = b.chess960;
        b.review_game_key = reviewKey(b.game);
        // The game's other analyses go on with it, so that they stay comparable.
        for (const auto& r : kept) {
            const auto e =
                std::find_if(engines_.begin(), engines_.end(), [&](const auto& x) { return x.name == r.engine; });
            if (e != engines_.end() && r.engine != name) startReviewTask(*e, kept);
        }
    }
    startReviewTask(*it, kept);
}

void App::judgeWith(const std::string& engine) {
    auto& b       = board_;
    auto kept     = loadReviews(storage_.root() / "analyses", b.game);
    const auto it = std::find_if(kept.begin(), kept.end(), [&](const auto& r) { return r.engine == engine; });
    if (it == kept.end() || it == kept.begin()) return;
    std::rotate(kept.begin(), it, it + 1);
    (void)saveReviews(storage_.root() / "analyses", b.game, kept);
    b.review_key.clear();
}

void App::removeReview(const std::string& engine) {
    auto& b = board_;
    if (b.review_game_key == reviewKey(b.game)) {
        for (auto it = b.review_tasks.begin(); it != b.review_tasks.end(); ++it) {
            if (it->engine != engine) continue;
            it->runner->stop();
            b.review_tasks.erase(it);
            break;
        }
    }
    auto kept = loadReviews(storage_.root() / "analyses", b.game);
    std::erase_if(kept, [&](const auto& r) { return r.engine == engine; });
    (void)saveReviews(storage_.root() / "analyses", b.game, kept);
    b.review_key.clear();
}

void App::cancelGameReview() {
    auto& b = board_;
    for (auto& t : b.review_tasks) t.runner->stop();
    pollGameReview();  // their passes done kept
    b.review_tasks.clear();
}

void App::pollGameReview() {
    auto& b = board_;
    if (b.review_tasks.empty()) return;
    // Together pass by pass, so that their curves compare: none starts a pass before the others finished theirs; all
    // at once when the cores allow, else one after the other, the judging one first.
    int lowest = 1 << 20;
    for (const auto& t : b.review_tasks)
        if (t.state.running) lowest = std::min(lowest, t.state.pass);
    int needed = tournamentThreads();
    for (const auto& t : b.review_tasks) needed += t.state.running ? std::max(1, b.settings.analysis.threads) : 0;
    const bool one_at_a_time = b.review_tasks.size() > 1 && needed > std::max(1, topology_.logical);
    bool given               = false;
    for (auto& t : b.review_tasks) {
        auto& r = *t.runner;
        r.setFirst(b.review_game_key == b.review_key ? b.ply : -1);
        r.setCeiling(b.settings.review_ceiling_ms);
        r.setPassLimit(lowest);
        bool paused = b.analysing;
        if (one_at_a_time && t.state.running) {
            if (t.state.pass == lowest && !given)
                given = true;
            else
                paused = true;
        }
        r.setPaused(paused);
        if (!r.state(t.state, t.version)) continue;
        // A pass completed: kept with the game, and read again if it is the game shown.
        if (t.state.settled.passes > t.saved) {
            (void)saveReview(storage_.root() / "analyses", b.review_game, t.state.settled);
            t.saved = t.state.settled.passes;
            if (b.review_key == b.review_game_key) b.review_key.clear();
        }
        if (!t.state.running && !t.state.error.empty()) {
            b.message       = t.state.error;
            b.message_error = true;
        }
    }
    std::erase_if(b.review_tasks, [](const auto& t) { return !t.state.running; });
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
        const char* review  = b.reviewing() ? TR("Stop the game's analysis") : TR("Analyse the game");
        float width         = 0;
        for (const char* label : {open, paste, edit, play, review, analyse})
            width += ImGui::CalcTextSize(label).x + px(44);
        width += ImGui::CalcTextSize("Ctrl+V").x + ImGui::CalcTextSize("A").x * 2 + ImGui::GetFrameHeight() + px(28);
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
        // The game analysed in passes, up to the time per position chosen beside.
        const bool live_game = b.watching && b.match && b.match_state.running;
        ImGui::BeginDisabled(b.editor.has_value() || (!b.reviewing() && (b.game.moves.empty() || live_game)));
        if (ui::iconButton("board_review", b.reviewing() ? ui::Icon::Stop : ui::Icon::Chart, review))
            b.reviewing() ? cancelGameReview() : startGameReview();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", live_game
                                        ? TR("Once the game is over.")
                                        : TR("G: the analysis engine goes through every position of the game, in "
                                             "the background, then again for longer at each pass: the graph comes "
                                             "at once and sharpens. Its inaccuracies, mistakes and blunders are "
                                             "marked."));
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, px(2));
        if (ImGui::ArrowButton("##review_ceiling", ImGuiDir_Down)) ImGui::OpenPopup("##review_ceiling_menu");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("How far the analysis of the game goes."));
        if (ImGui::BeginPopup("##review_ceiling_menu")) {
            ImGui::TextDisabled("%s", TR("Refine the analysis of the game up to"));
            for (const int64_t ceiling : {int64_t{320}, int64_t{1280}, int64_t{5120}, int64_t{20480}, int64_t{0}}) {
                const auto label =
                    ceiling > 0 ? trf("{} per position", ui::formatClock(ceiling)) : std::string(TR("until stopped"));
                if (ImGui::MenuItem(label.c_str(), nullptr, b.settings.review_ceiling_ms == ceiling)) {
                    b.settings.review_ceiling_ms = ceiling;
                    saveBoardSettings();
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("G");
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
    if (b.preview && b.preview->hover) {
        if (!b.preview_hovered) b.preview.reset();
        b.preview_hovered = false;
    }
    if (b.preview && !b.preview->hover && b.preview->at != b.ply) b.preview.reset();
    if (keys && !b.editor) {
        const int before = b.ply;
        b.ply = stepPly(navKeys(b.flip), b.ply, total);
        if (b.ply != before) b.preview.reset();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (b.preview)
                b.preview.reset();
            else
                b.selected.reset();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_N, false) && !ImGui::GetIO().KeyCtrl) {
            if (const int to = judgedPly(ImGui::GetIO().KeyShift ? -1 : 1); to >= 0) b.ply = to;
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) pasteOnBoard();
    }
    if (keys && boardKey()) setBoardAnalysis(!b.analysing);
    if (keys && !b.editor && ImGui::IsKeyPressed(ImGuiKey_X, false) && !ImGui::GetIO().KeyCtrl)
        setBoardThreat(!b.threat_on);
    if (keys && !b.editor && ImGui::IsKeyPressed(ImGuiKey_G, false) && !ImGui::GetIO().KeyCtrl) {
        if (b.reviewing())
            cancelGameReview();
        else
            startGameReview();
    }

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
    const float graph = boardGraphHeight();
    drawBoardMoves(std::max(px(160), ImGui::GetContentRegionAvail().y - graph - ImGui::GetStyle().ItemSpacing.y));
    drawBoardGraph(graph);
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
        if (review_ply_ >= 0) board_.ply = std::clamp(review_ply_, 0, static_cast<int>(board_.game.moves.size()));
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
            if (ImGui::ImageButton("##brush", atlas_.texture(button), ImVec2(button, button), uv0, uv1)) {
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
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("A click on a square then empties it. A right click empties a square whatever is "
                                   "chosen."));
    ui::topic(guide::topics::BoardOpen);
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
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The number of the move to play, as the FEN gives it."));

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
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("The file of a pawn that just moved two squares, to be taken en passant: only "
                                       "such files are offered."));
        }
        ui::topic(guide::topics::BoardOpen);
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
    // Without the board's analysis, each analysis of the game's best move here, in its colour.
    if (!fresh && b.settings.arrows) {
        const auto reviews = shownReviews();
        for (std::size_t i = reviews.size(); i-- > 0;) {
            const auto& positions = reviews[i]->positions;
            if (static_cast<std::size_t>(b.ply) >= positions.size() ||
                positions[static_cast<std::size_t>(b.ply)].best.empty())
                continue;
            const auto moves = legalPrefix(board, {positions[static_cast<std::size_t>(b.ply)].best}, 1);
            if (!moves.empty()) {
                auto colour = analysisColour(i);
                colour      = (colour & ~IM_COL32_A_MASK) | (IM_COL32(0, 0, 0, 170) & IM_COL32_A_MASK);
                decor.arrows.push_back({moves.front(), colour});
            }
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
    // The threat, in red.
    if (b.threat_on && !b.threat.lines().empty() && !b.threat.lines().front().pv.empty()) {
        if (const auto position = threatPosition(); position && b.threat.position() == *position) {
            auto passed = board;
            passed.makeNullMove();
            if (const auto moves = legalPrefix(passed, {b.threat.lines().front().pv.front()}, 1); !moves.empty())
                decor.arrows.push_back({moves.front(), IM_COL32(205, 40, 40, 200)});
        }
    }
    if (b.ply > 0) {
        const auto& m = b.game.moves[static_cast<std::size_t>(b.ply - 1)];
        // The move's judgement on its square.
        if (static_cast<std::size_t>(b.ply - 1) < b.judged.size()) {
            const auto j = b.judged[static_cast<std::size_t>(b.ply - 1)].judgement;
            if (j != Judgement::None)
                decor.badge = BoardBadge{shownTarget(m.move), judgementMark(j), judgementBadgeColour(j)};
        }
        // The reply the engine that played it expected, in dashes: its PV's second move.
        if (b.settings.arrows && !m.pv.empty()) {
            std::istringstream pv(m.pv);
            std::string first, reply;
            if (pv >> first >> reply) {
                if (const auto moves = legalPrefix(board, {reply}, 1); !moves.empty())
                    decor.arrows.insert(decor.arrows.begin(), {moves.front(), IM_COL32(60, 64, 74, 190), true});
            }
        }
    }

    // A line shown on the board instead of the game: its last position, framed.
    std::optional<chess::Board> line_board;
    if (b.preview) {
        auto at = boardAt(b.game, b.chess960, std::clamp(b.preview->ply, 0, static_cast<int>(b.game.moves.size())));
        const auto moves = legalPrefix(at, b.preview->moves, b.preview->moves.size());
        for (const auto& mv : moves) at.makeMove(mv);
        line_board       = at;
        const float bar  = decor.eval_white;  // kept: the board does not move
        decor            = BoardDecor{};
        decor.flipped    = b.flip;
        decor.eval_white = bar;
        if (!moves.empty()) decor.last_move = moves.back();
        decor.frame = ui::ACCENT;
    }
    const auto& shown = line_board ? *line_board : board;

    const ImVec2 top = ImGui::GetCursorScreenPos();
    if (appearance::current().board3d.on && board3d_.available()) {
        const auto input = board3d_.draw(shown, size, decor);
        if (line_board && (input.clicked || input.right_click)) {
            b.preview.reset();
        } else {
            if (input.clicked) boardClick(board, *input.clicked);
            if (input.right_click) b.selected.reset();
        }
    } else {
        if (!appearance::current().board3d.on) board3d_.release();
        drawBoard(atlas_, shown, size, decor);
        if (line_board &&
            (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))) {
            b.preview.reset();
        } else {
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                if (const auto square = boardSquareAt(ImGui::GetMousePos(), top, size, decor))
                    boardClick(board, *square);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) b.selected.reset();
        }
    }
    // What the line is, over the board.
    if (line_board && b.preview) {
        auto* dl = ImGui::GetWindowDrawList();
        const auto label =
            b.preview->label + (b.preview->hover ? "" : "  ·  " + std::string(TR("Esc or a click: the game")));
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        const ImVec2 p0(top.x + evalBarSpace(size) + px(8), top.y + px(8));
        dl->AddRectFilled(p0, ImVec2(p0.x + ts.x + px(16), p0.y + ts.y + px(8)), IM_COL32(20, 24, 32, 215), px(6));
        dl->AddText(ImVec2(p0.x + px(8), p0.y + px(4)), IM_COL32(240, 243, 248, 255), label.c_str());
    }

    // The piece a pawn becomes.
    if (ImGui::BeginPopup("##promotion")) {
        static constexpr chess::PieceType types[] = {chess::PieceType::QUEEN, chess::PieceType::ROOK,
                                                     chess::PieceType::BISHOP, chess::PieceType::KNIGHT};
        for (const auto type : types) {
            ImVec2 uv0, uv1;
            atlas_.uv(chess::Piece(type, stm), uv0, uv1);
            ImGui::PushID(static_cast<int>(type));
            if (ImGui::ImageButton("##piece", atlas_.texture(px(44)), ImVec2(px(44), px(44)), uv0, uv1) &&
                b.promotion) {
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

    // Through the moves, back to the move being played while a game from here goes on, and the position as text.
    const int total = static_cast<int>(b.game.moves.size());
    const bool live = b.match && b.match_state.running && b.watching;
    if (setup_.nav_buttons || (live && b.ply < total)) {
        const float copy = ImGui::CalcTextSize(TR("Copy FEN")).x + ImGui::GetStyle().FramePadding.x * 2 +
                           ImGui::GetStyle().ItemSpacing.x;
        const auto click = drawBoardNav("##board_nav", b.ply, total, b.flip, setup_.nav_buttons, live, size - copy);
        b.ply            = stepPly(click.step, b.ply, total);
        if (click.hide) {
            setup_.nav_buttons = false;
            saveSetupSoon();
        }
        ImGui::SameLine();
    }
    // From one judged move to the next.
    if (std::any_of(b.judged.begin(), b.judged.end(), [](const auto& j) { return j.judgement != Judgement::None; })) {
        const int prev = judgedPly(-1), next = judgedPly(1);
        ImGui::BeginDisabled(prev < 0);
        if (ImGui::Button("< ?##prev_judged")) b.ply = prev;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("Shift+N: the previous move the analysis marked (?!, ?, ??)."));
        ImGui::SameLine(0, px(2));
        ImGui::BeginDisabled(next < 0);
        if (ImGui::Button("? >##next_judged")) b.ply = next;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("N: the next move the analysis marked (?!, ?, ??)."));
        ImGui::SameLine();
    }
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
            b.threat.stop();
            if (b.analysing) setBoardAnalysis(true);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The engine of the library that searches the position shown. Its options other "
                                   "than those beside are the library's, sent as a game sends them."));
    }
    ui::topic(guide::topics::Analysis);

    // Threads, hash and lines, folded into one line: a click opens them.
    ImGui::SameLine();
    {
        const auto summary = trf("{} · {} · {}", trn("{} thread", "{} threads", s.threads, s.threads),
                                 s.hash > 0 ? fmt::format("{} MB", s.hash) : std::string(TR("hash of the engine")),
                                 trn("{} line", "{} lines", s.multipv, s.multipv));
        if (ui::iconButton("analysis_settings", ui::Icon::Settings, summary.c_str()))
            ImGui::OpenPopup("##analysis_settings_popup");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("This analysis's own threads, hash and lines: they change neither the engine in "
                                       "the library nor a tournament."));
        }
        if (ImGui::BeginPopup("##analysis_settings_popup")) {
            const auto number = [&](const char* id, const char* label, int& value, int lo, int hi, const char* tip) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                ImGui::SameLine(px(90));
                ImGui::SetNextItemWidth(px(80));
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
            ImGui::TextDisabled("%s", TR("this analysis only"));
            ImGui::EndPopup();
        }
    }
    if (changed) {
        saveBoardSettings();
        a.update(s);
        auto one    = s;
        one.multipv = 1;
        b.threat.update(one);
    }
    // The threat: the opponent's best move, were it to move again.
    ImGui::SameLine();
    {
        const bool on = b.threat_on;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ui::SELECT);
        if (ImGui::SmallButton(TR("Threat"))) setBoardThreat(!b.threat_on);
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("X: the opponent's best move if it could move again, as a red arrow (another "
                                       "process of the engine, its threads counted)."));
        }
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("X");
    }
    // Its place among the cores, at the right.
    {
        const int threads = boardThreads();
        const int busy    = tournamentThreads();
        auto chip         = trf("{} / {} threads", threads, topology_.logical);
        if (busy > 0) chip += " · " + trf("tournament on {}", busy);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(8),
                                 ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(chip.c_str()).x - px(4)));
        ImGui::AlignTextToFramePadding();
        if (busy > 0)
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", chip.c_str());
        else
            ImGui::TextDisabled("%s", chip.c_str());
        if (ImGui::IsItemHovered()) {
            std::string tip = TR("The threads the analysis and the games of this page use now, out of the machine's.");
            if (busy > 0)
                tip += "\n" + trf("A tournament is running: the analysis competes with its games ({} threads).", busy);
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }

    // How the search goes.
    const auto stm = board.sideToMove();
    if (a.state() == Analyser::State::Failed) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", a.error().c_str());
        ImGui::PopStyleColor();
    } else if (!a.active()) {
        ImGui::TextDisabled("%s", TR("Press A, or Analyse, for the engine's best moves here."));
    } else if (a.state() == Analyser::State::Starting) {
        ImGui::TextDisabled("%s", trf("Starting {}…", a.engine()).c_str());
    } else {
        const auto& lines = a.lines();
        const bool fresh  = a.position() == analysisPosition(b.game, b.chess960, b.ply);
        if (!fresh) {
            ImGui::TextDisabled("%s", b.analysing ? "…" : TR("Not this position."));
        } else if (!lines.empty()) {
            // The search in figures.
            const auto& best = lines.front();
            const std::array<std::pair<const char*, std::string>, 6> tiles{
                {{TR("Depth"), fmt::format("{}/{}", best.depth, best.seldepth)},
                 {TR("Nodes"), ui::formatCount(best.nodes)},
                 {TR("Speed"), ui::formatCount(best.nps) + "/s"},
                 {TR("Time"), ui::formatClock(best.time_ms)},
                 {TR("TB hits"), ui::formatCount(best.tbhits)},
                 {TR("Hash"), best.hashfull >= 0 ? fmt::format("{} %", best.hashfull / 10) : std::string("–")}}};
            const float gap = px(4);
            const float w   = (ImGui::GetContentRegionAvail().x - gap * 5) / 6;
            const float h   = ImGui::GetTextLineHeight() * 2.1f + px(6);
            auto* dl        = ImGui::GetWindowDrawList();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const ImU32 tile =
                ui::currentTheme() == ui::Theme::Light ? IM_COL32(243, 245, 248, 255) : IM_COL32(32, 36, 43, 255);
            for (std::size_t i = 0; i < tiles.size(); ++i) {
                const ImVec2 p0(at.x + static_cast<float>(i) * (w + gap), at.y);
                dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), tile, px(5));
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.7f, ImVec2(p0.x + px(7), p0.y + px(3)),
                            ui::MUTED, tiles[i].first);
                dl->AddText(ImVec2(p0.x + px(7), p0.y + h - ImGui::GetTextLineHeight() - px(3)),
                            ImGui::GetColorU32(ImGuiCol_Text), tiles[i].second.c_str());
            }
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, h));
            if (best.wdl) {
                // The best line's chances, White's.
                auto wdl = *best.wdl;
                if (stm == chess::Color::BLACK) std::swap(wdl[0], wdl[2]);
                wdlLine(wdl);
            }
        }
        if (fresh) {
            const auto* figurines = appearance::current().figurines ? &atlas_ : nullptr;
            const float badge_w   = ImGui::CalcTextSize("+00.00").x + px(10);
            const float depth_w   = ImGui::CalcTextSize("d00").x + px(8);
            for (std::size_t i = 0; i < lines.size(); ++i) {
                const auto& line = lines[i];
                if (line.pv.empty()) continue;
                ImGui::PushID(static_cast<int>(i));
                // Its score, White's, the best one set apart; its depth; its moves on two lines at most.
                const auto score = whiteScoreText(line, stm);
                const ImVec2 p   = ImGui::GetCursorScreenPos();
                auto* dl         = ImGui::GetWindowDrawList();
                const float lh   = ImGui::GetTextLineHeight() + px(7);
                const ImU32 bg   = i == 0 ? (ui::currentTheme() == ui::Theme::Light ? IM_COL32(250, 251, 253, 255)
                                                                                    : IM_COL32(230, 234, 240, 255))
                                          : (ui::currentTheme() == ui::Theme::Light ? IM_COL32(236, 239, 244, 255)
                                                                                    : IM_COL32(40, 45, 54, 255));
                const ImU32 fg   = i == 0 && ui::currentTheme() != ui::Theme::Light ? IM_COL32(29, 35, 48, 255)
                                                                                    : ImGui::GetColorU32(ImGuiCol_Text);
                dl->AddRectFilled(ImVec2(p.x, p.y + px(2)), ImVec2(p.x + badge_w - px(6), p.y + lh - px(3)), bg, px(4));
                if (i == 0)
                    dl->AddRect(ImVec2(p.x, p.y + px(2)), ImVec2(p.x + badge_w - px(6), p.y + lh - px(3)), ui::CARD_BD,
                                px(4));
                const ImVec2 ts = ImGui::CalcTextSize(score.c_str());
                dl->AddText(ImVec2(p.x + (badge_w - px(6) - ts.x) / 2, p.y + (lh - ts.y) / 2), fg, score.c_str());
                dl->AddText(ImVec2(p.x + badge_w, p.y + (lh - ts.y) / 2), ui::MUTED,
                            fmt::format("d{}", line.depth).c_str());
                ImGui::SetCursorScreenPos(ImVec2(p.x + badge_w + depth_w, p.y));
                const auto items = lineItems(board, line.pv, 24);
                const auto ev    = drawNotation("##line", items, ImGui::GetContentRegionAvail().x, figurines, 2);
                // Hovered: the position after that move, on the board; clicked: the line played up to it.
                if (ev.hovered >= 0) {
                    BoardPreview preview;
                    preview.ply = preview.at = b.ply;
                    preview.moves.assign(
                        line.pv.begin(),
                        line.pv.begin() +
                            std::min<std::size_t>(line.pv.size(), static_cast<std::size_t>(ev.hovered) + 1));
                    preview.label     = pvToSan(board, preview.moves, preview.moves.size());
                    preview.hover     = true;
                    b.preview         = std::move(preview);
                    b.preview_hovered = true;
                    ImGui::SetTooltip("%s", TR("Click: the line played up to this move. Scores are White's."));
                }
                if (ev.clicked >= 0) {
                    for (const auto& m : legalPrefix(board, line.pv, static_cast<std::size_t>(ev.clicked) + 1))
                        boardPlay(m);
                    b.preview.reset();
                }
                ImGui::PopID();
            }
            // The threat.
            if (b.threat_on) {
                const auto& t = b.threat.lines();
                ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
                if (board.inCheck())
                    ImGui::TextUnformatted(TR("Threat: none while in check."));
                else if (!t.empty() && !t.front().pv.empty()) {
                    auto passed = board;
                    passed.makeNullMove();
                    ImGui::TextUnformatted(trf("Threat: {} ({})", pvToSan(passed, t.front().pv, 4),
                                               t.front().score ? whiteScoreText(t.front(), ~stm) : "")
                                               .c_str());
                } else {
                    ImGui::TextUnformatted(TR("Threat: …"));
                }
                ImGui::PopStyleColor();
            }
        }
    }
    ui::endCard();
}

void App::drawBoardMoves(float height) {
    auto& b         = board_;
    const int total = static_cast<int>(b.game.moves.size());

    ui::beginCard("board_game", ImVec2(0, height));
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
        b.reviews    = total > 0 ? loadReviews(storage_.root() / "analyses", b.game) : std::vector<GameReview>{};
        b.judged = b.reviews.empty() ? std::vector<MoveReview>{} : reviewMoves(b.game, b.chess960, b.reviews.front());
    }
    drawGameReview();

    const float strip = b.ply > 0 && b.ply <= total ? thisMoveHeight() : 0.0f;
    ImGui::BeginChild("##board_moves", ImVec2(0, -strip), ImGuiChildFlags_Borders);
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
    if (strip > 0) drawThisMove();
    ui::endCard();
}

float App::thisMoveHeight() const {
    const auto& b = board_;
    const auto& m = b.game.moves[static_cast<std::size_t>(b.ply - 1)];
    int rows      = 1 + (!m.book && m.pv.find(' ') != std::string::npos ? 1 : 0);
    for (const auto* r : shownReviews())
        if (static_cast<std::size_t>(b.ply) < r->positions.size()) ++rows;
    return static_cast<float>(rows) * ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2;
}

int App::judgedPly(int direction) const {
    const auto& b   = board_;
    const int total = static_cast<int>(std::min(b.judged.size(), b.game.moves.size()));
    for (int ply = b.ply + direction; ply >= 1 && ply <= total; ply += direction) {
        if (b.judged[static_cast<std::size_t>(ply - 1)].judgement != Judgement::None) return ply;
    }
    return -1;
}

void App::drawThisMove() {
    auto& b          = board_;
    const int ply    = b.ply;
    const auto& m    = b.game.moves[static_cast<std::size_t>(ply - 1)];
    const auto prior = boardAt(b.game, b.chess960, ply - 1);
    const auto after = boardAt(b.game, b.chess960, ply);
    const auto split = [](const std::string& pv) {
        std::vector<std::string> out;
        std::istringstream in(pv);
        for (std::string w; in >> w;) out.push_back(w);
        return out;
    };
    const auto name_of = [&](chess::Color c) {
        auto name = b.game.header(c == chess::Color::WHITE ? "White" : "Black");
        return name.empty() || name == "?" ? std::string(c == chess::Color::WHITE ? TR("White") : TR("Black")) : name;
    };
    const auto white = [](fastchess::Score score, chess::Color stm) {
        if (stm == chess::Color::BLACK) score.value = -score.value;
        return ui::formatScore(score);
    };
    const auto reviews = shownReviews();
    float label_w      = ImGui::CalcTextSize(TR("This move")).x;
    label_w            = std::max(label_w, ImGui::CalcTextSize(TR("It expected")).x);
    for (const auto* r : reviews) label_w = std::max(label_w, ImGui::CalcTextSize(r->engine.c_str()).x);
    label_w += px(12);
    const float show_w = ImGui::CalcTextSize(TR("Show")).x + ImGui::GetStyle().FramePadding.x * 2;
    int row_id         = 0;
    // A row: its label, its text (cut to the room), a Show that lays a line on the board.
    const auto row = [&](const std::string& label, const std::string& text, int base, std::vector<std::string> line,
                         const std::string& line_label) {
        ImGui::PushID(row_id++);
        const float start = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label.c_str());
        ImGui::SameLine(start + label_w);
        const float room  = ImGui::GetContentRegionAvail().x - (line.empty() ? 0.0f : show_w + px(8));
        std::string shown = text;
        while (!shown.empty() && ImGui::CalcTextSize(shown.c_str()).x > room) shown.pop_back();
        if (shown.size() < text.size() && shown.size() > 3) shown = shown.substr(0, shown.size() - 3) + "…";
        ImGui::TextUnformatted(shown.c_str());
        if (shown.size() < text.size() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text.c_str());
        if (!line.empty()) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - show_w);
            const bool on = b.preview && !b.preview->hover && b.preview->label == line_label;
            if (ImGui::SmallButton(on ? TR("Back") : TR("Show"))) {
                if (on)
                    b.preview.reset();
                else
                    b.preview = BoardPreview{base, b.ply, std::move(line), line_label, false};
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The line on the board, the game untouched."));
        }
        ImGui::PopID();
    };

    ImGui::Separator();
    // The move, and what the engine that played it said.
    const int number  = prior.fullMoveNumber();
    const auto judged = static_cast<std::size_t>(ply - 1) < b.judged.size()
                            ? b.judged[static_cast<std::size_t>(ply - 1)]
                            : MoveReview{};
    std::string text  = fmt::format("{}{}{}{}", number, m.mover == chess::Color::WHITE ? "." : "…", m.san,
                                    judgementMark(judged.judgement));
    if (m.book) {
        text += "   " + std::string(TR("book move"));
    } else if (m.score) {
        text += "   " + name_of(m.mover) + ": " + white(*m.score, m.mover);
        if (m.depth > 0)
            text += m.seldepth > 0 ? fmt::format(" · d{}/{}", m.depth, m.seldepth) : fmt::format(" · d{}", m.depth);
        if (m.time_ms >= 0) text += " · " + ui::formatClock(m.time_ms);
        if (m.nodes > 0) text += " · " + ui::formatCount(m.nodes) + " n";
        if (m.nps > 0) text += " · " + ui::formatCount(static_cast<uint64_t>(m.nps)) + " n/s";
        if (m.tbhits > 0) text += " · TB " + ui::formatCount(m.tbhits);
    }
    row(TR("This move"), text, 0, {}, "");
    // The line it expected after it.
    if (!m.book) {
        auto pv = split(m.pv);
        if (pv.size() > 1) {
            pv.erase(pv.begin());
            const auto san = pvToSan(after, pv, 10);
            row(TR("It expected"), san, ply, pv, trf("{} expected {}", name_of(m.mover), pvToSan(after, pv, 6)));
        }
    }
    // Each analysis's verdict, and its best line from the position before the move.
    for (std::size_t i = 0; i < reviews.size(); ++i) {
        const auto& r = *reviews[i];
        if (static_cast<std::size_t>(ply) >= r.positions.size()) continue;
        const auto& before = r.positions[static_cast<std::size_t>(ply - 1)];
        MoveReview verdict;
        if (i == 0) {
            verdict = judged;
        } else if (const auto all = reviewMoves(b.game, b.chess960, r);
                   static_cast<std::size_t>(ply - 1) < all.size()) {
            verdict = all[static_cast<std::size_t>(ply - 1)];
        }
        std::string what;
        const auto best      = legalPrefix(prior, {before.best}, 1);
        const auto best_text = best.empty() || !before.score ? std::string()
                                                             : chess::uci::moveToSan(prior, best.front()) + " (" +
                                                                   white(*before.score, prior.sideToMove()) + ")";
        if (!before.score || !r.positions[static_cast<std::size_t>(ply)].score) {
            what = "…";
        } else if (verdict.best) {
            what = trf("The best move ({})", white(*before.score, prior.sideToMove()));
        } else {
            const char* kind = verdict.judgement == Judgement::Blunder      ? TR("Blunder")
                               : verdict.judgement == Judgement::Mistake    ? TR("Mistake")
                               : verdict.judgement == Judgement::Inaccuracy ? TR("Inaccuracy")
                                                                            : nullptr;
            what             = kind ? trf("{}: {:.0f} % of the winning chances lost", kind, verdict.loss)
                                    : trf("{:.0f} % of the winning chances lost", verdict.loss);
            if (!best_text.empty()) what += " · " + trf("best {}", best_text);
        }
        row(r.engine, what, ply - 1, split(before.pv), trf("{}: {}", r.engine, pvToSan(prior, split(before.pv), 6)));
    }
}

// ---------------------------------------------------------------------------
// The game graph

float App::boardGraphHeight() const {
    const auto& b = board_;
    if (b.game.moves.empty()) return 0.0f;
    const float pad = ImGui::GetStyle().WindowPadding.y * 2;
    if (b.settings.graph.folded) return foldedGraphHeight() + pad;
    return std::floor(std::max(px(230), std::min(px(320), ImGui::GetContentRegionAvail().y * 0.36f)));
}

std::string App::boardGraphTip(int ply) const {
    const auto& b     = board_;
    const auto judged = ply > 0 && static_cast<std::size_t>(ply - 1) < b.judged.size()
                            ? b.judged[static_cast<std::size_t>(ply - 1)]
                            : MoveReview{};
    auto tip = graphMoveTip(b.game, ply, judged.judgement != Judgement::None ? judgementMark(judged.judgement) : "");
    const auto* review = shownReview();
    if (!tip.empty() && review && static_cast<std::size_t>(ply) < review->positions.size()) {
        const auto& pos = review->positions[static_cast<std::size_t>(ply)];
        if (const float v = graphPawns(pos.score, ~b.game.moves[static_cast<std::size_t>(ply - 1)].mover);
            std::isfinite(v)) {
            tip += "\n" +
                   trf("Analysis ({}): {}", review->engine,
                       ui::formatScore(whiteScore(*pos.score, ~b.game.moves[static_cast<std::size_t>(ply - 1)].mover)));
        }
    }
    return tip;
}

void App::drawBoardGraph(float height) {
    auto& b = board_;
    if (height <= 0) return;
    ui::beginCard("board_graph", ImVec2(0, height));
    const auto reviews = shownReviews();
    std::vector<GraphAnalysis> analyses;
    for (std::size_t i = 0; i < reviews.size(); ++i) {
        analyses.push_back({reviews[i]->engine, i == 0 && reviews.size() > 1 ? TR("judge") : TR("analysis"),
                            analysisColour(i), reviews[i]});
    }
    auto graph = buildGameGraph(b.game, b.chess960, b.settings.graph.chart, analyses, b.judged);
    // A mark is settled when the last pass did not change it and every other analysis judged the move alike.
    if (!b.reviews.empty()) {
        const auto& previous = b.reviews.front().previous;
        std::vector<std::vector<MoveReview>> others;
        for (std::size_t i = 1; i < b.reviews.size(); ++i) {
            if (b.reviews[i].complete(b.game.moves.size()))
                others.push_back(reviewMoves(b.game, b.chess960, b.reviews[i]));
        }
        for (auto& m : graph.marks) {
            const auto i = static_cast<std::size_t>(m.ply - 1);
            m.hollow     = i < previous.size() && previous[i] != static_cast<int8_t>(m.judgement);
            for (const auto& o : others) m.hollow |= i < o.size() && o[i].judgement != m.judgement;
        }
    }
    GraphDraw draw;
    draw.current       = b.ply;
    draw.tip           = [this](int ply) { return boardGraphTip(ply); };
    draw.can_add       = !b.game.moves.empty() && !(b.watching && b.match && b.match_state.running);
    draw.analyses_menu = true;
    // The analyses in passes: where they are, what each position got.
    const bool running = b.reviewing() && b.review_game_key == b.review_key;
    if (running) {
        const auto ceiling = b.settings.review_ceiling_ms;
        const int passes   = reviewPasses(ceiling);
        int pass           = 1 << 20;
        std::size_t done = 0, all = 0;
        for (const auto& t : b.review_tasks) pass = std::min(pass, t.state.pass);
        for (const auto& t : b.review_tasks) {
            if (t.state.pass != pass) {
                done += t.state.done.size();  // waiting for the others
            } else {
                done += static_cast<std::size_t>(std::count(t.state.done.begin(), t.state.done.end(), true));
            }
            all += t.state.done.size();
        }
        draw.progress = ceiling > 0 ? trf("pass {} of {} · {} per position · {}/{}", pass + 1, passes,
                                          ui::formatClock(passTime(pass)), done, all)
                                    : trf("pass {} · {} per position · {}/{}", pass + 1,
                                          ui::formatClock(passTime(pass)), done, all);
        if (b.analysing) draw.progress += " · " + std::string(TR("paused while A analyses"));
        const auto& job = b.review_tasks.front().state;
        const int top   = ceiling > 0 ? passes : pass + 1;
        for (const int level : job.level)
            graph.level.push_back(level < 0 ? 0.0f : static_cast<float>(level + 1) / static_cast<float>(top));
        draw.action = TR("Stop");
        draw.action_tip =
            TR("G: stop the analysis of the game. Its passes done are kept, and it goes on from there "
               "next time.");
    } else if (!b.reviewing() && draw.can_add) {
        bool more = false;
        for (const auto& r : b.reviews) more |= r.passes > 0 && r.passes < reviewPasses(b.settings.review_ceiling_ms);
        draw.action     = more ? TR("Continue the analysis") : b.reviews.empty() ? TR("Analyse the game") : nullptr;
        draw.action_tip =
            TR("G: the analysis engine goes through every position of the game, in the background, "
               "then again for longer at each pass: the graph comes at once and sharpens.");
    }
    const auto ev = drawGameGraph("##board_graph", graph, b.settings.graph, draw,
                                  ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));
    if (ev.go_to >= 0) b.ply = std::clamp(ev.go_to, 0, static_cast<int>(b.game.moves.size()));
    if (ev.view_changed) saveBoardSettings();
    if (ev.action) running ? cancelGameReview() : startGameReview();
    if (ev.judge >= 0 && static_cast<std::size_t>(ev.judge) < reviews.size())
        judgeWith(reviews[static_cast<std::size_t>(ev.judge)]->engine);
    if (ev.remove >= 0 && static_cast<std::size_t>(ev.remove) < reviews.size())
        removeReview(reviews[static_cast<std::size_t>(ev.remove)]->engine);
    // Another engine: those of the library that have no analysis of the game yet.
    if (ev.add_analysis) ImGui::OpenPopup("##add_analysis");
    if (ImGui::BeginPopup("##add_analysis")) {
        ImGui::TextDisabled("%s", TR("Another engine analyses the game too, for as long at each pass:"));
        bool any = false;
        for (const auto& e : engines_) {
            if (e.inactive ||
                std::any_of(reviews.begin(), reviews.end(), [&](const auto* r) { return r->engine == e.name; }))
                continue;
            any = true;
            if (ImGui::MenuItem(e.name.c_str())) addReviewEngine(e.name);
        }
        if (!any) ImGui::TextDisabled("%s", TR("Every engine of the library analyses it already."));
        ImGui::EndPopup();
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
    const auto* review = b.reviews.empty() ? nullptr : &b.reviews.front();
    if (review && static_cast<std::size_t>(ply) < review->positions.size()) {
        const auto& before = review->positions[static_cast<std::size_t>(ply)];
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
    auto& b = board_;
    // What the analysis found, by side.
    if (b.reviews.empty() || !b.reviews.front().complete(b.game.moves.size())) return;
    const auto& review = b.reviews.front();
    const auto counts  = judgementCounts(b.game, b.judged);
    const auto side    = [&](int colour) {
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
        const auto how = review.depth > 0 ? trf("depth {}", review.depth)
                                          : trf("{:g} s per move", static_cast<double>(review.movetime_ms) / 1000.0);
        ImGui::SetTooltip("%s", trf("By {}, {}. Losses of 10, 20 and 30 % of the winning chances are an "
                                    "inaccuracy (?!), a mistake (?) and a blunder (??).",
                                    review.engine, how)
                                    .c_str());
    }
    ui::topic(guide::topics::GameAnalysis);
}

std::vector<const GameReview*> App::shownReviews() const {
    const auto& b = board_;
    std::vector<const GameReview*> out;
    const bool running = b.reviewing() && b.review_game_key == b.review_key;
    const auto task    = [&](const std::string& engine) -> const GameReview* {
        if (!running) return nullptr;
        for (const auto& t : b.review_tasks)
            if (t.engine == engine) return &t.state.live;
        return nullptr;
    };
    for (const auto& r : b.reviews) {
        const auto* live = task(r.engine);
        out.push_back(live ? live : &r);
    }
    // Those started that have no pass done yet.
    if (running) {
        for (const auto& t : b.review_tasks) {
            if (std::none_of(b.reviews.begin(), b.reviews.end(), [&](const auto& r) { return r.engine == t.engine; }))
                out.push_back(&t.state.live);
        }
    }
    return out;
}

const GameReview* App::shownReview() const {
    const auto all = shownReviews();
    return all.empty() ? nullptr : all.front();
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
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", TR("Back to the choice of the engines and the clocks, for another game."));
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
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("One clock for both engines. Untick it to give each its own, for time odds."));
    ui::topic(guide::topics::BoardPlay);
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
