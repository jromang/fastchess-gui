// The command palette (Ctrl+K): a page, a panel, an engine, a book, a tournament or an action, found by typing a few of
// its letters. And F1: the guide at the section of the page shown, as the `?` beside the page titles.

#include <algorithm>
#include <string_view>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "board_text.hpp"
#include "i18n.hpp"
#include "layout.hpp"
#include "palette.hpp"
#include "participants.hpp"
#include "review_stats.hpp"
#include "setup_checks.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// What a label shows, without its "##" or "###" id.
std::string shown(const char* label) {
    const std::string_view text(label);
    return std::string(text.substr(0, text.find("##")));
}

}  // namespace

const char* App::guideSection() const {
    using namespace guide::topics;
    switch (page_) {
        case Page::Home:
            return Home;
        case Page::NewMatch:
            return NewTournament;
        case Page::Tournament:
            if (!run_->tournament()) return Running;
            switch (shown_tab_) {
                case Tab::Overview:
                    return Overview;
                case Tab::Standings:
                    return Standings;
                case Tab::Bracket:
                    return Schedule;
                case Tab::Schedule:
                    return Changing;
                case Tab::Live:
                    return Live;
                case Tab::Games:
                    return Games;
                case Tab::Openings:
                    return OpeningsTab;
                case Tab::Broadcast:
                    return Broadcast;
            }
            return Running;
        case Page::Engines:
            return Engines;
        case Page::Books:
            return Books;
        case Page::Board:
            return Board;
        case Page::Suites:
            return Suites;
        case Page::Ratings:
            return Ratings;
        case Page::Log:
            return Report;
        case Page::Settings:
            break;
    }
    return Window;
}

void App::toggleGuide() {
    // Over the panel itself, F1 closes it; else it opens at what is under the mouse, or at the page.
    if (guide_.isOpen() && guide_.hovered()) {
        guide_.close();
        return;
    }
    const char* topic = ui::hoveredTopic();
    guide_.toggle(topic ? topic : guideSection());
}

// The actions possible now first, then the pages, the panels of the tournament and the tabs of the ratings, the
// tournaments, the engines and the books.
std::vector<App::PaletteEntry> App::paletteEntries() {
    std::vector<PaletteEntry> out;
    const auto add = [&](std::string label, std::string where, std::function<void()> run) {
        out.push_back({std::move(label), std::move(where), std::move(run)});
    };
    const auto* t          = run_->tournament();
    const std::string name = t ? t->spec().name : std::string();

    // Actions, as their buttons allow them.
    if (run_->running()) {
        if (run_->paused()) {
            if (!run_->stopping() && !run_->stoppingWhenIdle())
                add(TR("Resume"), name, [this] { setRunPaused(false); });
        } else if (!run_->stopping()) {
            add(TR("Pause"), name, [this] { setRunPaused(true); });
        }
        if (!run_->stopping()) {
            add(TR("Finish the running games, then stop"), name, [this] { stopRun(false); });
            add(TR("Stop now (running games are lost)"), name, [this] { stopRun(true); });
        }
    } else {
        if (unfinished_) add(TR("Resume"), unfinished_->spec.name, [this] { resumeRun(); });
        if (!queue_.empty()) add(TR("Start now"), queue_.front().label, [this] { startFromQueue(); });
    }
    // Pause all and Resume all, as the home page offers them.
    if (allPaused()) {
        add(TR("Resume all"), TR("Tournaments"), [this] { resumeAll(); });
    } else if (canPauseAll()) {
        add(TR("Pause all"), TR("Tournaments"), [this] { pauseAll(); });
    }
    // The last matches finished, relaunched with another engine A from their row of the home page.
    {
        const auto busy = run_->running() && t ? t->dir() : std::filesystem::path();
        for (const auto* s : home::lastFinished(tournaments_, home::LAST_FINISHED, busy)) {
            if (s->spec.format != tour::Format::Match || s->spec.participants.size() < 2) continue;
            add(trf("Relaunch {} with…", s->spec.name), TR("Last finished"), [this, dir = s->dir] {
                page_             = Page::Home;
                relaunch_request_ = dir;
            });
        }
    }
    // Clone: the tournament shown, and the last finished, as their headers and rows offer it.
    {
        std::vector<std::pair<std::filesystem::path, std::string>> sources;
        if (t) sources.emplace_back(t->dir(), name);
        const auto busy = run_->running() && t ? t->dir() : std::filesystem::path();
        for (const auto* s : home::lastFinished(tournaments_, home::LAST_FINISHED, busy)) {
            if (!t || s->dir != t->dir()) sources.emplace_back(s->dir, s->spec.name);
        }
        for (const auto& source : sources) {
            // Its date beside it: a tournament cloned is often named as the one it came from.
            if (canClone(source.first)) {
                add(trf("Clone {}", source.second), fmt::format("{} · {}", TR("Tournaments"), folderDate(source.first)),
                    [this, dir = source.first] { cloneTournament(dir); });
            }
        }
    }
    // The engines that cannot start, as their card offers.
    if (t) {
        const bool editable = t->spec().format == tour::Format::RoundRobin ||
                              t->spec().format == tour::Format::Gauntlet || t->spec().format == tour::Format::TeamMatch;
        for (const auto& e : t->startFailures().engines()) {
            const auto* p = t->spec().participant(e.participant);
            if (!p || !p->active) continue;
            if (run_->running() && !run_->stopping() && !e.trying) {
                add(trf("Try again to start {}", e.name), name,
                    [this, id = e.participant] { run_->tryEngineAgain(id); });
            }
            if (editable) {
                add(trf("Withdraw {}…", e.name), name,
                    [this, id = e.participant] { openParticipantEdit(tour::Edit::Kind::Withdraw, id); });
                add(trf("Replace {}…", e.name), name,
                    [this, id = e.participant] { openParticipantEdit(tour::Edit::Kind::ReplaceParticipant, id); });
            }
        }
    }
    // The other tournaments open, a tab each: shown, or paused and resumed from here.
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        if (i == shown_run_) continue;
        const auto* other = runs_[i].run->tournament();
        if (!other) continue;
        const auto& other_name = other->spec().name;
        add(trf("Show {}", other_name), TR("Tournaments"), [this, i] {
            showRun(i);
            page_ = Page::Tournament;
        });
        auto& r = *runs_[i].run;
        if (r.running() && !r.stopping()) {
            if (r.paused()) {
                add(TR("Resume"), other_name, [this, i] { runs_[i].run->setPaused(false); });
            } else {
                add(TR("Pause"), other_name, [this, i] { runs_[i].run->setPaused(true); });
            }
        }
    }
    const bool ready = setupChosen() && !setupBadSprt();
    if (ready && !startPending("start")) add(TR("Start"), TR("New tournament"), [this] { startRun(); });
    if (ready) add(TR("Add to the queue"), TR("New tournament"), [this] { enqueue(); });
    // The fixes the setup summary offers (Lower Threads, Lower to N pieces, Locate…), named after their engine. The
    // engines open from their own entries, below.
    {
        std::vector<Notice> notices = programNotices(setup_, engines_);
        if (auto book = bookNotice(setup_)) notices.push_back(std::move(*book));
        for (auto& n : setupNotices()) notices.push_back(std::move(n));
        std::vector<Fix> seen;
        for (const auto& n : notices) {
            for (const auto& f : n.fixes) {
                const bool generic =
                    f.kind == Fix::Kind::OpenEngine || f.kind == Fix::Kind::OpenLog || f.kind == Fix::Kind::OpenFolder;
                const bool setup = f.kind != Fix::Kind::LocateProgram && f.kind != Fix::Kind::LocateDir &&
                                   f.kind != Fix::Kind::LowerEngineThreads;
                if (generic || (!setup && engineInUse(f.engine)) ||
                    std::find(seen.begin(), seen.end(), f) != seen.end())
                    continue;
                seen.push_back(f);
                add(f.engine.empty() ? f.label : fmt::format("{}: {}", f.engine, f.label), TR("New tournament"),
                    [this, f] { applyFix(f); });
            }
        }
    }
    if (t && !games_.empty()) {
        add(TR("Export for a rating list…"), name, [this] {
            page_        = Page::Tournament;
            tab_request_ = Tab::Games;
            openSubmissionExport();
        });
    }
    // The games of the openings flagged unbalanced as they ended.
    if (t && t->spec().unbalanced_cp > 0 &&
        std::any_of(games_.begin(), games_.end(), [](const GameRecord& g) { return g.unbalanced != 0; })) {
        add(TR("Show the games from unbalanced openings"), TR("Games"), [this] { showUnbalancedGames(); });
        add(TR("Show the unbalanced openings"), TR("Openings"), [this] {
            page_                = Page::Tournament;
            tab_request_         = Tab::Openings;
            opening_show_        = UNBALANCED_OPENINGS;
            opening_order_dirty_ = true;
        });
    }
    // The big screen: the game followed, filling the window.
    if (t && run_->observer()) {
        if (big_.on) {
            add(TR("Leave the big screen"), TR("Live"), [this] { leaveBigScreen(); });
        } else {
            add(TR("Big screen"), TR("Live"), [this] { startBigScreen(live_slot_); });
        }
    }
    // The game shown in the Games tab, and the game followed in the Live tab: their result set by hand.
    if (t && selected_game_ >= 0 && selected_game_ < static_cast<int>(games_.size())) {
        const auto id = games_[static_cast<std::size_t>(selected_game_)].game_id;
        add(trf("Set the result of game {}…", id), TR("Games"), [this, id] {
            page_           = Page::Tournament;
            tab_request_    = Tab::Games;
            open_game_menu_ = id;
        });
    }
    if (t && run_->running() && live_slot_ >= 0 && live_slot_ < static_cast<int>(slot_views_.size()) &&
        slot_views_[static_cast<std::size_t>(live_slot_)].state.active) {
        const auto& st = slot_views_[static_cast<std::size_t>(live_slot_)].state;
        add(trf("End game {} with a result…", st.game_id), TR("Live"), [this, slot = live_slot_] {
            page_          = Page::Tournament;
            tab_request_   = Tab::Live;
            open_end_menu_ = slot + 1;
        });
    }
    if (t && !isMatch()) {
        const auto copy = [&](const char* how, ui::CopyAs as) {
            add(fmt::format("{}: {}", TR("Copy"), how), TR("Standings"), [this, as] { copyStandings(as); });
        };
        copy(TR("As text"), ui::CopyAs::Text);
        copy(TR("As BBCode, for a forum"), ui::CopyAs::Bbcode);
        if (t->spec().format == tour::Format::Gauntlet) {
            copy(TR("Gauntlet table as text"), ui::CopyAs::GauntletText);
            copy(TR("Gauntlet table as BBCode, for a forum"), ui::CopyAs::GauntletBbcode);
        }
    }
    const auto import       = fmt::format("{} · {}", TR("Engines"), TR("Import…"));
    const auto engines_page = [this](std::function<void()> then) {
        return [this, then = std::move(then)] {
            page_ = Page::Engines;
            then();
        };
    };
    add(TR("An engine list…"), import, engines_page([this] { askEngineList(); }));
    add(TR("Every engine of a folder…"), import,
        engines_page([this] { dialogs_.openFolder(Dialogs::Purpose::ImportEngineFolder); }));
    if (!engineFolder().empty())
        add(TR("Refresh: the new engines of the folder"), import, engines_page([this] { refreshEngineFolder(); }));
    add(TR("A rating list, for the reference Elo…"), import, engines_page([this] { openRatingImport(); }));
    // The engines of a machine, played from this computer.
    for (const auto& m : machines_) {
        const auto name = m.name.empty() ? m.host : m.name;
        add(trf("Add an engine from {}…", name), TR("Engines"), engines_page([this, id = m.id] {
                openRemotePicker(RemotePicker::Purpose::AddEngine, id);
            }));
        add(trf("Every engine of a folder on {}…", name), import, engines_page([this, id = m.id] {
                openRemotePicker(RemotePicker::Purpose::ImportFolder, id);
            }));
    }
    if (const auto unchecked = uncheckedEngines(); !unchecked.empty()) {
        add(trf("Check those not checked ({})", unchecked.size()), TR("Engines"),
            engines_page([this, unchecked] { queueChecks(unchecked); }));
    }
    // The engines whose rebuilt program's new read found changes: their card, the choices for all, the dismissal.
    for (const auto& e : engines_) {
        if (!e.changes) continue;
        const auto& n = e.name;
        add(trf("What changed: {}", n), TR("Engines"), [this, n] { showEngineChanges(n); });
        if (choicesLeft(*e.changes) > 0) {
            add(trf("Keep my values: {}", n), TR("Engines"), [this, n] { settleEngineChanges(n, ValueChoice::Keep); });
            add(trf("Use the new defaults: {}", n), TR("Engines"),
                [this, n] { settleEngineChanges(n, ValueChoice::NewDefault); });
        }
        add(trf("Dismiss what changed: {}", n), TR("Engines"), [this, n] { settleEngineChanges(n, std::nullopt); });
    }
    if (!resumed_programs_.empty() && run_->running())
        add(TR("Continue with the changed programs"), name, [this] { resumed_programs_.clear(); });
    // The console of the engine shown, or the one open.
    if (page_ == Page::Engines && selected_engine_ >= 0 && selected_engine_ < static_cast<int>(engines_.size())) {
        add(TR("Console…"), engines_[static_cast<std::size_t>(selected_engine_)].name,
            [this] { openEngineConsole(selected_engine_); });
    }
    if (engine_console_) add(TR("Close the console"), engine_console_->engine, [this] { closeEngineConsole(); });

    // Pages, the panels of the tournament open, and the tabs of the ratings.
    const auto page = [this](Page p) {
        return [this, p] {
            if (p == Page::Home) tournaments_dirty_ = true;
            if (p == Page::Ratings && page_ != Page::Ratings) game_db_dirty_ = true;
            page_ = p;
        };
    };
    add(TR("Tournaments"), "", page(Page::Home));
    add(TR("New tournament"), "", page(Page::NewMatch));
    // The profiles of each section, applied to the setup, and the templates, loaded.
    {
        for (const auto section : SETUP_SECTIONS) {
            for (const auto& p : profiles_.list(section)) {
                add(p.label(), fmt::format("{} · {}", TR("Profile"), sectionTitle(section)),
                    [this, section, name = p.name] {
                        page_ = Page::NewMatch;
                        if (const auto* profile = profiles_.find(section, name)) applySetupProfile(section, *profile);
                    });
            }
        }
        // Each row of the setup, opened in place.
        const auto row = [&](const char* title, SetupRow r) {
            add(title, TR("New tournament"), [this, r] {
                page_          = Page::NewMatch;
                setup_open_[r] = true;
            });
        };
        for (const auto section : SETUP_SECTIONS) row(sectionTitle(section), static_cast<SetupRow>(section));
        row(TR("Concurrency"), RowConcurrency);
        row(TR("PGN and misc"), RowPgn);
        // A team match: the participants selected to a team, or the halves of the list again.
        if (setup_.format == TournamentFormat::TeamMatch && !setup_.participants.empty()) {
            std::vector<int> picked;
            for (int i = 0; i < static_cast<int>(setup_.participants.size()); ++i) {
                if (participants_selected_.Contains(static_cast<ImGuiID>(i))) picked.push_back(i);
            }
            for (int team = 0; team < 2 && !picked.empty(); ++team) {
                add(team == 0 ? TR("Put the participants selected in team A.")
                              : TR("Put the participants selected in team B."),
                    TR("Team match"), [this, picked, team] {
                        page_ = Page::NewMatch;
                        setTeam(setup_, picked, team);
                        saveSetupSoon();
                    });
            }
            add(TR("The teams made again from the list: its upper half against its lower half."), TR("Team match"),
                [this] {
                    page_ = Page::NewMatch;
                    splitTeams(setup_);
                    saveSetupSoon();
                });
        }
        add(TR("Save the setup as a template…"), TR("New tournament"), [this] {
            page_               = Page::NewMatch;
            open_save_template_ = true;
        });
        listTemplates();
        for (const auto& name : template_names_) {
            add(name, TR("Template"), [this, name] {
                page_ = Page::NewMatch;
                loadTemplate(name);
            });
        }
    }
    if (t) {
        // Every panel of the workspace: shown (docked again if it was closed) and brought to the front. Then the
        // layouts, ready-made and saved.
        std::vector<std::string> saved;
        for (const auto& l : layouts_) saved.push_back(l.name);
        for (const auto& c : layout::commands(panelsAvailable(), saved)) {
            if (c.kind == layout::Command::Kind::Show) {
                add(c.label, name, [this, panel = c.target] {
                    page_ = Page::Tournament;
                    showPanel(panel);
                });
            } else if (c.kind == layout::Command::Kind::Preset) {
                add(c.label, TR("Layout"), [this, key = c.target] { applyLayout(key); });
            } else {
                // A layout of the user's, even if named as the key of a ready-made one.
                add(c.label, TR("Layout"), [this, name = c.target] {
                    for (const auto& l : layouts_)
                        if (l.name == name) applyLayout(l);
                });
            }
        }
        add(TR("Save the layout as…"), TR("Layout"), [this] {
            page_ = Page::Tournament;
            layout_name_.clear();
            open_save_layout_ = true;
        });
        add(TR("Default layout"), TR("Layout"), [this] { applyLayout(layout::preset(layout::Preset::Tabs)); });
    }
    add(TR("Engines"), "", page(Page::Engines));
    add(TR("Books"), "", page(Page::Books));
    for (const bool symmetrical : {false, true}) {
        add(fmt::format("{}: {}", TR("Generate Chess324 book"),
                        symmetrical ? TR("Only the 18 symmetrical ones…") : TR("All 324 positions…")),
            TR("Books"), [this, symmetrical] {
                page_ = Page::Books;
                askChess324(symmetrical);
            });
    }
    add(TR("Build a Polyglot book…"), TR("Books"), [this] {
        page_ = Page::Books;
        openBookBuild();
    });
    if (book_build_.busy()) {
        add(TR("Cancel the Polyglot book being built"), std::filesystem::path(book_build_.bin()).filename().string(),
            [this] { book_build_.cancel(); });
    }
    add(TR("Board"), "", page(Page::Board));
    add(board_.analysing ? TR("Stop the analysis") : TR("Analyse"), TR("Board"), [this] {
        page_ = Page::Board;
        setBoardAnalysis(!board_.analysing);
    });
    if (board_.analyser.active() || board_.threat.active())
        add(TR("Unload the engine"), TR("Board"), [this] { unloadBoardEngines(); });
    add(TR("Copy FEN"), TR("Board"), [this] { ImGui::SetClipboardText(boardFen().c_str()); });
    add(TR("Open a PGN…"), TR("Board"), [this] {
        page_ = Page::Board;
        dialogs_.openFile(Dialogs::Purpose::BoardPgn, {{"PGN", "pgn"}}, storage_.runsDir().string());
    });
    add(TR("Paste"), TR("Board"), [this] {
        page_ = Page::Board;
        pasteOnBoard();
    });
    add(TR("Edit the position"), TR("Board"), [this] {
        page_ = Page::Board;
        openPositionEditor();
    });
    if (board_.match && board_.match_state.running) {
        add(TR("Stop the game"), TR("Board"), [this] { board_.match->stop(); });
    } else {
        add(TR("Play a game…"), TR("Board"), [this] {
            page_            = Page::Board;
            board_.play_open = true;
        });
    }
    if (board_.reviewing()) {
        add(TR("Cancel the analysis of the game"), TR("Board"), [this] { cancelGameReview(); });
    } else if (!board_.game.moves.empty()) {
        add(TR("Analyse the game"), TR("Board"), [this] {
            page_ = Page::Board;
            startGameReview();
        });
    }
    add(TR("Copy PGN"), TR("Board"),
        [this] { ImGui::SetClipboardText(gamePgn(board_.tree, board_.chess960).c_str()); });
    add(board_.threat_on ? TR("Hide the threat") : TR("Show the threat"), TR("Board"), [this] {
        page_ = Page::Board;
        setBoardThreat(!board_.threat_on);
    });
    if (!board_.reviews.empty()) {
        add(TR("Next marked move"), TR("Board"), [this] {
            page_ = Page::Board;
            if (const int to = judgedPly(1); to >= 0) showBoardLine(-1, to);
        });
        add(TR("Previous marked move"), TR("Board"), [this] {
            page_ = Page::Board;
            if (const int to = judgedPly(-1); to >= 0) showBoardLine(-1, to);
        });
        add(TR("Copy PGN with the analysis"), TR("Board"), [this] {
            ImGui::SetClipboardText(
                pgnWithAnalysis(board_.tree, board_.chess960, board_.reviews.front(), board_.judged).c_str());
        });
    }
    add(TR("The game's review"), TR("Board"), [this] {
        page_           = Page::Board;
        board_.open_tab = 3;
    });
    add(appearance::current().figurines ? TR("Letters in the notation") : TR("Figurines in the notation"), TR("Board"),
        [this] {
            auto s      = appearance::current();
            s.figurines = !s.figurines;
            applyAppearance(s);
        });
    if (board_.line >= 0) {
        add(TR("Back to the game"), TR("Board"), [this] { showBoardLine(-1, boardMainPly()); });
    }
    if (live_slot_ >= 0 && live_slot_ < static_cast<int>(slot_views_.size()) &&
        slot_views_[static_cast<std::size_t>(live_slot_)].state.has_game) {
        add(TR("Analyse the game followed"), name, [this] { analyseLiveGame(live_slot_); });
    }
    if (replay_) add(TR("Analyse the game shown"), name, [this] { analyseReplay(); });
    add(TR("Test suites"), "", page(Page::Suites));
    add(TR("Ratings"), "", page(Page::Ratings));
    const auto ratings_tab = [&](const char* label, int index) {
        add(shown(label), TR("Ratings"), [this, index, go = page(Page::Ratings)] {
            go();
            ratings_tab_request_ = index;
        });
    };
    ratings_tab(TR("Rating list###rt_list"), 0);
    ratings_tab(TR("Engine###rt_engine"), 1);
    ratings_tab(TR("Head to head###rt_h2h"), 2);
    ratings_tab(TR("Progress###rt_progress"), 3);
    add(TR("Log"), "", page(Page::Log));
    add(TR("Settings"), "", page(Page::Settings));
    add(TR("Copy the version"), TR("Settings"), [this] { copyVersion(); });
    add(TR("Report a problem…"), TR("Settings"), [this] { reportProblem(); });
    add(TR("Open the profile folder"), TR("Settings"), [this] { ui::openPath(storage_.root()); });
    add(TR("Open the tournaments folder"), TR("Settings"), [this] { ui::openPath(storage_.runsDir()); });
    add(TR("Change the tournaments folder…"), TR("Settings"), [this] { applyFix(changeTournamentsFolderFix()); });
    if (const auto* release = update::notice(updates_, FCG_VERSION)) {
        add(trf("Download page of fastchess-gui {}", release->version), TR("Settings"),
            [url = release->url] { SDL_OpenURL(url.c_str()); });
    }
    add(TR("Software rendering"), TR("Settings"), page(Page::Settings));  // the checkbox and what draws the window
    // Appearance: its section of Settings, then its choices made at once (the board of the theme in use).
    add(TR("Appearance: board colours, pieces, move sounds"), TR("Settings"), [this, go = page(Page::Settings)] {
        go();
        scroll_to_appearance_ = true;
    });
    for (const auto& p : appearance::boardPresets()) {
        add(trf("Board colours: {}", TR(p.name)), TR("Settings"), [this, &p] {
            auto s       = appearance::current();
            auto& board  = s.board(ui::currentTheme());
            board.preset = p.id;
            board.custom = p.colours;
            applyAppearance(s);
        });
    }
    for (const auto& p : appearance::pieceSets()) {
        add(trf("Pieces: {}", p.name), TR("Settings"), [this, id = p.id] {
            auto s   = appearance::current();
            s.pieces = id;
            applyAppearance(s);
        });
    }
    // The remote machines: their section of Settings, a new one, and each one's test.
    add(TR("Machines"), TR("Settings"), [this, go = page(Page::Settings)] {
        go();
        scroll_to_machines_ = true;
    });
    add(TR("Add a machine"), TR("Settings"), [this, go = page(Page::Settings)] {
        go();
        addMachine();
        scroll_to_machines_ = true;
    });
    for (const auto& m : machines_) {
        add(trf("Test the connection to {}", m.name), TR("Machines"), [this, go = page(Page::Settings), id = m.id] {
            go();
            machine_open_       = id;
            scroll_to_machines_ = true;
            startMachineJob(id, MachineJob::Kind::Test);
        });
        if (!m.engines.empty()) {
            add(trf("Check the engines on {}", m.name), TR("Machines"), [this, go = page(Page::Settings), id = m.id] {
                go();
                machine_open_                      = id;
                scroll_to_machines_                = true;
                machine_activity_[id].open_engines = true;
                startMachineJob(id, MachineJob::Kind::Engines);
            });
        }
        // The library's engines: copied to the machine when they have no path there, replaced when the build there
        // is another one.
        for (const auto& e : engines_) {
            if (e.inactive) continue;
            const auto* there = m.engine(e.name);
            if (!there || there->path.empty()) {
                add(trf("Copy {} to {}", e.name, m.name), TR("Machines"),
                    [this, id = m.id, name = e.name] { offerEngineCopy(id, name); });
            } else if (!there->remote_hash.empty() && there->remote_hash != there->local_hash) {
                add(trf("Replace {} on {} with this computer's build", e.name, m.name), TR("Machines"),
                    [this, id = m.id, name = e.name] { offerEngineCopy(id, name); });
            }
        }
    }
    add(TR("Pieces: your own folder…"), TR("Settings"),
        [this] { dialogs_.openFolder(Dialogs::Purpose::PieceFolder, appearance::current().piece_folder); });
    add(appearance::current().sounds ? TR("Turn the move sounds off") : TR("Turn the move sounds on"), TR("Settings"),
        [this] {
            auto s   = appearance::current();
            s.sounds = !s.sounds;
            applyAppearance(s);
        });
    add(appearance::current().engine_logos ? TR("Hide the engines' logos") : TR("Show the engines' logos"),
        TR("Settings"), [this] {
            auto s         = appearance::current();
            s.engine_logos = !s.engine_logos;
            applyAppearance(s);
        });
    // The logo of the engine whose card is open.
    if (page_ == Page::Engines && selected_engine_ >= 0 && selected_engine_ < static_cast<int>(engines_.size())) {
        add(trf("Choose a logo for {}…", engines_[static_cast<std::size_t>(selected_engine_)].name), TR("Engines"),
            [this] {
                dialogs_.openFile(Dialogs::Purpose::EngineLogo,
                                  {{TR("Pictures (PNG, JPEG, SVG)"), "png;jpg;jpeg;svg"}, {TR("All files"), "*"}});
            });
    }
    add(setup_.nav_buttons ? TR("Hide the navigation buttons under the boards")
                           : TR("Show the navigation buttons under the boards"),
        TR("Settings"), [this] {
            setup_.nav_buttons = !setup_.nav_buttons;
            saveSetupSoon();
        });

    // The tournaments of the home page: one opens as its Open button does.
    for (const auto& s : tournaments_) {
        add(s.spec.name, fmt::format("{} · {}", TR("Tournament"), folderDate(s.dir)), [this, dir = s.dir] {
            openTournament(dir);
            if (!start_error_.empty()) page_ = Page::Home;  // where the reason is shown
        });
    }
    // The engines, each on its card of the Engines page, and the books.
    for (std::size_t i = 0; i < engines_.size(); ++i) {
        const auto& e = engines_[i];
        add(e.name, e.inactive ? fmt::format("{} · {}", TR("Engines"), TR("Inactive")) : std::string(TR("Engines")),
            [this, i] {
                page_            = Page::Engines;
                selected_engine_ = static_cast<int>(i);
                engine_marked_.clear();
            });
    }
    for (std::size_t i = 0; i < books_.size(); ++i) {
        add(books_[i].name, TR("Books"), [this, i] {
            page_          = Page::Books;
            selected_book_ = static_cast<int>(i);
            book_item_     = -1;
        });
    }
    // The guide's sections, by their titles in the interface's language, and its search with what was typed.
    if (const auto* en = guide::doc("en")) {
        const auto code = guide::currentCode();
        for (std::size_t i = 1; i < en->headings.size(); ++i) {
            const auto s = guide::shown(code, i);
            if (!s.heading) continue;
            add(trf("Guide: {}", s.heading->title), TR("Guide"), [this, id = en->headings[i].id] { guide_.open(id); });
        }
    }
    if (!palette_query_.empty())
        add(trf("Search the guide for “{}”", palette_query_), TR("Guide"),
            [this, q = palette_query_] { guide_.search(q); });
    return out;
}

void App::drawPalette() {
    // F1: the guide at what is under the mouse, or at the page shown, also from a text field (none uses it).
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) toggleGuide();

    // Ctrl+K, but not while a text field has the keys, nor over another popup or dialog: opening this one would close
    // it.
    const bool ctrl_k = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K);
    if (ctrl_k && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        palette_query_.clear();
        palette_request_ = true;
    }
    if (palette_request_) {
        palette_request_   = false;
        palette_selected_  = 0;
        tournaments_dirty_ = true;  // the list of the home page, read again
        ImGui::OpenPopup("##palette");
    }

    // A column in the middle of the window, near its top.
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.12f),
        ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(std::min(px(640), viewport->WorkSize.x - px(32)), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(12)));
    const bool open = ImGui::BeginPopup("##palette", ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();
    if (!open) return;
    refreshTournaments();

    std::function<void()> chosen;
    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (ctrl_k && !ImGui::IsWindowAppearing());

    // The search field keeps the keys: Up and Down move in the list (the history callback keeps them from the
    // navigation), Enter runs the line chosen.
    // Not while the mouse is down: it may be clicking a line, which needs the press and the release.
    if (ImGui::IsWindowAppearing() || (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)))
        ImGui::SetKeyboardFocusHere();
    char buffer[256];
    const auto n = std::min(palette_query_.size(), sizeof(buffer) - 1);
    std::copy_n(palette_query_.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##palette_search", TR("A page, an engine, a tournament, an action…"), buffer,
                                 sizeof(buffer), ImGuiInputTextFlags_CallbackHistory,
                                 [](ImGuiInputTextCallbackData*) { return 0; })) {
        palette_query_    = buffer;
        palette_selected_ = 0;
    }

    const auto entries = paletteEntries();
    std::vector<std::string> labels, wheres;
    labels.reserve(entries.size());
    wheres.reserve(entries.size());
    for (const auto& e : entries) {
        labels.push_back(e.label);
        wheres.push_back(e.where);
    }
    const auto order  = palette::rank(palette_query_, labels, &wheres);
    const int matches = static_cast<int>(order.size());

    bool moved = false;
    if (matches > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) palette_selected_++, moved = true;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) palette_selected_--, moved = true;
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) palette_selected_ += 10, moved = true;
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) palette_selected_ -= 10, moved = true;
    }
    palette_selected_ = std::clamp(palette_selected_, 0, std::max(0, matches - 1));
    if (matches > 0 && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
        chosen = entries[static_cast<std::size_t>(order[static_cast<std::size_t>(palette_selected_)])].run;

    ImGui::Spacing();
    if (matches == 0) {
        ImGui::TextDisabled("%s", TR("Nothing matches."));
    } else {
        // Twelve lines in sight, the others scrolled to.
        const float line = ImGui::GetTextLineHeightWithSpacing();
        ImGui::BeginChild("##palette_list", ImVec2(0, line * static_cast<float>(std::min(matches, 12))),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
        auto* dl          = ImGui::GetWindowDrawList();
        const ImU32 text  = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const float gap   = px(16);
        ImGuiListClipper clipper;
        clipper.Begin(matches);
        if (moved) clipper.IncludeItemByIndex(palette_selected_);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& e = entries[static_cast<std::size_t>(order[static_cast<std::size_t>(row)])];
                ImGui::PushID(row);
                if (ImGui::Selectable("##line", row == palette_selected_)) chosen = e.run;
                if (moved && row == palette_selected_) ImGui::SetScrollHereY();
                ImGui::PopID();

                // The name on the left, where it belongs on the right; a long name stops before it.
                const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
                const float pad   = ImGui::GetStyle().FramePadding.x * 0.5f;
                const float where = e.where.empty() ? 0.0f : ImGui::CalcTextSize(e.where.c_str()).x;
                if (where > 0) dl->AddText(ImVec2(max.x - pad - where, min.y), muted, e.where.c_str());
                const ImVec4 clip(min.x, min.y, max.x - pad - where - (where > 0 ? gap : 0.0f), max.y);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + pad, min.y), text, e.label.c_str(),
                            nullptr, 0.0f, &clip);
            }
        }
        ImGui::EndChild();
    }

    if (chosen) close = true;
    if (close) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    // Once the palette is closed: an action may open a popup or a dialog of its own.
    if (chosen) chosen();
}

}  // namespace fcg
