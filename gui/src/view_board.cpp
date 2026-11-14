// The Board page: a game or a position, from a tournament, a PGN file or the clipboard, analysed by an engine of the
// library. Key A on every board opens its position here.

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include <tour/chess_text.hpp>

#include "app.hpp"
#include "i18n.hpp"
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

void App::pollBoard() {
    board_key_taken_ = false;
    board_.analyser.poll();
    if (board_.analysing && board_.analyser.state() == Analyser::State::Failed) board_.analysing = false;
    // The analysis follows the board while the page is shown, and waits meanwhile.
    if (board_.analysing && page_ == Page::Board) {
        board_.analyser.analyse(analysisPosition(board_.game, board_.chess960, board_.ply), {}, board_.new_game);
        board_.new_game = false;
    } else if (board_.analyser.state() == Analyser::State::Searching) {
        board_.analyser.halt();
    }
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
    // The tools, on the title's line.
    {
        const char* analyse = b.analysing ? TR("Stop the analysis") : TR("Analyse");
        const float width   = ImGui::CalcTextSize(analyse).x + ImGui::CalcTextSize("A").x + px(70);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(20), ImGui::GetContentRegionMax().x - width));
        if (ui::iconButton("board_analyse", b.analysing ? ui::Icon::Stop : ui::Icon::Analyse, analyse))
            setBoardAnalysis(!b.analysing);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("A: an engine of the library analyses the position."));
        ImGui::SameLine(0, px(4));
        ImGui::TextDisabled("A");
    }

    // Keys, unless a text field has them.
    const bool keys = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    const int total = static_cast<int>(b.game.moves.size());
    if (keys) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) b.ply = std::max(0, b.ply - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) b.ply = std::min(total, b.ply + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) b.ply = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End)) b.ply = total;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) b.selected.reset();
    }
    if (keys && boardKey()) setBoardAnalysis(!b.analysing);

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

    ImGui::BeginGroup();
    drawBoardColumn(size);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginChild("##board_side", ImVec2(0, 0));
    drawBoardAnalysis();
    drawBoardMoves();
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
    }

    // The evaluation the engines gave of their moves, when the game has them.
    std::vector<float> evals;
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
    }
    const float chart  = scored ? std::max(px(70), ImGui::GetContentRegionAvail().y * 0.3f) : 0.0f;
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
            if (ImGui::IsItemHovered() && m.score && !m.book) {
                ImGui::SetTooltip("%s/%d  %s", ui::formatScore(m.score).c_str(), m.depth,
                                  m.time_ms >= 0 ? ui::formatClock(m.time_ms).c_str() : "");
            }
            ImGui::SameLine();
        }
        if (!b.game.result.empty() && b.game.result != "*") ImGui::TextDisabled("%s", b.game.result.c_str());
    }
    b.followed = b.ply;
    ImGui::EndChild();

    if (scored) {
        if (const int clicked = plotEval("##board_eval", evals, b.ply, ImGui::GetContentRegionAvail().y); clicked >= 0)
            b.ply = std::clamp(clicked, 0, total);
    }
    ui::endCard();
}

}  // namespace fcg
