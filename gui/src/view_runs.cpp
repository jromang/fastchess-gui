// The tournaments open at once: a tab each in the workspace, each playing in a process of its own (remote_run.hpp).

#include <algorithm>

#include <implot.h>
#include <engine/uci_engine.hpp>
#include <tour/files.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "memory.hpp"
#include "notify.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool sameDir(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::equivalent(a, b, ec)) return true;
    return fs::absolute(a).lexically_normal() == fs::absolute(b).lexically_normal();
}

}  // namespace

int App::findRun(const fs::path& dir) const {
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        const auto* t = runs_[i].run->tournament();
        if (t && sameDir(t->dir(), dir)) return static_cast<int>(i);
    }
    return -1;
}

bool App::localPlaying() const {
    return std::any_of(runs_.begin(), runs_.end(),
                       [](const OpenRun& r) { return r.run->running() && !r.run->remote(); });
}

int App::runsPlaying() const {
    return static_cast<int>(
        std::count_if(runs_.begin(), runs_.end(), [](const OpenRun& r) { return r.run->running(); }));
}

std::size_t App::addRun(OpenRun open) {
    stashRunView();
    runs_.push_back(std::move(open));
    shown_run_         = runs_.size() - 1;
    run_               = runs_.back().run.get();
    tab_select_frames_ = 2;
    resetRunView();
    loadRunView();
    return shown_run_;
}

void App::showRun(std::size_t index) {
    if (index >= runs_.size() || index == shown_run_) return;
    stashRunView();
    shown_run_         = index;
    run_               = runs_[index].run.get();
    tab_select_frames_ = 2;
    const auto& open   = runs_[index];
    finish_message_    = open.finish_message;
    finish_fixes_      = open.finish_fixes;
    finish_fixes_for_  = open.finish_fixes_for;
    stopped_by_        = open.stopped_by;
    user_stop_         = open.user_stop;
    resume_guarded_    = open.resume_guarded;
    games_at_start_    = open.games_at_start;
    console_mark_      = open.console_mark;
    loadRunView();
}

void App::closeRun(std::size_t index) {
    if (index >= runs_.size() || runs_[index].run->running()) return;
    if (index == shown_run_) stashRunView();
    runs_.erase(runs_.begin() + static_cast<std::ptrdiff_t>(index));
    if (runs_.empty()) {
        shown_run_ = 0;
        run_       = &idle_run_;
        clearRunView();
        finish_message_.clear();
        return;
    }
    const auto shown = shown_run_ == index ? std::min(index, runs_.size() - 1) : shown_run_ - (shown_run_ > index);
    shown_run_       = runs_.size();  // none: showRun() shows it afresh
    run_             = runs_[shown].run.get();
    showRun(shown);
}

void App::stashRunView() {
    auto* open = shownRun();
    if (!open || open->run.get() != run_) return;
    open->finish_message   = finish_message_;
    open->finish_fixes     = finish_fixes_;
    open->finish_fixes_for = finish_fixes_for_;
    open->stopped_by       = stopped_by_;
    open->user_stop        = user_stop_;
    open->resume_guarded   = resume_guarded_;
    open->games_at_start   = games_at_start_;
    open->console_mark     = console_mark_;
}

void App::loadRunView() {
    clearRunView();
    const auto* t = run_->tournament();
    auto* obs     = run_->observer();
    if (!t || !obs) return;
    // The games recorded so far, then those its observer told since (not in the results read yet).
    games_ = obs->records(t->results());
    std::vector<GameRecord> more;
    std::vector<fastchess::Stats> counted;  // in the history below already
    obs->drain(more, counted);
    games_.insert(games_.end(), more.begin(), more.end());
    reasons_  = obs->reasons();
    openings_ = obs->openings();
    totals_   = obs->totals();
    if (isMatch() && t->spec().participants.size() >= 2) {
        const auto& p = t->spec().participants;
        for (const auto& s : t->pairHistory(p[0].id, p[1].id)) {
            stats_ = s;
            history_.push_back(computeStats(s, run_->info().penta, sprt_ ? &*sprt_ : nullptr));
        }
        if (!history_.empty()) current_ = history_.back();
    }
}

std::string App::launchTournament(const fs::path& dir) {
    const auto& program = fastchess::engine::xboard_adapter;  // this program, as main() found it
    if (program.empty()) return TR("This program cannot find its own file to start a tournament.");

    RemoteStart start;
    start.program = program;
    start.profile = storage_.root();
    start.dir     = dir;
    // Its share beside the tournaments playing, from its first game.
    try {
        start.allowance = allowanceFor(tour::TournamentState::open(dir, true)->spec(), 1.0);
    } catch (const std::exception&) {
        // Said by the start below.
    }
    auto remote = std::make_unique<RemoteRun>();
    if (auto error = remote->start(start); !error.empty()) return error;

    OpenRun open;
    open.run = std::move(remote);
    // The tab of the same tournament (stopped, looked at) is taken over, with its place.
    if (const int i = findRun(dir); i >= 0 && !runs_[static_cast<std::size_t>(i)].run->running()) {
        const auto index = static_cast<std::size_t>(i);
        open.weight      = runs_[index].weight;
        stashRunView();
        runs_[index]       = std::move(open);
        shown_run_         = index;
        run_               = runs_[index].run.get();
        tab_select_frames_ = 2;
        resetRunView();
        loadRunView();
        return {};
    }
    addRun(std::move(open));
    return {};
}

std::string App::resumeCheck(const fs::path& dir) {
    try {
        const auto t = tour::TournamentState::open(dir, true);
        if (t->finished()) return TR("This match is already finished.");
        // Its PGN books in the cache again, made again if their files changed (the window has done it already).
        for (const auto& need : pgnBookNeeds(t->spec())) {
            if (const auto problem = pgnBookProblem(need.path, need.depth, need.chess960); !problem.empty()) {
                return trf("Cannot resume the match: {}",
                           trf("The book of {} cannot be used: {}", need.participant, problem));
            }
        }
        for (const auto& warning : t->warnings()) Console::instance().add(warning, true);
    } catch (const std::exception& e) {
        return trf("Cannot resume the match: {}", e.what());
    }
    // Played by another program (a --run in a terminal, another window): not twice.
    if (findRun(dir) < 0 || !runs_[static_cast<std::size_t>(findRun(dir))].run->running()) {
        try {
            const tour::FolderLock probe(dir);
        } catch (const tour::FolderBusy&) {
            return TR("This tournament is playing in another program.");
        } catch (const std::exception&) {
        }
    } else {
        return TR("This tournament is playing already.");
    }
    return {};
}

void App::onBackgroundRunFinished(std::size_t index) {
    auto& open      = runs_[index];
    const auto& run = *open.run;
    const auto* t   = run.tournament();
    const auto name = run.info().name;

    // Quitting while it runs leaves it to be resumed at the next launch; otherwise it ended, or the user stopped it.
    const auto marker_dir = t ? fs::absolute(t->dir()).lexically_normal().string() : std::string();
    if (!keepMarkerAtEnd(quit_pending_, open.user_stop || run.stoppedByUser())) storage_.clearRunningMarker(marker_dir);

    std::size_t played = 0;
    if (t) {
        for (const auto& r : t->results()) played += r.voided ? 0 : 1;
    }
    std::string verdict;
    const auto error = run.error();
    if (!error.empty()) {
        verdict = trf("Error: {}", error);
    } else if (const auto failed = run.startupFailed()) {
        verdict = trf("Interrupted: {} could not start: {}", failed->engine, failed->reason);
    } else if (run.abnormal() && !run.stoppedByUser()) {
        verdict = TR("Interrupted: an engine crashed or stopped responding (see the Log)");
    } else if (run.stoppedByUser()) {
        verdict = TR("Stopped");
    } else {
        verdict = TR("Finished");
    }
    open.finish_message =
        trf("{} · {} games · {}", verdict, played,
            ui::formatDuration(std::chrono::duration<double>(run.info().finished - run.info().started).count()));
    Console::instance().add(trf("{}: {}", name, open.finish_message), !error.empty());
    if (setup_.notify && !open.quick_test) notifyRunFinished(window_, name, open.finish_message);
    tournaments_dirty_ = true;
    unfinished_        = unfinishedMatch(storage_.runsDir());

    const bool completed = error.empty() && !run.stoppedByUser() && !run.abnormal();
    if (completed && !queue_.empty() && !quit_pending_) startFromQueue(true);
}

void App::drawRunTabs() {
    if (runs_.size() < 2) return;
    // A tab per tournament: its name, a dot while it plays; × closes one that does not play.
    int show = -1, close = -1;
    if (ImGui::BeginTabBar("##runs", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)) {
        for (std::size_t i = 0; i < runs_.size(); ++i) {
            const auto& r = *runs_[i].run;
            const auto* t = r.tournament();
            auto label    = t ? t->spec().name : std::string(TR("No match"));
            if (r.running()) label = (r.paused() ? "⏸ " : "● ") + label;
            label += "###run" + std::to_string(reinterpret_cast<uintptr_t>(runs_[i].run.get()));
            bool keep           = true;
            const auto flags    = i == shown_run_ && tab_select_frames_ > 0 ? ImGuiTabItemFlags_SetSelected : 0;
            const bool selected = ImGui::BeginTabItem(label.c_str(), r.running() ? nullptr : &keep, flags);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", r.running()                       ? runStatusText(i).c_str()
                                        : runs_[i].finish_message.empty() ? (t ? t->dir().string().c_str() : "")
                                                                          : runs_[i].finish_message.c_str());
            }
            if (selected) {
                if (i != shown_run_ && tab_select_frames_ == 0) show = static_cast<int>(i);
                ImGui::EndTabItem();
            }
            if (!keep) close = static_cast<int>(i);
        }
        ImGui::EndTabBar();
    }
    if (tab_select_frames_ > 0) --tab_select_frames_;
    if (close >= 0) {
        closeRun(static_cast<std::size_t>(close));
    } else if (show >= 0) {
        showRun(static_cast<std::size_t>(show));
    }
}

std::string App::runStatusText(std::size_t index) const {
    if (index == shown_run_) return runStatusText();
    const auto& r = *runs_[index].run;
    if (!r.running()) return runs_[index].finish_message;
    const auto* t      = r.tournament();
    std::size_t played = 0;
    if (t) {
        for (const auto& g : t->results()) played += g.voided ? 0 : 1;
    }
    const auto total = r.info().total_games;
    const auto state = r.stopping() ? std::string(TR("Stopping…"))
                       : r.paused() ? std::string(TR("Paused"))
                                    : std::string(TR("Running"));
    return trf("{} · {} / {} games · {} playing", state, played, total ? fmt::format("{}", total) : std::string("∞"),
               r.runningGames());
}

void App::drawRunControls(std::size_t index) {
    if (index >= runs_.size()) return;
    auto& open = runs_[index];
    auto& r    = *open.run;
    if (!r.running()) return;
    const auto* t   = r.tournament();
    const auto dir  = t ? fs::absolute(t->dir()).lexically_normal().string() : std::string();
    const auto mark = [&](bool paused) {
        if (!open.quick_test) storage_.setRunningMarker(RunningMarker{dir, paused, open.resume_guarded, open.weight});
    };
    if (r.paused()) {
        if (ui::iconButton("resume", ui::Icon::Play, TR("Resume"), !r.stopping() && !r.stoppingWhenIdle())) {
            r.setPaused(false);
            mark(false);
        }
    } else if (ui::iconButton("pause", ui::Icon::Pause, TR("Pause"), !r.stopping())) {
        r.setPaused(true);
        mark(true);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Pause: no new pair starts. Running games, and the second game of their "
                                   "pair,\nplay to the end; its cores go to the other tournaments."));
    }
    ImGui::SameLine();
    if (ui::iconButton("stop", ui::Icon::Stop, TR("Stop"), !r.stopping())) ImGui::OpenPopup("stop_menu");
    if (ImGui::BeginPopup("stop_menu")) {
        const bool now = ImGui::MenuItem(TR("Stop now (running games are lost)"));
        if (ImGui::MenuItem(TR("Finish the running games, then stop")) || now) {
            open.user_stop = true;
            if (index == shown_run_) user_stop_ = true;
            if (now) {
                r.stopNow();
            } else {
                r.stopWhenIdle();
            }
            storage_.clearRunningMarker(dir);
        }
        ImGui::EndPopup();
    }
    if (!r.remote()) return;  // the quick test is not shared out

    // Its weight: its share of the cores against the others'.
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("Weight"));
    ImGui::SameLine();
    float weight = static_cast<float>(open.weight);
    ImGui::SetNextItemWidth(px(110));
    if (ImGui::SliderFloat("##weight", &weight, 0.25f, 8.0f, "%.2g", ImGuiSliderFlags_Logarithmic)) {
        open.weight = std::clamp(static_cast<double>(weight), 0.25, 8.0);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) mark(r.paused());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Its share of the cores against the other tournaments': a tournament of weight 2 "
                                   "gets twice the cores of one of weight 1, up to its own concurrency. Each "
                                   "tournament gets one game's worth first, while the budget allows (Settings)."));
    }
    // What it may use, and what holds its next game back.
    const auto allowance = r.allowance();
    const auto used      = r.inFlight();
    ImGui::SameLine();
    std::string text;
    if (allowance.threads < 0) {
        text = trn("{} core in use", "{} cores in use", used.threads, used.threads);
    } else {
        text = trf("{} of {} cores", used.threads, allowance.threads);
    }
    const auto wait = r.waiting();
    if (wait == tour::Wait::Memory) text += " · " + std::string(TR("games wait for memory"));
    if (wait == tour::Wait::Gpus) text += " · " + std::string(TR("games wait for a GPU"));
    if (wait == tour::Wait::Cores && !r.paused()) text += " · " + std::string(TR("games wait for cores"));
    ImGui::PushStyleColor(ImGuiCol_Text, wait == tour::Wait::Memory || wait == tour::Wait::Gpus
                                             ? ui::WARN
                                             : ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

tour::Budget App::machineBudget() const {
    tour::Budget b;
    b.threads = budget_setting_.cores > 0 ? budget_setting_.cores : std::max(1, topology_.physical - 1);
    if (budget_setting_.memory_mb > 0) {
        b.memory_mb = static_cast<uint64_t>(budget_setting_.memory_mb);
    } else if (installed_mb_ > 0) {
        b.memory_mb = installed_mb_ * MachineBudget::MEMORY_SHARE / 100;
    }
    b.gpus = budget_setting_.gpus;
    return b;
}

int App::localThreads() const {
    int threads = 0;
    // The quick test: its games, each its larger engine's threads.
    for (const auto& r : runs_) {
        const auto* t = r.run->tournament();
        if (r.run->remote() || !r.run->running() || !t) continue;
        threads += r.run->runningGames() * tour::largestGame(t->spec()).threads;
    }
    // The analysis engine of the Board page, when there is one, counts here too.
    return threads;
}

namespace {

// A tournament playing as the arbiter sees it. Paused, or stopping once its games are over, it keeps what its running
// games and the second games of their pairs need, and gives the rest to the others.
tour::Claim claimOf(const Run& run, double weight) {
    tour::Claim c;
    const auto* t = run.tournament();
    if (!t || !run.running() || run.stopping()) {
        c.active = false;
        return c;
    }
    c.weight  = weight;
    c.game    = tour::largestGame(t->spec());
    c.ceiling = std::max(1, t->spec().conditions.concurrency);
    if (run.paused() || run.stoppingWhenIdle()) {
        const auto* remote = dynamic_cast<const RemoteRun*>(&run);
        const int need     = run.runningGames() + (remote ? remote->status().open_pairs : 0);
        c.active           = need > 0;
        c.ceiling          = std::min(c.ceiling, std::max(1, need));
    }
    return c;
}

}  // namespace

void App::arbitrate() {
    auto budget    = machineBudget();
    budget.threads = std::max(1, budget.threads - localThreads());
    // By weight, the heaviest first (the first gets one game always); the order they started in between equals.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        if (runs_[i].run->remote() && runs_[i].run->running()) order.push_back(i);
    }
    if (order.empty()) return;
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return runs_[a].weight > runs_[b].weight; });
    std::vector<tour::Claim> claims;
    for (const auto i : order) claims.push_back(claimOf(*runs_[i].run, runs_[i].weight));
    auto allowances = tour::share(budget, claims);
    // A tournament alone plays as many games as its concurrency says, as it always has: the cores are shared between
    // tournaments. Its memory and GPUs stay within the budget.
    const auto active = std::count_if(claims.begin(), claims.end(), [](const tour::Claim& c) { return c.active; });
    for (std::size_t k = 0; k < order.size(); ++k) {
        if (active == 1 && claims[k].active) allowances[k].threads = -1;
        runs_[order[k]].run->setAllowance(allowances[k]);
    }
}

int App::freeCores() const {
    int free = machineBudget().threads - localThreads();
    for (const auto& r : runs_) {
        const auto* t = r.run->tournament();
        if (!r.run->remote() || !r.run->running() || !t) continue;
        free -= std::max(1, t->spec().conditions.concurrency) * tour::largestGame(t->spec()).threads;
    }
    return free;
}

bool App::queueBeside(std::size_t index) const {
    if (!budget_setting_.queue_when_free || index >= queue_.size()) return false;
    return freeCores() >= setupGameCost(queue_[index].setup, engines_).threads;
}

tour::Allowance App::allowanceFor(const tour::TournamentSpec& spec, double weight) const {
    auto budget    = machineBudget();
    budget.threads = std::max(1, budget.threads - localThreads());
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        if (runs_[i].run->remote() && runs_[i].run->running()) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return runs_[a].weight > runs_[b].weight; });
    std::vector<tour::Claim> claims;
    for (const auto i : order) claims.push_back(claimOf(*runs_[i].run, runs_[i].weight));
    tour::Claim mine;
    mine.weight  = weight;
    mine.game    = tour::largestGame(spec);
    mine.ceiling = std::max(1, spec.conditions.concurrency);
    // Among the others by its weight.
    std::size_t at = 0;
    while (at < order.size() && runs_[order[at]].weight >= weight) ++at;
    claims.insert(claims.begin() + static_cast<std::ptrdiff_t>(at), mine);
    auto allowances   = tour::share(budget, claims);
    auto out          = allowances[at];
    const auto active = std::count_if(claims.begin(), claims.end(), [](const tour::Claim& c) { return c.active; });
    if (active == 1) out.threads = -1;
    return out;
}

std::vector<App::CoreUse> App::coreShare() const {
    std::vector<CoreUse> out;
    for (const auto& r : runs_) {
        const auto* t = r.run->tournament();
        if (!r.run->running() || !t || !r.run->remote()) continue;
        out.push_back({t->spec().name, r.run->allowance().threads, r.run->inFlight().threads});
    }
    if (const int local = localThreads(); local > 0)
        out.push_back({TR("This window (quick test, analysis)"), local, local});
    return out;
}

void App::drawCoreShare() {
    const auto share = coreShare();
    const int budget = machineBudget().threads;
    if (share.empty() || budget <= 0) return;
    // A bar of the budget, a part per tournament: what it uses, darker, in what it may use.
    const float width = ImGui::GetContentRegionAvail().x;
    const float h     = px(10);
    const auto pos    = ImGui::GetCursorScreenPos();
    auto* draw        = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), px(3));
    float x = pos.x;
    for (std::size_t i = 0; i < share.size(); ++i) {
        const auto& u     = share[i];
        const int allowed = u.allowed < 0 ? budget : u.allowed;
        const float w     = width * static_cast<float>(std::min(allowed, budget)) / static_cast<float>(budget);
        const float used  = width * static_cast<float>(std::min(u.in_use, budget)) / static_cast<float>(budget);
        const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImPlot::GetColormapColor(static_cast<int>(i)));
        draw->AddRectFilled(ImVec2(x, pos.y), ImVec2(x + w, pos.y + h), (color & 0x00FFFFFF) | 0x50000000);
        draw->AddRectFilled(ImVec2(x, pos.y), ImVec2(x + std::min(used, w), pos.y + h), color);
        x += w;
    }
    ImGui::Dummy(ImVec2(width, h));
    std::string line = trn("{} core shared:", "{} cores shared:", budget, budget);
    for (const auto& u : share) {
        line += "  " + (u.allowed < 0 ? fmt::format("{} {}", u.label, u.in_use)
                                      : fmt::format("{} {}/{}", u.label, u.in_use, u.allowed));
    }
    ImGui::TextDisabled("%s", line.c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Each tournament: the cores its games use, of those it may use. The budget is set "
                                   "in Settings; the weights on each card decide who gets more."));
    }
}

void App::drawMachineBudget(float label) {
    bool changed        = false;
    auto& b             = budget_setting_;
    const auto resolved = machineBudget();

    ui::label(TR("Cores for the tournaments"), label);
    bool automatic = b.cores == 0;
    if (ImGui::Checkbox(TR("Automatic##cores"), &automatic)) {
        b.cores = automatic ? 0 : resolved.threads;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(automatic);
    int cores = resolved.threads;
    ImGui::SetNextItemWidth(px(160));
    if (ImGui::SliderInt("##budget_cores", &cores, 1, std::max(1, topology_.logical), TR("%d cores"))) {
        b.cores = cores;
        changed = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", trf("The engine threads the tournaments playing at once share, by their weights "
                                    "(each card of the home page). Automatic: the physical cores less one, {} here. "
                                    "A tournament alone plays as many games as its concurrency says.",
                                    std::max(1, topology_.physical - 1))
                                    .c_str());
    }

    ui::label(TR("Memory for the engines"), label);
    automatic = b.memory_mb == 0;
    if (ImGui::Checkbox(TR("Automatic##memory"), &automatic)) {
        b.memory_mb = automatic ? 0 : static_cast<int>(resolved.memory_mb);
        changed     = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(automatic);
    int memory = static_cast<int>(resolved.memory_mb);
    ImGui::SetNextItemWidth(px(160));
    const int most = static_cast<int>(std::max<uint64_t>(1024, installed_mb_));
    if (ImGui::SliderInt("##budget_memory", &memory, 256, most, formatMemory(static_cast<uint64_t>(memory)).c_str(),
                         ImGuiSliderFlags_Logarithmic)) {
        b.memory_mb = memory;
        changed     = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", trf("A game starts only when the Hash of its two engines, and what each engine process "
                                    "takes besides ({} MB, and {} MB per thread), fit in what the games playing leave: "
                                    "no tournament swaps. Automatic: {} % of the installed memory.",
                                    tour::PROCESS_MB, tour::THREAD_MB, MachineBudget::MEMORY_SHARE)
                                    .c_str());
    }

    ui::label(TR("GPUs"), label);
    int gpus = b.gpus;
    ImGui::SetNextItemWidth(px(160));
    if (ImGui::SliderInt("##budget_gpus", &gpus, 0, 16, gpus == 0 ? TR("not counted") : "%d")) {
        b.gpus  = std::max(0, gpus);
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("No more engines marked \"Uses a GPU\" (on their card) play at once than there are "
                                   "GPUs, in a tournament and across tournaments: two games never share a GPU without "
                                   "you knowing."));
    }
    if (changed) storage_.setMachineBudget(b);
}

}  // namespace fcg
