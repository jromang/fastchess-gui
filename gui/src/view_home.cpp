// The home page, a control room: what runs and what needs the tester first (the cores in use, an engine that cannot
// start, the tournament an error stopped, a drive too full), then the card of the running tournament, the queue and
// when it all ends, the last tournaments finished with their result, the health of the last 24 hours, and every
// tournament of the profile.

#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <unordered_map>

#include <imgui.h>

#include <implot.h>

#include <tour/budget.hpp>

#include "app.hpp"
#include "console.hpp"
#include "home.hpp"
#include "i18n.hpp"
#include "memory.hpp"
#include "remote_run.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using home::endTime;
using home::formatLabel;
using home::sectionTitle;
using home::statusLabel;

// The width ui::iconButton() gives a button labelled so.
float iconButtonWidth(const char* label) {
    const auto& style = ImGui::GetStyle();
    const float text  = ImGui::CalcTextSize(label, nullptr, true).x;
    return style.FramePadding.x * 2 + ImGui::GetFrameHeight() * 0.42f +
           (text > 0 ? style.ItemInnerSpacing.x + text : 0);
}

void tooltip(const char* text) {
    if (!ImGui::BeginItemTooltip()) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

}  // namespace

// ---------------------------------------------------------------------------
// The page

void App::drawHome() {
    refreshTournaments();
    pollDiskSpace();
    drawHomeHeader();

    if (!start_error_.empty()) drawMessage(start_error_, start_fixes_, start_fixes_for_, true, "start_error");
    drawUpdateNotice();
    drawGettingStarted();

    // What runs, and what needs the tester, first. Each part tells F1 its section of the guide.
    ImGui::Spacing();
    {
        ui::TopicRegion topic(guide::topics::Several);
        home::drawResourceBars(resourceBars());
    }
    drawRemoteMachinesLine();
    {
        ui::TopicRegion topic(guide::topics::Home);
        drawNeedsAction();
        drawRunCards();
    }
    {
        ui::TopicRegion topic(guide::topics::Queue);
        drawQueue();
        drawLastFinished();
    }
    {
        ui::TopicRegion topic(guide::topics::Home);
        drawHealth();
    }

    if (legacy_runs_ > 0) {
        ImGui::Spacing();
        ui::beginCard("legacy_card");
        ImGui::TextUnformatted(trn("{} match of an earlier version is not in the list yet.",
                                   "{} matches of an earlier version are not in the list yet.", legacy_runs_,
                                   legacy_runs_)
                                   .c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Import them"))) importLegacyRuns();
        tooltip(TR("Turns them into finished tournaments of the list."));
        ui::endCard();
    }

    drawAllTournaments();
}

void App::drawHomeHeader() {
    const auto& style     = ImGui::GetStyle();
    const bool paused     = allPaused();
    const bool pause_all  = paused || canPauseAll();
    const char* pause     = paused ? TR("Resume all") : TR("Pause all");
    const char* folder    = TR("Open a folder…");
    const char* new_match = TR("New tournament");
    const float buttons   = (pause_all ? iconButtonWidth(pause) + style.ItemSpacing.x : 0) + iconButtonWidth(folder) +
                            style.ItemSpacing.x + iconButtonWidth(new_match);

    ui::pageTitle(TR("Tournaments"), guideSection());
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(20), ImGui::GetContentRegionMax().x - buttons));
    if (pause_all) {
        if (ui::iconButton("pause_all", paused ? ui::Icon::Play : ui::Icon::Pause, pause)) {
            if (paused)
                resumeAll();
            else
                pauseAll();
        }
        tooltip(paused ? TR("The paused tournament goes on, and the queue starts its next tournament when the running "
                            "one ends.")
                       : TR("The running tournament starts no new game pair (the games in progress play to the end), "
                            "and the queue starts no tournament until Resume all."));
        ImGui::SameLine();
    }
    if (ui::iconButton("open_folder", ui::Icon::Folder, folder)) {
        dialogs_.openFolder(Dialogs::Purpose::OpenTournament, storage_.runsDir().string());
    }
    tooltip(TR("Every tournament is a folder: it resumes exactly where it stopped, even after a power cut."));
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ui::currentTheme() == ui::Theme::Dark ? ImVec4(0.04f, 0.07f, 0.13f, 1) : ImVec4(1, 1, 1, 1));
    if (ui::iconButton("new_match", ui::Icon::Plus, new_match)) page_ = Page::NewMatch;
    ImGui::PopStyleColor(2);
}

// ---------------------------------------------------------------------------
// Getting started

void App::drawGettingStarted() {
    // Until a first tournament is started (none in the list, none playing), unless hidden.
    if (getting_started_hidden_ || !tournaments_read_ || !tournaments_.empty() || runsPlaying() > 0 || unfinished_)
        return;
    ui::TopicRegion topic(guide::topics::QuickStart);
    ImGui::Spacing();
    ui::beginCard("getting_started");
    ImGui::PushFont(ui::titleFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(TR("Getting started"));
    ImGui::PopFont();
    const char* hide = TR("Hide");
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(hide).x - ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::SmallButton(hide)) {
        getting_started_hidden_ = true;
        auto settings           = storage_.guideSettings();
        settings.getting_started_hidden = true;
        storage_.setGuideSettings(settings);
    }
    tooltip(TR("Puts this card away for good; the quick start stays in the guide."));

    struct Step {
        const char* title;
        const char* detail;
        bool done;
        const char* button;
        std::function<void()> go;
    };
    const Step steps[] = {
        {TR("Add engines to the library"),
         TR("At least two engines: their programs, or the engine list of another program."), engines_.size() >= 2,
         TR("Go to the engines"), [this] { page_ = Page::Engines; }},
        {TR("Set up a tournament"), TR("A format, a time control and the participants."), setupChosen(),
         TR("New tournament"), [this] { page_ = Page::NewMatch; }},
        {TR("Start it"), TR("Start, on the New tournament page; the home page then follows the games."), false,
         nullptr, nullptr},
    };
    auto* dl          = ImGui::GetWindowDrawList();
    const float r     = ImGui::GetFontSize() * 0.62f;
    const float right = ImGui::GetContentRegionMax().x;
    for (int i = 0; i < 3; ++i) {
        const auto& s = steps[i];
        ImGui::PushID(i);
        ImGui::Spacing();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 c(at.x + r, at.y + ImGui::GetTextLineHeight() * 0.5f);
        if (s.done) {
            dl->AddCircleFilled(c, r, ui::GOOD);
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.8f);
            const ImVec2 ts = ImGui::CalcTextSize("\xE2\x9C\x93");
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), "\xE2\x9C\x93");
            ImGui::PopFont();
        } else {
            dl->AddCircle(c, r, ImGui::GetColorU32(ImGuiCol_TextDisabled), 0, std::max(1.0f, px(1.5f)));
            const std::string n = std::to_string(i + 1);
            const ImVec2 ts     = ImGui::CalcTextSize(n.c_str());
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), n.c_str());
        }
        ImGui::Indent(r * 2 + px(10));
        if (s.done) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(s.title);
        if (s.done) ImGui::PopStyleColor();
        if (s.button) {
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(16),
                                     right - ImGui::CalcTextSize(s.button).x - ImGui::GetStyle().FramePadding.x * 2));
            if (ImGui::SmallButton(s.button)) s.go();
        }
        ImGui::PushTextWrapPos(right);
        ImGui::TextDisabled("%s", s.detail);
        ImGui::PopTextWrapPos();
        ImGui::Unindent(r * 2 + px(10));
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button(TR("Read the quick start"))) guide_.open(guide::topics::QuickStart);
    ui::endCard();
}

// ---------------------------------------------------------------------------
// Pause all

bool App::canPauseAll() const {
    const bool tournament = std::any_of(runs_.begin(), runs_.end(), [](const OpenRun& r) {
        return r.run->running() && !r.run->paused() && !r.run->stopping();
    });
    return tournament || (!queue_.empty() && !queue_held_);
}

bool App::allPaused() const {
    // A tournament stopping is paused too, but not to be resumed.
    const bool tournament = std::any_of(runs_.begin(), runs_.end(), [](const OpenRun& r) {
        return r.run->running() && r.run->paused() && !r.run->stopping() && !r.run->stoppingWhenIdle();
    });
    return !canPauseAll() && (tournament || queue_held_);
}

void App::pauseAll() {
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        auto& r = *runs_[i].run;
        if (!r.running() || r.paused() || r.stopping()) continue;
        if (i == shown_run_) {
            setRunPaused(true);
        } else {
            r.setPaused(true);
        }
    }
    queue_held_ = true;
    Console::instance().add(TR("Everything paused: no new game pair starts, and the queue waits for Resume all."));
}

void App::resumeAll() {
    queue_held_ = false;
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        auto& r = *runs_[i].run;
        if (!r.running() || !r.paused() || r.stopping() || r.stoppingWhenIdle()) continue;
        if (i == shown_run_) {
            setRunPaused(false);
        } else {
            r.setPaused(false);
        }
    }
    Console::instance().add(TR("Everything resumed."));
    // A tournament that ended meanwhile: the queue goes on as it would have.
    if (runsPlaying() == 0 && queue_blocked_ && !queue_.empty()) startFromQueue(true);
    queue_blocked_ = false;
}

// ---------------------------------------------------------------------------
// The machine

std::vector<home::ResourceBar> App::resourceBars() {
    home::ResourceBar cores;
    const int used  = coresInUse();
    const int total = std::max(1, topology_.logical);
    cores.title     = TR("Cores");
    cores.capacity  = total;
    cores.caption   = trf("{} of {} in use", used, total);
    cores.free      = trf("{} free", std::max(0, total - used));
    cores.tooltip =
        trf("{} hardware threads on {} cores. A game takes the Threads of its engine that has the most: "
            "the two engines think in turn.",
            total, topology_.physical);
    // Each tournament its part, in a colour of its own; what each one uses, as its process tells it.
    const auto budget = machineBudget();
    home::ResourceBar memory, gpus;
    memory.title    = TR("Memory");
    memory.capacity = static_cast<double>(std::max<uint64_t>(1, budget.memory_mb));
    memory.tooltip =
        trf("The memory budget of Settings, {}: a game starts only when its engines' Hash and processes "
            "fit in it.",
            formatMemory(budget.memory_mb));
    gpus.title           = TR("GPUs");
    gpus.capacity        = std::max(1, budget.gpus);
    gpus.tooltip         = TR("No more engines that use a GPU play at once than the GPUs of Settings.");
    uint64_t memory_used = 0;
    int gpus_used        = 0;
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        const auto& r = *runs_[i].run;
        const auto* t = r.tournament();
        // A tournament on a remote machine takes nothing of this computer's.
        if (!r.running() || !t || !runs_[i].machine_id.empty()) continue;
        const auto& name  = t->spec().name;
        const ImU32 color = runs_.size() > 1
                                ? ImGui::ColorConvertFloat4ToU32(ImPlot::GetColormapColor(static_cast<int>(i)))
                                : ui::ACCENT;
        const auto use    = r.inFlight();
        const int threads = r.remote() ? use.threads
                            : i == shown_run_
                                ? used
                                : r.runningGames() * tour::largestGame(t->spec()).threads;
        if (threads > 0) {
            cores.shares.push_back({fmt::format("{} · {}", name, threads),
                                    trn("{}: {} core in use", "{}: {} cores in use", threads, name, threads),
                                    double(threads), color});
        }
        if (use.memory_mb > 0) {
            memory.shares.push_back({fmt::format("{} · {}", name, formatMemory(use.memory_mb)),
                                     trf("{}: {} for its games' engines", name, formatMemory(use.memory_mb)),
                                     double(use.memory_mb), color});
            memory_used += use.memory_mb;
        }
        if (use.gpus > 0) {
            gpus.shares.push_back({fmt::format("{} · {}", name, use.gpus),
                                   trn("{}: {} GPU in use", "{}: {} GPUs in use", use.gpus, name, use.gpus),
                                   double(use.gpus), color});
            gpus_used += use.gpus;
        }
    }
    // The window's own engines: the Board page's analysis and games.
    if (const int board = analysisThreads(); board > 0) {
        cores.shares.push_back({fmt::format("{} · {}", TR("Board"), board),
                                trn("The Board page: {} core for its analysis and its games",
                                    "The Board page: {} cores for its analysis and its games", board, board),
                                double(board), ui::WARN});
    }
    // In use: every tournament's games, and the window's engines.
    double in_use = 0;
    for (const auto& share : cores.shares) in_use += share.amount;
    cores.caption = trf("{} of {} in use", static_cast<int>(in_use), total);
    // Several tournaments share the budget of Settings by their weights, set on each card.
    if (runsPlaying() > 1) {
        cores.caption += " · " + trn("{} shared by the tournaments", "{} shared by the tournaments", budget.threads,
                                     budget.threads);
        cores.tooltip += "\n" + std::string(TR("The tournaments playing at once share the cores of the budget "
                                                "(Settings) by their weights, set on each card: one game's worth "
                                                "each first, then by weight, none above its own concurrency."));
    }
    cores.free                          = trf("{} free", std::max(0, total - static_cast<int>(in_use)));
    std::vector<home::ResourceBar> bars = {cores};
    if (budget.memory_mb > 0) {
        memory.caption = trf("{} of {} for the engines", formatMemory(memory_used), formatMemory(budget.memory_mb));
        memory.free = trf("{} free", formatMemory(budget.memory_mb > memory_used ? budget.memory_mb - memory_used : 0));
        bars.push_back(memory);
    }
    if (budget.gpus > 0) {
        gpus.caption = trf("{} of {} in use", gpus_used, budget.gpus);
        gpus.free    = trf("{} free", std::max(0, budget.gpus - gpus_used));
        bars.push_back(gpus);
    }
    return bars;
}

int App::coresInUse() {
    const auto* obs = run_->observer();
    const auto* t   = run_->tournament();
    if (!obs || !t || !run_->running()) return 0;

    // The Threads of each participant, by the observer's index, read again only when the tournament changed.
    const auto& names = obs->names();
    if (cores_cache_.of != t || cores_cache_.version != t->version() || cores_cache_.names != names.size()) {
        std::unordered_map<std::string, int> threads;
        for (const auto& p : t->spec().participants) threads[p.engine.name] = std::max(1, configThreads(p.engine));
        cores_cache_.threads.assign(names.size(), 1);
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (const auto it = threads.find(names[i]); it != threads.end()) cores_cache_.threads[i] = it->second;
        }
        cores_cache_.of      = t;
        cores_cache_.version = t->version();
        cores_cache_.names   = names.size();
    }

    int cores = 0;
    for (int i = 0; i < obs->slotCount(); ++i) {
        const auto players = obs->slotPlayers(i);
        if (!players) continue;
        int game = 1;
        for (const int p : *players) {
            if (p >= 0 && p < static_cast<int>(cores_cache_.threads.size()))
                game = std::max(game, cores_cache_.threads[static_cast<std::size_t>(p)]);
        }
        cores += game;
    }
    return cores;
}

// ---------------------------------------------------------------------------
// What runs

std::optional<double> App::runSecondsLeft() const {
    const auto total = run_->info().total_games;
    if (!run_->running() || total == 0 || games_per_hour_ <= 0) return std::nullopt;
    const auto played = static_cast<uint64_t>(games_.size());
    return played >= total ? 0.0 : static_cast<double>(total - played) / games_per_hour_ * 3600.0;
}

home::RunCard App::runCard() {
    const auto* t    = run_->tournament();
    const auto& info = run_->info();

    home::RunCard c;
    c.id     = info.dir.string();
    c.name   = t ? t->spec().name : info.name;
    c.format = fmt::format("{} · {}", formatLabel(info.format), info.limit_text);
    if (info.format != tour::Format::Match)
        c.format += " · " + trn("{} engine", "{} engines", info.participants, info.participants);
    if (info.sprt) c.format += fmt::format(" · SPRT [{:g}, {:g}]", info.elo0, info.elo1);
    c.played  = games_.size();
    c.total   = info.total_games;
    c.playing = run_->runningGames();

    c.paused     = run_->paused();
    c.can_pause  = !run_->stopping();
    c.can_resume = !run_->stopping() && !run_->stoppingWhenIdle();
    c.warn_state = run_->paused() || run_->stopping();
    c.state      = run_->stopping()           ? TR("Stopping…")
                   : run_->stoppingWhenIdle() ? TR("Stopping after the running games")
                   : run_->paused()           ? TR("Paused")
                                              : TR("Running");
    if (const auto left = runSecondsLeft()) {
        c.end = trf("Ends about {} (in {})", endTime(*left), ui::formatDuration(*left));
    } else if (info.total_games == 0) {
        c.end = info.sprt ? TR("Until the SPRT decides") : TR("Until stopped");
    } else {
        c.end = TR("End estimated after the first games");
    }

    // Who leads, computed again when games came (the standings once their table was).
    const bool match = info.format == tour::Format::Match;
    if (!match) refreshTable();
    const std::size_t key = match ? games_.size() : table_for_;
    if (leader_cache_.games != key || leader_cache_.started != info.started) {
        leader_cache_.games   = key;
        leader_cache_.started = info.started;
        auto& leader          = leader_cache_.leader;
        auto& detail          = leader_cache_.detail;
        leader.clear();
        detail.clear();
        if (games_.empty()) {
            leader = TR("No game finished yet");
        } else if (match) {
            std::tie(leader, detail) = matchLeader(info, stats_, current_, info.sprt && sprt_ ? &*sprt_ : nullptr);
        } else if (info.format == tour::Format::TeamMatch && t) {
            // The teams' score, and team A's Elo over team B once it has an interval.
            const auto& a = teams_.teams[0];
            const auto& b = teams_.teams[1];
            leader        = fmt::format("{} {} – {} {}", teamLabel(t->spec(), 0), home::halfPoints(a.points),
                                        home::halfPoints(b.points), teamLabel(t->spec(), 1));
            if (teams_.elo) detail = trf("Elo {:+.1f} ± {:.1f}", *teams_.elo, *teams_.error);
        } else if (info.format == tour::Format::Knockout && t) {
            const auto left = t->enginesLeft();
            leader          = trn("{} player left", "{} players left", left.size(), left.size());
            if (left.size() <= 4) {
                for (const auto& name : left) detail += (detail.empty() ? "" : ", ") + name;
            }
        } else if (!table_.rows.empty()) {
            // Its rating against the field once it means something: not within ±300 after a few games.
            const auto& first = table_.rows.front();
            const bool rated  = first.rated && first.rating.error > 0 && first.rating.error < 300;
            leader = rated ? trf("Leader: {} {}", first.name, formatElo(first.rating.elo, first.rating.error))
                           : trf("Leader: {}", first.name);
            detail = trf("{} points of {} games", home::halfPoints(first.points), first.games);
        }
    }
    c.leader = leader_cache_.leader;
    c.detail = leader_cache_.detail;
    return c;
}

std::pair<std::string, std::string> App::matchLeader(const RunInfo& info, const fastchess::Stats& stats,
                                                     const StatPoint& point, const fastchess::SPRT* sprt) {
    const double a = stats.wins + 0.5 * stats.draws;
    const double b = stats.losses + 0.5 * stats.draws;
    auto score = fmt::format("{} {} – {} {}", info.engine_a, home::halfPoints(a), home::halfPoints(b), info.engine_b);
    if (sprt) {
        auto llr = fmt::format("LLR {:.2f} ({:.2f}, {:.2f})", point.llr, sprt->getLowerBound(), sprt->getUpperBound());
        if (!point.valid) return {std::move(llr), std::move(score)};
        return {std::move(llr), fmt::format("Elo {} · {}", formatElo(point.elo, point.elo_err), score)};
    }
    if (point.valid) {
        const bool a_leads = point.elo >= 0;
        return {trf("Leader: {} {}", a_leads ? info.engine_a : info.engine_b,
                    formatElo(std::abs(point.elo), point.elo_err)),
                std::move(score)};
    }
    return {std::move(score), {}};
}

home::RunCard App::runCardOf(std::size_t index) {
    const auto& r    = *runs_[index].run;
    const auto* t    = r.tournament();
    const auto& info = r.info();
    home::RunCard c;
    c.id     = info.dir.string();
    c.name   = t ? t->spec().name : info.name;
    c.format = fmt::format("{} · {}", formatLabel(info.format), info.limit_text);
    if (info.format != tour::Format::Match)
        c.format += " · " + trn("{} engine", "{} engines", info.participants, info.participants);
    if (info.sprt) c.format += fmt::format(" · SPRT [{:g}, {:g}]", info.elo0, info.elo1);
    if (t) {
        for (const auto& g : t->results()) c.played += g.voided ? 0 : 1;
    }
    c.total      = info.total_games;
    c.playing    = r.runningGames();
    c.paused     = r.paused();
    c.can_pause  = !r.stopping();
    c.can_resume = !r.stopping() && !r.stoppingWhenIdle();
    c.warn_state = r.paused() || r.stopping();
    c.state      = r.stopping()           ? TR("Stopping…")
                   : r.stoppingWhenIdle() ? TR("Stopping after the running games")
                   : r.paused()           ? TR("Paused")
                                          : TR("Running");
    c.end        = TR("Open it for its end");

    // Who leads, from its copy of the journal, computed again when games came.
    if (!t) return c;
    const auto results = t->results();
    auto& cache        = other_leaders_[c.id];
    if (cache.games != results.size()) {
        cache.games = results.size();
        cache.leader.clear();
        cache.detail.clear();
        const auto wall = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
        cache.games_per_hour = home::gamesPerHour(results, wall);
        const auto& spec     = t->spec();
        if (c.played == 0) {
            cache.leader = TR("No game finished yet");
        } else if (spec.format == tour::Format::Match && spec.participants.size() == 2) {
            // As the shown match's: its SPRT with fastchess' own classes, on the pairs complete.
            std::optional<fastchess::SPRT> sprt;
            if (info.sprt) sprt.emplace(info.alpha, info.beta, info.elo0, info.elo1, info.model, true);
            const auto stats = t->pairStats(spec.participants[0].id, spec.participants[1].id);
            const auto point = computeStats(stats, info.penta, sprt ? &*sprt : nullptr);
            std::tie(cache.leader, cache.detail) = matchLeader(info, stats, point, sprt ? &*sprt : nullptr);
        } else if (spec.format == tour::Format::RoundRobin || spec.format == tour::Format::Gauntlet ||
                   spec.format == tour::Format::Swiss) {
            const auto table = tour::computeTable(spec, results, std::nullopt, t->byes());
            if (!table.rows.empty()) {
                const auto& first = table.rows.front();
                const bool rated  = first.rated && first.rating.error > 0 && first.rating.error < 300;
                cache.leader = rated ? trf("Leader: {} {}", first.name, formatElo(first.rating.elo, first.rating.error))
                                     : trf("Leader: {}", first.name);
                cache.detail = trf("{} points of {} games", home::halfPoints(first.points), first.games);
            }
        }
    }
    c.leader = cache.leader;
    c.detail = cache.detail;
    if (c.total == 0) {
        c.end = info.sprt ? TR("Until the SPRT decides") : TR("Until stopped");
    } else if (cache.games_per_hour > 0 && !r.paused()) {
        const double left =
            c.played >= c.total ? 0.0 : static_cast<double>(c.total - c.played) / cache.games_per_hour * 3600.0;
        c.end = trf("Ends about {} (in {})", endTime(left), ui::formatDuration(left));
    } else {
        c.end = TR("End estimated after the first games");
    }
    return c;
}

void App::drawRunCards() {
    if (runsPlaying() == 0) {
        // A tournament that can be resumed; one an error stopped is under Needs action.
        if (unfinished_ && !stoppedByError()) {
            sectionTitle(TR("Interrupted"));
            drawInterrupted(false);
        }
        return;
    }

    sectionTitle(TR("Running"));
    // Side by side when there is room: one card per tournament running, the one shown with what its views know.
    std::vector<home::RunCard> cards;
    std::vector<std::size_t> of;
    const bool several = runsPlaying() > 1;
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        if (!runs_[i].run->running()) continue;
        cards.push_back(i == shown_run_ ? runCard() : runCardOf(i));
        if (several && runs_[i].run->remote()) {
            cards.back().weight   = static_cast<int>(std::lround(runs_[i].weight));
            cards.back().controls = [this, i] { drawRunControls(i); };
        }
        // On a remote machine: which, and whether the window reaches it at the moment.
        if (!runs_[i].machine_id.empty()) {
            cards.back().format = trf("On {}", runs_[i].machine_name) + " · " + cards.back().format;
            if (const auto* remote = dynamic_cast<const RemoteRun*>(runs_[i].run.get()); remote && remote->connecting()) {
                cards.back().state      = trf("Reaching {}…", runs_[i].machine_name);
                cards.back().warn_state = true;
            }
        }
        of.push_back(i);
    }
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float avail   = ImGui::GetContentRegionAvail().x;
    const int columns =
        std::clamp(static_cast<int>((avail + spacing) / (px(420) + spacing)), 1, static_cast<int>(cards.size()));
    const float width = (avail - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (i % static_cast<std::size_t>(columns) != 0) ImGui::SameLine();
        const auto run = of[i];
        switch (home::drawRunCard(cards[i], width)) {
            case home::CardAction::Open:
                showRun(run);
                page_ = Page::Tournament;
                break;
            case home::CardAction::Pause:
            case home::CardAction::Resume: {
                const bool pause = !runs_[run].run->paused();
                if (run == shown_run_) {
                    setRunPaused(pause);
                } else {
                    runs_[run].run->setPaused(pause);
                }
                break;
            }
            case home::CardAction::None:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// Needs action

bool App::stoppedByError() const {
    if (run_->state() != RunController::State::Finished || finish_message_.empty()) return false;
    if (finish_hidden_ == finish_message_) return false;
    return !run_->error().empty() || finish_fixes_for_ == finish_message_;
}

bool App::drawNeedsAction() {
    // Under one title: the engines that cannot start (their card has it), then the rest.
    bool shown       = drawStartFailures(TR("Needs action"));
    const auto title = [&] {
        if (!shown) sectionTitle(TR("Needs action"));
        shown = true;
    };
    if (stoppedByError()) {
        title();
        drawInterrupted(true);
    }
    for (const auto& disk : disks_) {
        if (!home::diskLow(disk.available, disk.capacity)) continue;
        title();
        ImGui::PushID(disk.dir.string().c_str());
        ui::beginCard("disk_card");
        if (const auto* fix = drawNotice(home::diskNotice(disk), "disk")) applyFix(*fix);
        ui::endCard();
        ImGui::PopID();
    }
    return shown;
}

void App::drawInterrupted(bool error) {
    const auto* t        = run_->tournament();
    const bool resumable = unfinished_ && (!error || (t && unfinished_->dir == t->dir()));
    if (!resumable && !error) return;

    ui::beginCard(error ? "stopped_card" : "unfinished_card");
    ImGui::TextUnformatted(error ? (t ? t->spec().name : run_->info().name).c_str() : unfinished_->spec.name.c_str());
    if (resumable) {
        const auto& u = *unfinished_;
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%s", (u.total ? trf("{} / {} games", u.played, *u.total) : trn("{} game", "{} games", u.played, u.played))
                      .c_str());
    }
    // Which engine, what happened, and its fixes (Open the engine, Open the log).
    if (error) drawMessage(finish_message_, finish_fixes_, finish_fixes_for_, true, "home_finish");

    if (resumable) {
        const auto& u = *unfinished_;
        if (!resume_crashed_.empty() && fs::absolute(u.dir).lexically_normal() == resume_crashed_) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(
                TR("The program ended while it resumed this tournament at the last launch: it is "
                   "not resumed automatically again."));
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            reportButton("report_resume", true,
                         "The program ended while it resumed a tournament at launch (" + u.spec.name + ").");
        }
        // Pressed, it starts at once, or says what it waits for.
        const auto key = resumeKey(u.dir);
        if (!drawStartWaiting("resume_match", key)) {
            if (ui::iconButton("resume_match", ui::Icon::Play, TR("Resume"))) resumeRun();
            tooltip(TR("It continues exactly where it stopped: no game is lost or played twice."));
        }
        if (const auto waiting = startWaitingText(key); !waiting.empty()) ImGui::TextDisabled("%s", waiting.c_str());
    }
    if (error) {
        if (resumable) ImGui::SameLine();
        if (ImGui::Button(TR("Open"))) page_ = Page::Tournament;
        tooltip(TR("Its workspace: standings, live games, schedule"));
        ImGui::SameLine();
        if (ImGui::Button(TR("Hide##stopped"))) finish_hidden_ = finish_message_;
        tooltip(TR("The tournament stays in the list, to resume or open later."));
    }
    ui::endCard();
}

void App::pollDiskSpace() {
    // The drives the tournaments are written to, asked every minute on a worker thread: a network drive can be slow.
    if (disk_job_.valid() && disk_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        disks_ = disk_job_.get();
    const auto now = std::chrono::steady_clock::now();
    if (disk_job_.valid() || (disk_asked_ != decltype(disk_asked_){} && now - disk_asked_ < std::chrono::minutes(1)))
        return;
    disk_asked_ = now;

    std::vector<fs::path> dirs = {storage_.runsDir()};
    // A tournament opened from elsewhere may be on another drive.
    if (const auto* t = run_->tournament(); t && run_->running()) {
        const auto rel = t->dir().lexically_relative(storage_.runsDir());
        if (rel.empty() || *rel.begin() == "..") dirs.push_back(t->dir());
    }
    disk_job_ = std::async(std::launch::async, [dirs] {
        std::vector<home::DiskSpace> out;
        for (const auto& dir : dirs) {
            if (auto space = home::diskSpace(dir)) out.push_back(std::move(*space));
        }
        return out;
    });
}

// ---------------------------------------------------------------------------
// Queue, last finished, health, every tournament

void App::drawQueue() {
    if (queue_.empty()) return;

    // How long each lasts: the running tournament at its pace, the queued ones from their games and time control.
    std::vector<std::optional<double>> lengths;
    if (run_->running()) lengths.push_back(runSecondsLeft());
    for (const auto& q : queue_) lengths.push_back(plannedSeconds(q.setup, topology_));
    const auto end = home::queueEnd(lengths);

    sectionTitle(trn("Queue · {} tournament", "Queue · {} tournaments", queue_.size(), queue_.size()).c_str());
    ui::beginCard("queue_card");
    if (end.known()) {
        auto text = trf("The queue ends about {} (in {})", endTime(end.seconds), ui::formatDuration(end.seconds));
        if (end.unknown > 0) {
            text += trn(", and {} tournament of unknown length", ", and {} tournaments of unknown length", end.unknown,
                        end.unknown);
        }
        ImGui::TextUnformatted(text.c_str());
    } else {
        ImGui::TextUnformatted(
            TR("When the queue ends is not known: an SPRT without a game limit, or nodes or depth per move, has no "
               "length to tell."));
    }
    if (ImGui::Checkbox(TR("Start when cores are free"), &budget_setting_.queue_when_free))
        storage_.setMachineBudget(budget_setting_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The next tournament starts beside those running as soon as the cores of the budget "
                                   "(Settings) are more than they can take, each at its concurrency."));
    }
    if (queue_held_) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::TextUnformatted(TR("Held by Pause all: no tournament starts until Resume all."));
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("%s", TR("Started one after the other, when the running tournament ends by itself."));
    }

    // When each starts, while the lengths before it are known.
    std::optional<double> start = run_->running() ? lengths.front() : std::optional<double>(0.0);
    const std::size_t first     = run_->running() ? 1 : 0;
    int remove = -1, up = -1;
    for (std::size_t i = 0; i < queue_.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextDisabled("%d", static_cast<int>(i + 1));
        ImGui::SameLine();
        ImGui::TextUnformatted(queue_[i].label.c_str());
        ImGui::SameLine();
        const auto& length = lengths[first + i];
        auto when          = formatLabel(static_cast<tour::Format>(queue_[i].setup.format));
        when += " · " + (length ? trf("about {}", ui::formatDuration(*length)) : std::string(TR("length unknown")));
        if (start && (i > 0 || run_->running())) when += " · " + trf("starts about {}", endTime(*start));
        ImGui::TextDisabled("%s", when.c_str());
        if (start && length)
            *start += *length;
        else
            start.reset();

        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(10),
                                 ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - px(200)));
        ImGui::BeginDisabled(i == 0);
        if (ImGui::SmallButton("^")) up = static_cast<int>(i);
        ImGui::EndDisabled();
        tooltip(TR("One place up in the queue."));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Remove"))) remove = static_cast<int>(i);
        if (i == 0 && !run_->running()) {
            ImGui::SameLine();
            if (!drawStartWaiting("queue_start", "queue", false) && ImGui::SmallButton(TR("Start now"))) {
                ImGui::PopID();
                startFromQueue();
                ui::endCard();
                return;
            }
        }
        ImGui::PopID();
    }
    if (const auto waiting = startWaitingText("queue"); !waiting.empty()) ImGui::TextDisabled("%s", waiting.c_str());
    ui::endCard();

    if (remove >= 0) queue_.erase(queue_.begin() + remove);
    if (up > 0) std::swap(queue_[up], queue_[up - 1]);
    if (remove >= 0 || up > 0) storage_.saveQueue(queue_);
}

void App::drawLastFinished() {
    const auto* t       = run_->tournament();
    const fs::path busy = run_->running() && t ? t->dir() : fs::path();
    const auto last     = home::lastFinished(tournaments_, home::LAST_FINISHED, busy);
    if (last.empty()) {
        relaunch_request_.clear();
        return;
    }

    sectionTitle(TR("Last finished"));
    ui::beginCard("finished_card");
    constexpr auto flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    const float buttons  = iconButtonWidth(TR("Relaunch with…")) + iconButtonWidth(TR("Clone")) +
                           ImGui::GetStyle().ItemSpacing.x * 2 + ImGui::CalcTextSize(TR("Open")).x +
                           ImGui::GetStyle().FramePadding.x * 2;
    if (ImGui::BeginTable("finished", 5, flags)) {
        ImGui::TableSetupColumn(TR("Tournament"), ImGuiTableColumnFlags_WidthStretch, 2.5f);
        ImGui::TableSetupColumn(TR("Format"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(TR("Result"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn(TR("Date"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, buttons);
        for (const auto* s : last) {
            ImGui::PushID(s->dir.string().c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(s->spec.name.c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", formatLabel(s->spec.format).c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(home::resultText(*s).c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", folderDate(s->dir).c_str());
            ImGui::TableNextColumn();
            // A match plays again with another engine A, the next version say.
            if (s->spec.format == tour::Format::Match && s->spec.participants.size() >= 2) {
                if (!(relaunch_from_ == s->dir && drawStartWaiting("relaunch", "start"))) {
                    drawRelaunch(s->spec.participants[0].engine.name, s->spec.participants[1].engine.name, s->dir,
                                 relaunch_request_ == s->dir);
                }
                ImGui::SameLine();
            }
            drawClone(s->dir);
            ImGui::SameLine();
            // Open at the right, under the other rows'.
            const float open_w = ImGui::CalcTextSize(TR("Open")).x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetCursorPosX(
                std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - open_w));
            if (ImGui::Button(TR("Open"))) openTournament(s->dir);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ui::endCard();
    relaunch_request_.clear();
}

void App::drawHealth() {
    // The journals of the last 24 hours, read again every 30 seconds on a worker thread: only what they gained.
    const auto now = std::chrono::steady_clock::now();
    if (incidents_job_.valid() && incidents_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        incidents_ = incidents_job_.get();
    if (!incidents_job_.valid() && now - incidents_asked_ > std::chrono::seconds(30)) {
        incidents_asked_ = now;
        const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                               (std::chrono::system_clock::now() - std::chrono::hours(24)).time_since_epoch())
                               .count();
        incidents_job_   = std::async(std::launch::async, [scanner = incident_scanner_, root = storage_.runsDir(),
                                                           since] { return scanner->scan(root, since); });
    }

    sectionTitle(TR("Health"));
    ui::beginCard("health_card");
    const auto item = [&](bool warn, const std::string& text, bool first = false) {
        if (!first) {
            ImGui::SameLine();
            ImGui::TextDisabled("·");
            ImGui::SameLine();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, warn ? ui::WARN : ImGui::GetColorU32(ImGuiCol_Text));
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
    };

    ImGui::TextDisabled("%s", TR("Last 24 hours:"));
    ImGui::SameLine();
    if (incidents_) {
        const auto& i = *incidents_;
        item(i.crashes > 0, trn("{} crash or hang", "{} crashes or hangs", i.crashes, i.crashes), true);
        item(i.time_losses > 0, trn("{} loss on time", "{} losses on time", i.time_losses, i.time_losses));
        if (i.network_cuts > 0) {
            item(false, trn("{} game cut by the network, played again", "{} games cut by the network, played again",
                            i.network_cuts, i.network_cuts));
        }
        item(i.illegal_moves > 0, trn("{} illegal move", "{} illegal moves", i.illegal_moves, i.illegal_moves));
    } else {
        ImGui::TextDisabled("%s", TR("reading the tournaments…"));
    }

    // Free memory below a tenth of the installed is worth a look.
    if (const auto memory = systemMemory()) {
        item(memory->available_mb * 10 < memory->total_mb,
             trf("{} of memory free of {}", formatMemory(memory->available_mb), formatMemory(memory->total_mb)), true);
    }
    // The tournaments whose next game waits for memory or a GPU (the budget of Settings).
    for (const auto& r : runs_) {
        const auto* t = r.run->tournament();
        if (!r.run->running() || !t) continue;
        if (r.run->waiting() == tour::Wait::Memory) item(true, trf("{}: games wait for memory", t->spec().name));
        if (r.run->waiting() == tour::Wait::Gpus) item(true, trf("{}: games wait for a GPU", t->spec().name));
    }
    // Those of this program (engine reads, the quick test) and of the tournaments' processes, two per game.
    int processes = engineProcesses();
    for (const auto& r : runs_) {
        if (r.run->remote()) processes += 2 * r.run->runningGames();
    }
    item(false, trn("{} engine process running", "{} engine processes running", processes, processes),
         !systemMemory().has_value());
    if (incidents_) drawSetupNotices(engineIncidentNotices(*incidents_), "incidents");
    ui::endCard();
}

void App::drawAllTournaments() {
    // A section title that folds the table away.
    ImGui::Spacing();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.1f);
    const bool open = ImGui::TreeNodeEx(
        "##all_tournaments",
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanLabelWidth, "%s",
        fmt::format("{} ({})", TR("All tournaments"), tournaments_.size()).c_str());
    ImGui::PopFont();
    if (!open) return;
    if (tournaments_.empty()) {
        if (tournaments_read_) ImGui::TextDisabled("%s", TR("No tournament yet. Start with a new tournament."));
        return;
    }

    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
    // The space left, and enough for a few rows when the cards above took it: the page scrolls then.
    const float height = std::max(ImGui::GetContentRegionAvail().y, px(260));
    if (!ImGui::BeginTable("tournaments", 7, flags, ImVec2(0, height))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(TR("Tournament"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn(TR("Format"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(TR("Time control"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(TR("Games"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(TR("State"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn(TR("Date"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
    const auto small_width = [](const char* label) {
        return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    };
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
                            small_width(TR("Open")) + small_width(TR("Clone")) + small_width(TR("Folder")) + px(8));
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(tournaments_.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& s         = tournaments_[static_cast<std::size_t>(i)];
            const int open        = findRun(s.dir);
            const bool is_running = open >= 0 && runs_[static_cast<std::size_t>(open)].run->running();
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.spec.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatLabel(s.spec.format).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(limitText(s.spec.limit()).c_str());  // as the tournament's header writes it
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(
                (s.total ? fmt::format("{} / {}", s.played, *s.total) : fmt::format("{}", s.played)).c_str());
            ImGui::TableNextColumn();
            const auto status = statusLabel(s, is_running);
            const ImU32 col   = is_running ? ui::ACCENT : !s.finished ? ui::WARN : ui::MUTED;
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(status.c_str());
            ImGui::PopStyleColor();
            if (s.finished && !s.scores.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", home::resultText(s).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", folderDate(s.dir).c_str());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(TR("Open"))) openTournament(s.dir);
            ImGui::SameLine();
            drawClone(s.dir, true);
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Folder"))) ui::openPath(s.dir);

            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

}  // namespace fcg

namespace fcg {

void App::drawRemoteMachinesLine() {
    // The machines the running tournaments' engines run on, each once: connected and what plays there, or why not.
    std::vector<control::Status::Machine> machines;
    for (const auto& r : runs_) {
        const auto* remote = dynamic_cast<const RemoteRun*>(r.run.get());
        if (!remote || !remote->running()) continue;
        for (const auto& m : remote->machines()) {
            auto it = std::find_if(machines.begin(), machines.end(), [&](const auto& o) { return o.id == m.id; });
            if (it == machines.end()) {
                machines.push_back(m);
            } else {
                it->engines += m.engines;
                it->connections += m.connections;
                if (it->error.empty()) it->error = m.error;
            }
        }
    }
    if (machines.empty()) return;
    ui::TopicRegion topic(guide::topics::RemoteEngines);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Machines"));
    for (const auto& m : machines) {
        ImGui::SameLine();
        const bool up = m.connections > 0;
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(up ? ui::GOOD : m.error.empty() ? ui::MUTED
                                                                                                           : ui::BAD));
        ImGui::TextUnformatted("●");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        std::string text = m.name;
        if (up) {
            text += " · " + trn("{} engine playing", "{} engines playing", m.engines, m.engines);
        } else if (!m.error.empty() && m.since > 0) {
            const auto t = static_cast<std::time_t>(m.since);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char hhmm[8];
            std::strftime(hhmm, sizeof hhmm, "%H:%M", &tm);
            text += " · " + trf("not answering since {}", hhmm);
        }
        if (up) {
            ImGui::TextUnformatted(text.c_str());
        } else {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(m.error.empty() ? ui::MUTED : ui::BAD), "%s",
                               text.c_str());
        }
        if (!m.error.empty()) ImGui::SetItemTooltip("%s", m.error.c_str());
    }
}

}  // namespace fcg
