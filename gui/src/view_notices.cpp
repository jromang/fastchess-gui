// The actionable messages of the setup summary (notice.hpp, setup_checks.hpp) and the fixes they offer: the App
// carries them out here.

#include <algorithm>

#include <imgui.h>

#include "app.hpp"
#include "i18n.hpp"
#include "setup_checks.hpp"
#include "ui.hpp"

namespace fcg {

std::vector<Notice> App::setupNotices() {
    std::vector<Notice> notices;
    const auto append = [&](std::vector<Notice> more) {
        for (auto& n : more) notices.push_back(std::move(n));
    };
    append(threadNotices(setup_, engines_, topology_));
    append(optionNotices(setup_, engines_));
    // The tablebases, as their last check found them: each path is read on a worker thread.
    if (auto none = adjudicationFolderNotice(setup_)) notices.push_back(std::move(*none));
    for (const auto& use : tablebaseUses(setup_, engines_)) {
        if (const auto* check = tb_checks_.get(use.paths)) append(tablebaseNotices(use, *check));
    }
    return notices;
}

void App::drawSetupNotices(std::vector<Notice> notices, const char* id) {
    // While a tournament runs, the setup is locked, and the engines it plays are not changed.
    for (auto& n : notices) {
        std::erase_if(n.fixes, [&](const Fix& f) {
            switch (f.kind) {
                case Fix::Kind::LocateBook:
                case Fix::Kind::LocateTablebases:
                case Fix::Kind::LowerParticipantThreads:
                case Fix::Kind::LowerCommonThreads:
                case Fix::Kind::LowerTablebasePieces:
                    return run_.running();
                case Fix::Kind::LocateProgram:
                case Fix::Kind::LocateDir:
                case Fix::Kind::LowerEngineThreads:
                    return engineInUse(f.engine);
                default:
                    return false;
            }
        });
    }
    Fix clicked;
    if (drawNotices(notices, id, clicked)) applyFix(clicked);
}

void App::setStartFixes() {
    // A start refused names what the summary says: its errors' fixes, with the log, where fastchess said more.
    start_fixes_.clear();
    std::vector<Notice> errors = programNotices(setup_, engines_);
    if (auto book = bookNotice(setup_)) errors.push_back(std::move(*book));
    if (auto folder = adjudicationFolderNotice(setup_)) errors.push_back(std::move(*folder));
    if (const auto memory = setupMemory(setup_, engines_, topology_); memory.aboveInstalled())
        errors.push_back(memoryNotice(setup_, engines_, memory));
    for (const auto& n : errors) {
        for (const auto& f : n.fixes) {
            if (std::find(start_fixes_.begin(), start_fixes_.end(), f) == start_fixes_.end()) start_fixes_.push_back(f);
        }
    }
    start_fixes_.push_back(openLogFix());
    start_fixes_for_ = start_error_;
}

void App::setFinishFixes() {
    finish_fixes_.clear();
    if (const auto engine = libraryEngine(engines_, stopped_by_); !engine.empty())
        finish_fixes_.push_back(openEngineFix(engine));
    finish_fixes_.push_back(openLogFix());
    finish_fixes_for_ = finish_message_;
}

void App::drawMessage(const std::string& text, const std::vector<Fix>& fixes, const std::string& fixes_for, bool error,
                      const char* id) {
    Notice n{error ? Notice::Level::Error : Notice::Level::Warning, text, {}, {}};
    if (fixes_for == text) {
        n.fixes = fixes;
    } else if (error) {
        n.fixes = {openLogFix()};
    }
    if (const auto* fix = drawNotice(n, id)) applyFix(*fix);
}

void App::selectEngine(const std::string& name) {
    for (std::size_t i = 0; i < engines_.size(); ++i) {
        if (engines_[i].name != name) continue;
        page_            = Page::Engines;
        selected_engine_ = static_cast<int>(i);
        engine_marked_.clear();
        return;
    }
}

void App::applyFix(const Fix& fix) {
    const auto engine =
        std::find_if(engines_.begin(), engines_.end(), [&](const auto& e) { return e.name == fix.engine; });
    const bool known = engine != engines_.end();
    switch (fix.kind) {
        case Fix::Kind::OpenEngine:
            selectEngine(fix.engine);
            break;
        case Fix::Kind::LocateProgram:
        case Fix::Kind::LocateDir:
            // The dialog's answer goes to the engine selected.
            if (!known) break;
            selected_engine_ = static_cast<int>(engine - engines_.begin());
            engine_marked_.clear();
            if (fix.kind == Fix::Kind::LocateProgram) {
                dialogs_.openFile(Dialogs::Purpose::EngineCommand, {}, engine->cmd);
            } else {
                dialogs_.openFolder(Dialogs::Purpose::EngineDir, engine->dir);
            }
            break;
        case Fix::Kind::LocateBook:
            dialogs_.openFile(Dialogs::Purpose::Book,
                              {{TR("Books (EPD, PGN, Polyglot)"), "epd;pgn;bin"}, {TR("All files"), "*"}}, setup_.book);
            break;
        case Fix::Kind::LocateTablebases: {
            const auto dirs = splitTablebasePath(setup_.tb_paths);
            dialogs_.openFolder(Dialogs::Purpose::TablebasesFolder, dirs.empty() ? std::string() : dirs.front());
            break;
        }
        case Fix::Kind::LowerEngineThreads:
            if (!known) break;
            engine->setOption("Threads", std::to_string(fix.value));
            pruneOptions(*engine);  // the engine's default, when that is what it comes back to
            storage_.saveEngines(engines_);
            Console::instance().add(trf("Threads of {} set to {} in the engine library.", fix.engine, fix.value));
            break;
        case Fix::Kind::LowerParticipantThreads:
            if (fix.index >= 0 && fix.index < static_cast<int>(setup_.participants.size()) &&
                setup_.participants[fix.index].name == fix.engine) {
                setup_.participants[fix.index].threads = fix.value;
                saveSetupSoon();
            }
            break;
        case Fix::Kind::LowerCommonThreads:
            setup_.common_threads = fix.value;
            saveSetupSoon();
            break;
        case Fix::Kind::LowerTablebasePieces:
            setup_.tb_pieces = fix.value;
            saveSetupSoon();
            break;
        case Fix::Kind::OpenLog:
            page_ = Page::Log;
            break;
        case Fix::Kind::OpenFolder:
            ui::openPath(fix.path);
            break;
    }
}

}  // namespace fcg
