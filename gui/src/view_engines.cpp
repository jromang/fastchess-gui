// "Engines": the engine list and the UCI option editor; "Console": fastchess output.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include <tour/rating_import.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool editText(const char* id, std::string& value, float width = -FLT_MIN) {
    char buffer[4096];
    const auto n = std::min(value.size(), sizeof(buffer) - 1);
    std::copy_n(value.data(), n, buffer);
    buffer[n] = 0;

    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(id, buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

// A text of several lines, of any length.
bool editLines(const char* id, std::string& value, ImVec2 size) {
    const auto resize = [](ImGuiInputTextCallbackData* data) {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
            auto* text = static_cast<std::string*>(data->UserData);
            text->resize(static_cast<std::size_t>(data->BufTextLen));
            data->Buf = text->data();
        }
        return 0;
    };
    return ImGui::InputTextMultiline(id, value.data(), value.capacity() + 1, size, ImGuiInputTextFlags_CallbackResize,
                                     resize, &value);
}

// What is wrong with the engine's program, empty when nothing is.
std::string programProblem(const EngineEntry& e) {
    switch (e.program_state) {
        case ProgramState::Missing:
            return trf("Program not found: {}", e.cmd);
        case ProgramState::MissingDir:
            return trf("Working directory not found: {}", e.dir);
        case ProgramState::Changed:
            return TR("The program changed since it was last read (rebuilt or replaced).");
        default:
            return "";
    }
}

bool lessCaseInsensitive(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

// Counts a read or a check in g_probes_in_flight until its thread is done with fastchess' configuration.
struct InFlight {
    InFlight()                           = default;
    InFlight(const InFlight&)            = delete;
    InFlight& operator=(const InFlight&) = delete;
    ~InFlight() { g_probes_in_flight--; }
};

// The date and time of a check, a read or a test, as the engine list shows it.
std::string nowText() {
    char when[32];
    const auto now = std::time(nullptr);
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", std::localtime(&now));
    return when;
}

bool containsCaseInsensitive(const std::string& text, const std::string& needle) {
    if (needle.empty()) return true;
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return lower(text).find(lower(needle)) != std::string::npos;
}

}  // namespace

std::string App::uniqueEngineName(const std::string& base, int ignore_index) const {
    auto taken = [&](const std::string& name) {
        for (int i = 0; i < static_cast<int>(engines_.size()); ++i) {
            if (i != ignore_index && engines_[i].name == name) return true;
        }
        return false;
    };

    const auto root = base.empty() ? std::string("engine") : base;
    if (!taken(root)) return root;
    for (int n = 2;; ++n) {
        const auto candidate = fmt::format("{} ({})", root, n);
        if (!taken(candidate)) return candidate;
    }
}

void App::renameEngine(int index, const std::string& name) {
    if (index < 0 || index >= static_cast<int>(engines_.size())) return;
    const auto old       = engines_[index].name;
    const auto now       = uniqueEngineName(name, index);
    engines_[index].name = now;
    if (now == old) return;

    // Everything that refers to it by name follows.
    const auto follow = [&](std::string& ref) {
        if (ref != old) return false;
        ref = now;
        return true;
    };
    const auto followSetup = [&](MatchSetup& s) {
        bool changed = follow(s.engine_a);
        changed |= follow(s.engine_b);
        for (auto& p : s.participants) changed |= follow(p.name);
        return changed;
    };
    if (followSetup(setup_)) saveSetupSoon();
    bool queue = false;
    for (auto& q : queue_) queue |= followSetup(q.setup);
    if (queue) storage_.saveQueue(queue_);
    for (auto& n : probe_queue_) follow(n);
    for (auto& n : check_queue_) follow(n);
    // A start put off: a quick test of this engine, pressed while its first read gave it its id name, starts under
    // that name.
    if (pending_start_ && pending_start_->rename(old, now)) pending_start_->start = quickTestLater(now);
    follow(quick_test_engine_);
    follow(quick_test_opponent_);
    follow(relaunch_engine_);
    follow(edit_engine_);
    loadSuiteSetup();  // before it is changed and written
    bool suite = false;
    for (auto& n : suite_setup_.engines) suite |= follow(n);
    if (suite) writeJson(storage_.root() / "suite.json", suite_setup_);
    if (engine_marked_.erase(old)) engine_marked_.insert(now);
}

void App::removeEngines(const std::vector<std::string>& names) {
    const int selected = selected_engine_;
    for (int i = static_cast<int>(engines_.size()) - 1; i >= 0; --i) {
        const auto name = engines_[i].name;
        if (std::find(names.begin(), names.end(), name) == names.end()) continue;
        // A read or a check still running belongs to an index: keep it on its engine, or drop it with it.
        for (int* busy : {&probe_index_, &check_index_, &selected_engine_}) {
            if (*busy == i)
                *busy = -1;
            else if (*busy > i)
                --*busy;
        }
        std::erase(probe_queue_, name);
        std::erase(check_queue_, name);
        engine_marked_.erase(name);
        engines_.erase(engines_.begin() + i);
    }
    // The one removed: the engine that took its place.
    if (selected_engine_ < 0 && selected >= 0)
        selected_engine_ = std::min(selected, static_cast<int>(engines_.size()) - 1);
    storage_.saveEngines(engines_);
}

void App::queueChecks(const std::vector<std::string>& names) {
    for (const auto& name : names) {
        const bool checking = check_.valid() && check_index_ >= 0 && engines_[check_index_].name == name;
        if (!checking && std::find(check_queue_.begin(), check_queue_.end(), name) == check_queue_.end())
            check_queue_.push_back(name);
    }
}

void App::addEngine(const std::string& path) {
    EngineEntry entry;
    entry.cmd  = path;
    entry.name = uniqueEngineName(fs::path(path).stem().string(), -1);

    engines_.push_back(std::move(entry));
    selected_engine_ = static_cast<int>(engines_.size()) - 1;
    storage_.saveEngines(engines_);

    startProbe(selected_engine_);
}

void App::refreshPrograms() {
    // What the file system tells at once; the CRCs (seconds for hundreds of large engines after an import) are read
    // in the background, see pollFingerprints.
    std::vector<std::string> programs;
    for (auto& e : engines_) {
        if (programNeedsCrc(e)) {
            if (std::find(programs.begin(), programs.end(), e.cmd) == programs.end()) programs.push_back(e.cmd);
            continue;
        }
        // A rebuilt engine has its options read again, so that the option table matches the new build.
        if (e.program_state == ProgramState::Changed && !probing(e.name)) probe_queue_.push_back(e.name);
    }
    if (programs.empty()) return;
    if (fingerprints_.valid()) {
        refresh_programs_again_ = true;
        return;
    }
    fingerprints_ = std::async(std::launch::async, [programs, cancel = &fingerprints_cancel_] {
        std::vector<std::pair<std::string, tour::FileFingerprint>> out;
        for (const auto& p : programs) {
            if (*cancel) break;
            out.emplace_back(p, tour::fingerprint(p));
        }
        return out;
    });
}

void App::pollFingerprints() {
    if (!fingerprints_.valid() || fingerprints_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    bool save = false;
    for (const auto& [program, fingerprint] : fingerprints_.get()) {
        for (auto& e : engines_) {
            // Not if it changed meanwhile: another program, or found out since.
            if (e.cmd != program || !programNeedsCrc(e)) continue;
            save |= applyFingerprint(e, fingerprint);
            if (e.program_state == ProgramState::Changed && !probing(e.name)) probe_queue_.push_back(e.name);
        }
    }
    if (save) storage_.saveEngines(engines_);
    if (std::exchange(refresh_programs_again_, false)) refreshPrograms();
}

bool App::inSetup(const std::string& name) const {
    if (setup_.format == TournamentFormat::Match) return name == setup_.engine_a || name == setup_.engine_b;
    return std::any_of(setup_.participants.begin(), setup_.participants.end(),
                       [&](const auto& p) { return p.name == name; });
}

void App::startProbe(int index) {
    if (index < 0 || index >= static_cast<int>(engines_.size())) return;
    const auto name = engines_[index].name;
    // Asked for again while it is read (a new protocol, a new program): that read is dropped when it ends, and the
    // engine read again.
    if (probe_.valid() && index == probe_index_) {
        probe_again_ = true;
        return;
    }
    // Asked for by name (a button, a new protocol, a check): first in the queue, not dropped. While a start waits, it
    // reads what it needs first (see advanceStart).
    if (probe_.valid() || pending_start_) {
        std::erase(probe_queue_, name);
        probe_queue_.insert(probe_queue_.begin(), name);
        return;
    }
    launchProbe(index);
}

void App::launchProbe(int index) {
    std::erase(probe_queue_, engines_[index].name);
    probe_index_       = index;
    probe_entry_       = engines_[index];
    probe_again_       = false;
    probe_message_     = TR("Reading the engine…");
    probe_interrupted_ = std::make_shared<std::atomic_bool>(false);
    g_probes_in_flight++;

    probe_ = std::async(std::launch::async, [entry = engines_[index], interrupted = probe_interrupted_] {
        const InFlight in_flight;
        return probeEngine(entry, interrupted.get());
    });
}

bool App::probing(const std::string& name) const {
    if (probe_.valid() && probe_index_ >= 0 && probe_index_ < static_cast<int>(engines_.size()) &&
        engines_[probe_index_].name == name) {
        return true;
    }
    return std::find(probe_queue_.begin(), probe_queue_.end(), name) != probe_queue_.end();
}

namespace {

// What the start put off under `key` starts, for the message that refuses another one.
std::string startedByKey(const std::string& key) {
    if (key == "queue") return TR("the queue");
    if (key.rfind("resume:", 0) == 0) return TR("the resume of the interrupted tournament");
    if (key.rfind("test:", 0) == 0) return trf("the quick test of {}", key.substr(5));
    return TR("the new tournament");
}

}  // namespace

bool App::startAfterReads(const std::string& key, std::vector<std::string> needs, std::function<void()> start,
                          std::function<void()> failed) {
    // A tournament plays: the start says so itself, and nothing here may end its engines.
    if (run_.running()) return false;
    switch (startAsked(pending_start_ ? pending_start_->key : std::string(), key)) {
        case StartAsked::Go:
            break;
        case StartAsked::Waiting:  // pressed again, or from another place: it goes on waiting, as it was
            return true;
        case StartAsked::Refused:  // the one waiting (an automatic queue start, perhaps) is not dropped silently
            start_error_ =
                trf("Not started: another start is waiting ({}). Cancel it first.", startedByKey(pending_start_->key));
            Console::instance().add(start_error_, true);
            return true;
    }
    start_error_.clear();
    pending_start_         = PendingStart{};
    pending_start_->key    = key;
    pending_start_->needs  = std::move(needs);
    pending_start_->start  = std::move(start);
    pending_start_->failed = std::move(failed);
    if (advanceStart()) {
        pending_start_.reset();
        return false;
    }
    return true;
}

bool App::advanceStart() {
    // What ended meanwhile first: a read it needs is applied, and what ended is not interrupted for nothing.
    if (probe_.valid() && probe_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) applyProbe();
    if (check_.valid() && check_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) pollCheck();
    if (!pending_start_) return false;  // a read it needed failed: it does not start

    auto& pending     = *pending_start_;
    const auto ending = [](const auto& job, const auto& interrupted) {
        return job.valid() && interrupted && *interrupted;
    };
    const auto engine_name = [&](int index) {
        return index >= 0 && index < static_cast<int>(engines_.size()) ? engines_[index].name : std::string("?");
    };
    // Interrupted reads and checks still ending: their engines again, one may have been starting meanwhile (not
    // while a test suite runs, whose engines would end too).
    if (ending(probe_, probe_interrupted_) || ending(check_, check_interrupted_)) {
        if (!suite_runner_.running()) interruptEngineProcesses();
        return false;
    }

    const auto reading  = probe_.valid() ? engine_name(probe_index_) : std::string();
    const auto checking = check_.valid() ? engine_name(check_index_) : std::string();
    const auto gate     = startGate(pending.needs, reading, checking, probe_queue_, suite_runner_.running());
    pending.waiting_for.clear();
    pending.waiting_check = false;
    pending.waiting_suite = false;
    switch (gate.action) {
        case StartGate::Action::Start:
            return true;
        case StartGate::Action::Wait:
            pending.waiting_for   = gate.engine;
            pending.waiting_check = gate.check;
            pending.waiting_suite = gate.suite;
            return false;
        case StartGate::Action::Read:
            pending.waiting_for = gate.engine;
            for (int i = 0; i < static_cast<int>(engines_.size()); ++i) {
                if (engines_[i].name == gate.engine) {
                    launchProbe(i);
                    return false;
                }
            }
            std::erase(probe_queue_, gate.engine);  // gone from the library meanwhile
            return false;
        case StartGate::Action::Interrupt:
            // The flags first: a thread whose engine ended gives up rather than start it again.
            if (gate.interrupt_read) {
                *probe_interrupted_ = true;
                Console::instance().add(
                    trf("The read of {} is interrupted for the start: it is done again later.", reading));
            }
            if (gate.interrupt_check) {
                *check_interrupted_ = true;
                Console::instance().add(
                    trf("The check of {} is interrupted for the start: it is done again later.", checking));
            }
            interruptEngineProcesses();
            return false;
    }
    return false;
}

void App::pollStart() {
    // A start whose read failed goes on as it said (the queue, with its next tournament).
    if (start_failed_) {
        const auto failed = std::exchange(start_failed_, {});
        if (!quit_ && !quit_pending_ && !run_.running()) failed();
    }
    if (!pending_start_) return;
    if (quit_ || quit_pending_ || run_.running()) {
        pending_start_.reset();
        return;
    }
    if (!advanceStart()) return;
    const auto start = std::exchange(pending_start_, std::nullopt)->start;
    start();
}

bool App::drawStartWaiting(const char* id, const std::string& key, bool icon) {
    if (!startPending(key)) return false;
    const auto& pending = *pending_start_;
    const auto label    = pending.waiting_suite         ? std::string(TR("Waiting for the test suite…"))
                          : pending.waiting_for.empty() ? std::string(TR("Starting…"))
                                                        : trf("Waiting for {}…", pending.waiting_for);
    if (icon) {
        (void)ui::iconButton(id, ui::Icon::Play, label.c_str(), false);
    } else {
        ImGui::BeginDisabled();
        ImGui::Button(fmt::format("{}###{}", label, id).c_str());
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::PushID(id);
    if (ImGui::SmallButton(TR("Cancel"))) pending_start_.reset();
    ImGui::PopID();
    return true;
}

std::string App::startWaitingText(const std::string& key) const {
    if (!startPending(key)) return {};
    if (pending_start_->waiting_suite) return TR("It starts once the test suite ends (or is stopped).");
    if (pending_start_->waiting_for.empty()) return {};
    const auto& name = pending_start_->waiting_for;
    return pending_start_->waiting_check ? trf("It starts once the check of {} ends.", name)
                                         : trf("It starts once the options of {} are read.", name);
}

void App::pollProbe() {
    pollFingerprints();

    // Start the next queued probe when idle, but never while a run is being set up, or a start waits: that start
    // reads what it needs itself (see advanceStart). The engines of the setup first.
    if (!probe_.valid() && !probe_queue_.empty() && !run_.running() && !pending_start_) {
        auto next = std::find_if(probe_queue_.begin(), probe_queue_.end(), [&](const auto& n) { return inSetup(n); });
        if (next == probe_queue_.end()) next = probe_queue_.begin();
        const auto name = *next;
        probe_queue_.erase(next);
        for (int i = 0; i < static_cast<int>(engines_.size()); ++i) {
            if (engines_[i].name == name) {
                startProbe(i);
                break;
            }
        }
    }

    if (probe_.valid() && probe_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) applyProbe();
}

void App::applyProbe() {
    ProbeResult result;
    try {
        result = probe_.get();
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    const int index        = probe_index_;
    const bool again       = std::exchange(probe_again_, false);
    const bool interrupted = probe_interrupted_ && *probe_interrupted_;
    probe_index_           = -1;

    if (index < 0 || index >= static_cast<int>(engines_.size())) return;
    auto& e = engines_[index];

    // Interrupted for a start: read again, first, once reads go on.
    if (interrupted) {
        std::erase(probe_queue_, e.name);
        probe_queue_.insert(probe_queue_.begin(), e.name);
        probe_message_.clear();
        return;
    }

    // Asked for again, or given another program, arguments, folder or protocol while it was read: the result is the
    // old engine's. It is read again, first.
    if (again || e.cmd != probe_entry_.cmd || e.args != probe_entry_.args || e.dir != probe_entry_.dir ||
        e.protocol != probe_entry_.protocol) {
        std::erase(probe_queue_, e.name);
        probe_queue_.insert(probe_queue_.begin(), e.name);
        probe_message_.clear();
        return;
    }
    e.read_by = FCG_VERSION;

    if (!result.ok) {
        probe_message_ = trf("Failed: {}", result.error);
        // A start waited for it: it would play an engine that cannot be read. The queue goes on with its next
        // tournament, at the next frame (this may be called while a start is asked for).
        if (pending_start_ && std::find(pending_start_->needs.begin(), pending_start_->needs.end(), e.name) !=
                                  pending_start_->needs.end()) {
            start_failed_ = std::move(pending_start_->failed);
            pending_start_.reset();
            start_error_ = trf("Not started: the options of {} cannot be read.", e.name);
            Console::instance().add(start_error_, true);
        }
        // The program as it is now: a rebuilt engine that cannot be read is not read again at every focus change.
        if (result.program.size > 0) {
            e.program = result.program;
            refreshProgram(e);
        }
        e.check_ok      = 0;
        e.check_message = result.error;
        e.check_report  = result.output;
        e.check_when    = nowText();
        storage_.saveEngines(engines_);
        return;
    }

    const bool first_probe = e.id_name.empty();
    const bool rebuilt     = e.program_state == ProgramState::Changed;
    e.protocol             = result.protocol;
    e.program              = result.program;
    refreshProgram(e);
    if (rebuilt) Console::instance().add(trf("{} changed: its options were read again.", e.name));

    e.id_name     = result.id_name;
    e.id_author   = result.id_author;
    e.uci_options = result.options;
    pruneOptions(e);
    if (first_probe) showWdlByDefault(e);
    if (first_probe && result.restart && !e.restart) {
        e.restart = true;
        Console::instance().add(
            trf("{} asks to be started again for every game: Restart the engine for every game is ticked.", e.name));
    }

    // A freshly added engine takes the name it reports.
    if (first_probe && !e.id_name.empty() && e.name == uniqueEngineName(fs::path(e.cmd).stem().string(), index))
        renameEngine(index, e.id_name);

    probe_message_ = trn("{} option read.", "{} options read.", e.uci_options.size(), e.uci_options.size());
    storage_.saveEngines(engines_);
}

std::vector<std::string> App::uncheckedEngines() const {
    std::vector<std::string> names;
    for (const auto& e : engines_) {
        const bool gone = e.program_state == ProgramState::Missing || e.program_state == ProgramState::MissingDir;
        if (!gone && e.check_ok < 0 && !e.cmd.empty() && !e.inactive) names.push_back(e.name);
    }
    return names;
}

void App::askEngineList() {
    dialogs_.openFile(Dialogs::Purpose::ImportEngineList,
                      {{TR("Engine lists"), "txt;json;cfg;ini"}, {TR("All files"), "*"}});
}

void App::drawEngines() {
    if (!ui::beginPanel(TR("Engines###engines"))) {
        ui::endPanel();
        return;
    }

    // List on the left.
    const float page   = ImGui::GetContentRegionAvail().x;
    const float list_w = ui::paneWidth(setup_.panes["engines"], page, px(260), px(520));
    ImGui::BeginChild("##engine_list", ImVec2(list_w, 0), ImGuiChildFlags_Borders);

    if (ImGui::Button(TR("Add…"))) {
#ifdef _WIN32
        dialogs_.openFile(Dialogs::Purpose::AddEngine, {{TR("Programs"), "exe"}, {TR("All files"), "*"}});
#else
        dialogs_.openFile(Dialogs::Purpose::AddEngine);
#endif
    }
    ui::sameLineOrWrap(TR("Import…"));
    if (ImGui::Button(TR("Import…"))) ImGui::OpenPopup("import_menu");
    if (ImGui::BeginPopup("import_menu")) {
        if (ImGui::MenuItem(TR("An engine list…"))) askEngineList();
        ImGui::TextDisabled("%s", TR("ChessGUI (ChsGUI_Eng.TXT), cutechess or Banksia (engines.json), Arena…"));
        if (ImGui::MenuItem(TR("Every engine of a folder…"))) dialogs_.openFolder(Dialogs::Purpose::ImportEngineFolder);
        if (const auto& folder = engineFolder(); !folder.empty()) {
            if (ImGui::MenuItem(TR("Refresh: the new engines of the folder"))) refreshEngineFolder();
            ImGui::TextDisabled("%s", folder.string().c_str());
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TR("A rating list, for the reference Elo…"))) openRatingImport();
        ImGui::TextDisabled("%s", TR("A rating list's text or web page, Ordo, CSV (name,elo)"));
        ImGui::EndPopup();
    }
    const auto duplicate = [&] {
        auto copy = engines_[selected_engine_];
        copy.name = uniqueEngineName(copy.name, -1);
        engines_.push_back(copy);
        selected_engine_ = static_cast<int>(engines_.size()) - 1;
        engine_marked_.clear();
        storage_.saveEngines(engines_);
    };
    // The engines an action applies to: those marked (Ctrl or Shift click), or the one selected.
    std::vector<std::string> chosen;
    for (const auto& e : engines_) {
        if (engine_marked_.count(e.name)) chosen.push_back(e.name);
    }
    if (chosen.empty() && selected_engine_ >= 0 && selected_engine_ < static_cast<int>(engines_.size()))
        chosen.push_back(engines_[selected_engine_].name);
    // The confirmation is opened at the end, in the list's window: a popup is found by the window it is opened in.
    bool remove_asked    = false;
    const auto askRemove = [&](std::vector<std::string> names) {
        remove_names_ = std::move(names);
        remove_asked  = !remove_names_.empty();
    };

    ui::sameLineOrWrap(TR("Duplicate"));
    ImGui::BeginDisabled(selected_engine_ < 0);
    if (ImGui::Button(TR("Duplicate")) && selected_engine_ >= 0) duplicate();
    ui::sameLineOrWrap(TR("Remove"));
    if (ImGui::Button(TR("Remove"))) askRemove(chosen);
    ImGui::EndDisabled();
    ui::sameLineOrWrap(TR("More…"));
    if (ImGui::Button(TR("More…"))) ImGui::OpenPopup("engines_more");
    if (ImGui::BeginPopup("engines_more")) {
        const auto unchecked = uncheckedEngines();
        std::vector<std::string> missing;
        for (const auto& e : engines_) {
            if (e.program_state == ProgramState::Missing || e.program_state == ProgramState::MissingDir)
                missing.push_back(e.name);
        }
        if (ImGui::MenuItem(trf("Check those not checked ({})", unchecked.size()).c_str(), nullptr, false,
                            !unchecked.empty()))
            queueChecks(unchecked);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", TR("One after the other, not while a tournament plays. Not the inactive ones."));
        if (ImGui::MenuItem(trf("Remove the missing programs ({})…", missing.size()).c_str(), nullptr, false,
                            !missing.empty()))
            askRemove(missing);
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal(TR("Remove the engine?###remove"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (remove_names_.size() == 1) {
            ImGui::Text(TR("Remove “%s” from the list? The file is not deleted."), remove_names_[0].c_str());
        } else {
            ImGui::TextUnformatted(trn("Remove {} engine from the list? The files are not deleted.",
                                       "Remove {} engines from the list? The files are not deleted.",
                                       remove_names_.size(), remove_names_.size())
                                       .c_str());
        }
        if (ImGui::Button(TR("Remove##confirm"))) {
            removeEngines(remove_names_);
            remove_names_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"))) {
            remove_names_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Search, groups and health.
    std::vector<std::string> groups;
    for (const auto& e : engines_) {
        for (const auto& g : e.groups) {
            if (std::find(groups.begin(), groups.end(), g) == groups.end()) groups.push_back(g);
        }
    }
    std::sort(groups.begin(), groups.end());
    {
        char buffer[128];
        const auto n = std::min(engine_search_.size(), sizeof(buffer) - 1);
        std::copy_n(engine_search_.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##engine_search", TR("Search: name, program, author, notes"), buffer,
                                     sizeof(buffer)))
            engine_search_ = buffer;

        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (!groups.empty()) {
            ImGui::SetNextItemWidth(half);
            if (ImGui::BeginCombo("##engine_group", engine_group_.empty() ? TR("All groups") : engine_group_.c_str())) {
                if (ImGui::Selectable(TR("All groups"), engine_group_.empty())) engine_group_.clear();
                for (const auto& g : groups) {
                    if (ImGui::Selectable(g.c_str(), g == engine_group_)) engine_group_ = g;
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
        }
        const char* statuses[] = {TR("Every state"),     TR("Works"),    TR("Failed or changed"), TR("Not checked"),
                                  TR("Program missing"), TR("WinBoard"), TR("Inactive")};
        ImGui::SetNextItemWidth(groups.empty() ? -FLT_MIN : half);
        ImGui::Combo("##engine_status", &engine_status_, statuses, IM_ARRAYSIZE(statuses));
    }

    // The engines several actions apply to.
    if (chosen.size() > 1) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(trn("{} selected", "{} selected", chosen.size(), chosen.size()).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Check##marked"))) queueChecks(chosen);
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Add to a group…"))) {
            group_to_add_.clear();
            ImGui::OpenPopup("add_to_group");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Remove…##marked"))) askRemove(chosen);
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Clear##marked"))) engine_marked_.clear();
    }
    if (ImGui::BeginPopup("add_to_group")) {
        ImGui::TextUnformatted(TR("Group"));
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        editText("##group_name", group_to_add_, px(220));
        // A group there is, to pick.
        for (const auto& g : groups) {
            if (containsCaseInsensitive(g, group_to_add_) && ImGui::Selectable(g.c_str(), g == group_to_add_))
                group_to_add_ = g;
        }
        ImGui::BeginDisabled(group_to_add_.empty());
        const bool add = ImGui::Button(TR("Add"));
        ImGui::EndDisabled();
        if (add) {
            for (auto& e : engines_) {
                if (std::find(chosen.begin(), chosen.end(), e.name) != chosen.end() &&
                    std::find(e.groups.begin(), e.groups.end(), group_to_add_) == e.groups.end()) {
                    e.groups.push_back(group_to_add_);
                }
            }
            storage_.saveEngines(engines_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!check_queue_.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", trn("{} engine waiting to be checked", "{} engines waiting to be checked",
                                      check_queue_.size(), check_queue_.size())
                                      .c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Stop##check_queue"))) check_queue_.clear();
    }

    ImGui::Separator();
    // In alphabetical order; engines_ keeps the order they were added in, which the indexes follow.
    std::vector<int> shown;
    for (int i = 0; i < static_cast<int>(engines_.size()); ++i) {
        const auto& e = engines_[i];
        if (!engine_group_.empty() && std::find(e.groups.begin(), e.groups.end(), engine_group_) == e.groups.end())
            continue;
        if (!engineMatches(e, engine_search_)) continue;
        const bool gone = e.program_state == ProgramState::Missing || e.program_state == ProgramState::MissingDir;
        const bool keep = engine_status_ == 1   ? engineWorks(e)
                          : engine_status_ == 2 ? !gone && (e.check_ok == 0 || e.program_state == ProgramState::Changed)
                          : engine_status_ == 3 ? !gone && e.check_ok < 0
                          : engine_status_ == 4 ? gone
                          : engine_status_ == 5 ? isWinboard(e)
                          : engine_status_ == 6 ? e.inactive
                                                : true;
        if (keep) shown.push_back(i);
    }
    std::stable_sort(shown.begin(), shown.end(),
                     [&](int a, int b) { return lessCaseInsensitive(engines_[a].name, engines_[b].name); });
    bool remove_one = false, duplicate_asked = false;
    int inactive_asked = -1;  // from the right click's menu: 1 inactive, 0 active again

    // Only the engines scroll: the buttons above and the legend below stay in sight.
    const char* legend =
        TR("✓ works · ! problem · ↔ WinBoard · grey: inactive · Ctrl or Shift click: several · right click: more");
    const float legend_h = engines_.empty()
                               ? 0.0f
                               : ImGui::CalcTextSize(legend, nullptr, false, ImGui::GetContentRegionAvail().x).y +
                                     ImGui::GetStyle().ItemSpacing.y * 2;
    ImGui::BeginChild("##engine_entries", ImVec2(0, -legend_h));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int i   = shown[row];
            const auto& e = engines_[i];
            std::string tag;
            if (e.name == setup_.engine_a) tag = "  [A]";
            if (e.name == setup_.engine_b) tag += "  [B]";

            // A mark for the health: program missing or changed, checked, failed, or a WinBoard engine.
            const auto problem = programProblem(e);
            const char* mark   = !problem.empty()  ? "!"
                                 : e.check_ok == 1 ? "✓"
                                 : e.check_ok == 0 ? "!"
                                 : isWinboard(e)   ? "↔"
                                                   : "·";
            const bool missing =
                e.program_state == ProgramState::Missing || e.program_state == ProgramState::MissingDir;
            ImGui::PushStyleColor(ImGuiCol_Text, missing            ? ui::BAD
                                                 : !problem.empty() ? ui::WARN
                                                 : e.check_ok == 1  ? ui::GOOD
                                                 : e.check_ok == 0  ? ui::WARN
                                                                    : ui::MUTED);
            ImGui::TextUnformatted(mark);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            const auto label  = fmt::format("{}{}##engine{}", e.name, tag, i);
            const bool marked = engine_marked_.count(e.name) > 0;
            if (e.inactive) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            const bool clicked = ImGui::Selectable(label.c_str(), selected_engine_ == i || marked);
            if (e.inactive) ImGui::PopStyleColor();
            if (clicked) {
                const auto& io = ImGui::GetIO();
                if (io.KeyCtrl) {
                    // The one selected is part of the marked ones.
                    if (engine_marked_.empty() && selected_engine_ >= 0 && selected_engine_ != i)
                        engine_marked_.insert(engines_[selected_engine_].name);
                    if (marked)
                        engine_marked_.erase(e.name);
                    else
                        engine_marked_.insert(e.name);
                } else if (io.KeyShift && selected_engine_ >= 0) {
                    // From the one selected to this one, as shown.
                    const auto from = std::find(shown.begin(), shown.end(), selected_engine_);
                    if (from != shown.end()) {
                        const int a = static_cast<int>(from - shown.begin());
                        for (int k = std::min(a, row); k <= std::max(a, row); ++k)
                            engine_marked_.insert(engines_[shown[k]].name);
                    }
                } else {
                    engine_marked_.clear();
                }
                if (!io.KeyShift) selected_engine_ = i;
            }
            if (ImGui::IsItemHovered()) {
                // What the mark means, then what was found.
                const std::string meaning = !problem.empty()  ? problem
                                            : e.check_ok == 1 ? std::string(TR("Checked: it works."))
                                            : e.check_ok == 0 ? std::string(TR("The last check or reading failed."))
                                            : isWinboard(e)   ? std::string(TR("A WinBoard engine."))
                                                              : std::string(TR("Not checked yet."));
                const bool more           = problem.empty() && !e.check_message.empty();
                ImGui::SetTooltip("%s %s%s%s%s%s%s%s", mark, meaning.c_str(), more ? "\n" : "",
                                  more ? e.check_message.c_str() : "", e.inactive ? "\n" : "",
                                  e.inactive ? TR("Inactive: out of the engine pickers.") : "",
                                  e.notes.empty() ? "" : "\n\n", e.notes.c_str());
            }
            if (ImGui::BeginPopupContextItem()) {
                if (!marked) {
                    selected_engine_ = i;
                    engine_marked_.clear();
                }
                if (ImGui::MenuItem(TR("Check"), nullptr, false, !check_.valid() && !pending_start_)) startCheck(i);
                const bool being_read = probe_.valid() && probe_index_ == i;
                if (ImGui::MenuItem(
                        probing(e.name) && !being_read ? TR("Read it next") : TR("Read the UCI options again"), nullptr,
                        false, !being_read))
                    startProbe(i);
                if (ImGui::MenuItem(TR("Duplicate"))) duplicate_asked = true;
                if (ImGui::MenuItem(TR("Inactive"), nullptr, e.inactive)) inactive_asked = e.inactive ? 0 : 1;
                if (ImGui::MenuItem(TR("Remove…"))) remove_one = true;
                ImGui::EndPopup();
            }
        }
    }
    if (engines_.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", TR("No engine yet. Click Add."));
        ImGui::PopTextWrapPos();
    }
    if (selected_engine_ >= 0 && ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        remove_one = true;
    }
    ImGui::EndChild();
    if (!engines_.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", legend);
        ImGui::PopTextWrapPos();
    }
    // Here, in the list's window: the confirmation is drawn there, and a popup is found by the window it is opened in.
    if (remove_one) {
        // Those marked, or the one right clicked, selected meanwhile.
        chosen.clear();
        for (const auto& e : engines_) {
            if (engine_marked_.count(e.name)) chosen.push_back(e.name);
        }
        if (chosen.empty() && selected_engine_ >= 0) chosen.push_back(engines_[selected_engine_].name);
        askRemove(chosen);
    }
    if (remove_asked) ImGui::OpenPopup(TR("Remove the engine?###remove"));
    if (duplicate_asked) duplicate();  // after the loop, which holds references into engines_
    if (inactive_asked >= 0) {
        // Those marked, or the one right clicked.
        chosen.clear();
        for (const auto& e : engines_) {
            if (engine_marked_.count(e.name)) chosen.push_back(e.name);
        }
        if (chosen.empty() && selected_engine_ >= 0) chosen.push_back(engines_[selected_engine_].name);
        setInactive(chosen, inactive_asked == 1);
    }

    ImGui::EndChild();
    if (ui::splitter("##engine_split", setup_.panes["engines"], page)) saveSetupSoon();

    ImGui::BeginChild("##engine_detail");
    if (selected_engine_ < 0 || selected_engine_ >= static_cast<int>(engines_.size())) {
        ui::emptyState(TR("Choose an engine in the list."));
        ImGui::EndChild();
        ui::endPanel();
        return;
    }

    auto& e            = engines_[selected_engine_];
    bool changed       = false;
    const bool in_use  = engineInUse(e.name);
    const float label  = 150.0f * ImGui::GetFontSize() / 16.0f;
    const float browse = ImGui::CalcTextSize("…").x + ImGui::GetStyle().FramePadding.x * 4;

    ImGui::BeginDisabled(in_use);

    ui::label(TR("Name"), label);
    {
        // Renamed when the edit ends, so that the names on the way never take the place of another engine's in
        // the setup or the queue. A name being typed when another engine is clicked still applies.
        if (name_draft_of_ != e.name) {
            const auto before = std::find_if(engines_.begin(), engines_.end(),
                                             [&](const EngineEntry& o) { return o.name == name_draft_of_; });
            if (before != engines_.end() && !name_draft_.empty() && name_draft_ != name_draft_of_) {
                renameEngine(static_cast<int>(before - engines_.begin()), name_draft_);
                changed = true;
            }
            name_draft_of_ = e.name;
            name_draft_    = e.name;
        }
        editText("##name", name_draft_);
        if (ImGui::IsItemDeactivatedAfterEdit() && !name_draft_.empty()) {
            renameEngine(selected_engine_, name_draft_);
            changed = true;
        }
        if (!ImGui::IsItemActive()) {
            name_draft_of_ = e.name;
            name_draft_    = e.name;
        }
    }

    ui::label(TR("Program"), label);
    changed |= editText("##cmd", e.cmd, ImGui::GetContentRegionAvail().x - browse - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SameLine();
    if (ImGui::Button("…##cmd_browse")) dialogs_.openFile(Dialogs::Purpose::EngineCommand, {}, e.cmd);

    ui::label(TR("Arguments"), label);
    changed |= editText("##args", e.args);

    ui::label(TR("Working directory"), label);
    changed |= editText("##dir", e.dir, ImGui::GetContentRegionAvail().x - browse - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SameLine();
    if (ImGui::Button("…##dir_browse")) dialogs_.openFolder(Dialogs::Purpose::EngineDir, engineWorkingDir(e));
    if (e.dir.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label);
        ImGui::TextDisabled("%s", TR("Empty: the program's directory."));
    }
    if (const auto problem = programProblem(e); !problem.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, e.program_state == ProgramState::Changed ? ui::WARN : ui::BAD);
        ImGui::TextUnformatted(problem.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (e.program_state == ProgramState::Missing) {
            if (ImGui::Button(TR("Locate…"))) dialogs_.openFile(Dialogs::Purpose::EngineCommand, {}, e.cmd);
        } else if (e.program_state == ProgramState::MissingDir) {
            if (ImGui::Button(TR("Choose…"))) dialogs_.openFolder(Dialogs::Purpose::EngineDir, e.dir);
        } else {
            ImGui::BeginDisabled(probe_.valid() && probe_index_ == selected_engine_);
            if (ImGui::Button(TR("Read it again"))) startProbe(selected_engine_);
            ImGui::EndDisabled();
        }
    }

    ui::label(TR("Protocol"), label);
    {
        const char* names[] = {TR("Automatic"), "UCI", "WinBoard"};
        int current         = e.protocol == "uci" ? 1 : e.protocol == "xboard" ? 2 : 0;
        ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 16.0f);
        if (ImGui::Combo("##protocol", &current, names, 3)) {
            e.protocol = current == 1 ? "uci" : current == 2 ? "xboard" : "";
            changed    = true;
            startProbe(selected_engine_);
        }
        ui::help(
            TR("WinBoard engines (xboard protocol) are run through a translation to UCI built into "
               "fastchess-gui. Automatic: found out when the engine is read."));
    }

    changed |= ImGui::Checkbox(TR("Restart the engine for every game"), &e.restart);
    ImGui::SameLine(0, ImGui::GetFontSize() * 1.5f);
    changed |= ImGui::Checkbox(TR("Inactive"), &e.inactive);
    ui::help(
        TR("Kept in the library, but out of the engine pickers (setup, suites, quick test, relaunch, adding to a "
           "tournament) and of Check those not checked. A tournament that has it already plays it as before."));

    // The usual start timeout is shown rather than 0, and saved as 0 when typed back.
    ui::label(TR("Start timeout (s)"), label);
    {
        const auto usual =
            static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(ENGINE_STARTUP_TIME).count());
        int seconds = e.start_timeout > 0 ? e.start_timeout : usual;
        ImGui::SetNextItemWidth(120.0f * ImGui::GetFontSize() / 16.0f);
        if (ui::inputInt("##start_timeout", &seconds)) {
            seconds         = std::clamp(seconds, 1, 3600);
            e.start_timeout = seconds == usual ? 0 : seconds;
            changed         = true;
        }
        ui::help(trf("How long the engine may take to answer uciok when started and isready at the start of each "
                     "game: more for an engine that loads a large network or hash table. Usually {} s.",
                     usual)
                     .c_str());
    }

    ui::label(TR("Groups"), label);
    {
        std::string text;
        for (const auto& g : e.groups) text += (text.empty() ? "" : ", ") + g;
        if (editText("##groups", text)) {
            e.groups.clear();
            std::size_t pos = 0;
            while (pos <= text.size()) {
                const auto comma = text.find(',', pos);
                auto part        = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                part.erase(0, part.find_first_not_of(' '));
                part.erase(part.find_last_not_of(' ') + 1);
                if (!part.empty()) e.groups.push_back(part);
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            changed = true;
        }
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label);
    ImGui::TextDisabled("%s", TR("Separated by commas, e.g. a pool or a rating list."));

    ui::label(TR("Reference Elo"), label);
    ImGui::SetNextItemWidth(120.0f * ImGui::GetFontSize() / 16.0f);
    if (ui::inputInt("##elo", &e.elo)) {
        e.elo   = std::clamp(e.elo, 0, 5000);
        changed = true;
    }
    ui::help(TR("Its rating on a list you trust, to anchor a tournament's ratings. 0 = none."));
    ImGui::SameLine(0, ImGui::GetFontSize() * 1.5f);
    if (ImGui::SmallButton(TR("Its results…"))) showEngineCard(e.name);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("Its card in Ratings: results, health and rating over every tournament."));

    ui::label(TR("Notes"), label);
    changed |= editLines("##notes", e.notes, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.5f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label);
    ImGui::TextDisabled("%s", TR("Anything to remember about it; the engine searches find it."));

    // Health check.
    ImGui::Spacing();
    ImGui::BeginDisabled(check_.valid() || pending_start_ || in_use);
    if (ImGui::Button(check_.valid() && check_index_ == selected_engine_ ? TR("Checking…") : TR("Check"))) {
        startCheck(selected_engine_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (e.check_ok >= 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, e.check_ok ? ui::GOOD : ui::WARN);
        ImGui::TextWrapped("%s %s", e.check_ok ? "✓" : "!", e.check_message.c_str());
        ImGui::PopStyleColor();
        if (!e.check_when.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", e.check_when.c_str());
        }
    } else {
        ImGui::TextDisabled("%s", TR("Starts the engine, measures how fast it answers, checks one move, then runs "
                                     "fastchess' UCI compliance steps."));
    }
    if (!e.check_report.empty() && ImGui::TreeNode(TR("UCI compliance report"))) {
        const float lines =
            std::min(14.0f, static_cast<float>(std::count(e.check_report.begin(), e.check_report.end(), '\n')));
        ImGui::BeginChild("##compliance", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * (lines + 0.5f)),
                          ImGuiChildFlags_Borders);
        std::size_t begin = 0;
        while (begin < e.check_report.size()) {
            auto end = e.check_report.find('\n', begin);
            if (end == std::string::npos) end = e.check_report.size();
            const std::string_view line(e.check_report.data() + begin, end - begin);
            const bool failed = line.rfind("✗", 0) == 0;
            if (failed) ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
            ImGui::TextUnformatted(line.data(), line.data() + line.size());
            if (failed) ImGui::PopStyleColor();
            begin = end + 1;
        }
        ImGui::EndChild();
        ImGui::TreePop();
    }

    // Quick test: a short match, through the tournament code.
    const bool test_waits = drawStartWaiting("quick_test", "test:" + e.name, false);
    ImGui::BeginDisabled(run_.running());
    if (!test_waits && ImGui::Button(quick_test_engine_ == e.name ? TR("Testing…") : TR("Quick test")))
        startQuickTest(selected_engine_);
    ImGui::EndDisabled();
    if (!test_waits && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", run_.running() ? TR("After the running match.")
                                               : TR("10 games at 10+0.1 from 5 positions, to see before a long "
                                                    "tournament whether it loses on time, crashes, plays an illegal "
                                                    "move or uses its clock badly."));
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("against"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 16.0f);
    if (ImGui::BeginCombo("##test_opponent", quick_test_opponent_.empty() ? TR("itself") : quick_test_opponent_.c_str(),
                          ImGuiComboFlags_HeightLargest)) {
        std::vector<std::string> names{TR("itself")}, more{""};  // first: the engine itself
        for (const auto& o : engines_) {
            if (o.name == e.name || (o.inactive && o.name != quick_test_opponent_)) continue;
            names.push_back(o.name);
            more.push_back(engineSearchText(o));
        }
        if (const int i =
                ui::searchComboItems(names, quick_test_opponent_.empty() ? names[0] : quick_test_opponent_, &more);
            i >= 0) {
            quick_test_opponent_ = i == 0 ? std::string() : names[i];
        }
        ImGui::EndCombo();
    }
    if (e.test_ok >= 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, e.test_ok ? ui::GOOD : ui::WARN);
        ImGui::TextWrapped("%s %s", e.test_ok ? "✓" : "!", e.test_message.c_str());
        ImGui::PopStyleColor();
        if (!e.test_when.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", e.test_when.c_str());
        }
    }

    ImGui::EndDisabled();
    if (in_use) ImGui::TextDisabled("%s", TR("Engine used by the running match: changes are locked."));

    ImGui::Separator();
    if (!e.id_name.empty()) {
        ImGui::Text("%s", e.id_name.c_str());
        if (!e.id_author.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled(TR("by %s"), e.id_author.c_str());
        }
    }

    // Waiting in the queue, it can be read next; being read, asking again would only read it again.
    const bool reading = probe_.valid() && probe_index_ == selected_engine_;
    const bool queued  = !reading && probing(e.name);
    ImGui::BeginDisabled(reading);
    if (ImGui::Button(reading  ? TR("Reading…")
                      : queued ? TR("Waiting to be read · read it next")
                               : TR("Read the UCI options again"))) {
        startProbe(selected_engine_);
    }
    ImGui::EndDisabled();
    if (!probe_message_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", probe_message_.c_str());
    }

    if (!e.uci_options.empty()) {
        ImGui::SameLine();
        char filter[256];
        const auto n = std::min(option_filter_.size(), sizeof(filter) - 1);
        std::copy_n(option_filter_.data(), n, filter);
        filter[n] = 0;
        ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 16.0f);
        if (ImGui::InputTextWithHint("##option_filter", TR("filter"), filter, sizeof(filter))) option_filter_ = filter;
        ImGui::SameLine();
        if (ImGui::Button(TR("All to default"))) {
            e.options.clear();
            changed = true;
        }
    }

    // Options: Threads and Hash first, then alphabetical.
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(e.uci_options.size()); ++i) {
        if (containsCaseInsensitive(e.uci_options[i].name, option_filter_)) order.push_back(i);
    }
    auto rank = [&](int i) {
        const auto& n = e.uci_options[i].name;
        return n == "Threads" ? 0 : n == "Hash" ? 1 : 2;
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return lessCaseInsensitive(e.uci_options[a].name, e.uci_options[b].name);
    });

    ImGui::BeginDisabled(in_use);
    // The options scroll on their own in the height left; in a low window, with the page, not under a second scroll
    // bar beside the page's.
    const bool own_scroll       = ImGui::GetContentRegionAvail().y >= ImGui::GetFrameHeightWithSpacing() * 7;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingStretchProp | (own_scroll ? ImGuiTableFlags_ScrollY : 0);
    if (!e.uci_options.empty() && ImGui::BeginTable("##options", 4, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(TR("Option"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn(TR("Value"), ImGuiTableColumnFlags_WidthStretch, 0.40f);
        ImGui::TableSetupColumn(TR("Default"), ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableHeadersRow();

        for (int i : order) {
            const auto& opt     = e.uci_options[i];
            const auto* current = e.option(opt.name);
            const bool modified = current != nullptr;
            auto value          = modified ? *current : opt.default_value;

            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (modified) ImGui::PushStyleColor(ImGuiCol_Text, ui::ACCENT);
            ImGui::TextUnformatted(opt.name.c_str());
            if (modified) ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            bool edited = false;

            if (opt.type == "spin") {
                // Engines do not always describe them well: a bound missing or not a number is no bound.
                const auto number = [](const std::string& text, long long fallback) {
                    try {
                        return std::stoll(text);
                    } catch (...) {
                        return fallback;
                    }
                };
                int64_t v  = number(value, 0);
                int64_t lo = number(opt.min, LLONG_MIN);
                int64_t hi = number(opt.max, LLONG_MAX);
                if (lo > hi) std::swap(lo, hi);
                if (ui::inputInt64("##v", &v)) {
                    v      = std::clamp(v, lo, hi);
                    value  = std::to_string(v);
                    edited = true;
                }
                if (ImGui::IsItemHovered() && (!opt.min.empty() || !opt.max.empty())) {
                    ImGui::SetTooltip("%s … %s", opt.min.c_str(), opt.max.c_str());
                }
            } else if (opt.type == "check") {
                bool v = value == "true";
                if (ImGui::Checkbox("##v", &v)) {
                    value  = v ? "true" : "false";
                    edited = true;
                }
            } else if (opt.type == "combo") {
                if (ImGui::BeginCombo("##v", value.c_str())) {
                    for (const auto& var : opt.vars) {
                        if (ImGui::Selectable(var.c_str(), var == value)) {
                            value  = var;
                            edited = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if (opt.type == "button") {
                bool v = value == "true";
                if (ImGui::Checkbox(TR("send at startup##v"), &v)) {
                    value  = v ? "true" : "false";
                    edited = true;
                }
            } else if (const auto path = optionPath(opt); path != OptionPath::None) {
                // A file or a folder: typed, or chosen.
                edited =
                    editText("##v", value, ImGui::GetContentRegionAvail().x - browse - ImGui::GetStyle().ItemSpacing.x);
                ImGui::SameLine();
                if (ImGui::Button("…##browse")) {
                    option_browse_ = opt.name;
                    if (path == OptionPath::File)
                        dialogs_.openFile(Dialogs::Purpose::EngineOption, {}, value);
                    else
                        dialogs_.openFolder(Dialogs::Purpose::EngineOption, value);
                }
            } else {
                edited = editText("##v", value);
            }

            if (edited) {
                if (opt.type == "button" ? value != "true" : value == opt.default_value) {
                    e.clearOption(opt.name);
                } else {
                    e.setOption(opt.name, value);
                }
                changed = true;
            }

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", opt.type == "button" ? TR("button") : opt.default_value.c_str());

            ImGui::TableNextColumn();
            if (modified && ImGui::SmallButton("x")) {
                e.clearOption(opt.name);
                changed = true;
            }
            if (modified && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Back to the default value"));

            ImGui::PopID();
        }
        ImGui::EndTable();
    } else if (e.uci_options.empty()) {
        ImGui::TextDisabled("%s", TR("No options yet: click Read the UCI options again."));
    }
    ImGui::EndDisabled();

    // Options set on an engine that no longer reports them would make fastchess warn at every game.
    if (!e.options.empty() && !e.uci_options.empty()) {
        for (const auto& [k, v] : e.options) {
            bool known = false;
            for (const auto& o : e.uci_options) known |= o.name == k;
            if (!known)
                ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.3f, 1), TR("Option unknown to the engine: %s = %s"),
                                   k.c_str(), v.c_str());
        }
    }

    if (changed) {
        refreshProgram(e);
        engines_save_due_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }

    ImGui::EndChild();
    ui::endPanel();
}

void App::drawConsole() {
    if (!ui::beginPanel(TR("Console###console"))) {
        ui::endPanel();
        return;
    }

    Console::instance().copy(console_lines_, console_version_);

    if (ImGui::SmallButton(TR("Clear"))) Console::instance().clear();
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Copy"))) {
        std::string all;
        for (const auto& l : console_lines_) all += l.text + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    ImGui::Checkbox(TR("Follow"), &console_follow_);

    ImGui::BeginChild("##console", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(console_lines_.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& l = console_lines_[i];
            if (l.warning) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.3f, 1));
            ImGui::TextUnformatted(l.text.c_str());
            if (l.warning) ImGui::PopStyleColor();
        }
    }
    if (console_follow_) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ui::endPanel();
}

bool App::engineInUse(const std::string& name) const {
    if (!run_.running()) return false;
    if (const auto* t = run_.tournament()) {
        for (const auto& p : t->spec().participants) {
            if (p.engine.name == name) return true;
            // With a book of its own, it may play as "<engine> (<book>)".
            if (!p.book.empty() && p.engine.name.rfind(name + " (", 0) == 0) return true;
        }
    }
    return false;
}

void App::startCheck(int index) {
    if (check_.valid() || pending_start_ || index < 0 || index >= static_cast<int>(engines_.size())) return;
    check_index_       = index;
    const auto entry   = engines_[index];
    check_interrupted_ = std::make_shared<std::atomic_bool>(false);
    // Counted like a read: a run must not replace fastchess' configuration while the check reads its timeouts.
    g_probes_in_flight++;
    check_ = std::async(std::launch::async, [entry, interrupted = check_interrupted_] {
        const InFlight in_flight;
        return checkEngine(entry, interrupted.get());
    });
}

void App::startQuickTest(int index) {
    if (index < 0 || index >= static_cast<int>(engines_.size()) || run_.running()) return;
    const auto name                = engines_[index].name;
    std::vector<std::string> needs = {name};
    if (!quick_test_opponent_.empty()) needs.push_back(quick_test_opponent_);
    if (startAfterReads("test:" + name, std::move(needs), quickTestLater(name))) return;
    // Only the last quick test is kept: its result stays with the engine.
    const auto dir = storage_.root() / "tests";
    std::error_code ec;
    std::vector<fs::path> old;
    for (fs::directory_iterator it(dir, ec), last; !ec && it != last; it.increment(ec)) {
        if (it->is_directory(ec)) old.push_back(it->path());
    }
    for (const auto& path : old) fs::remove_all(path, ec);

    auto engines     = engines_;
    const auto setup = quickTestSetup(engines_[index], quick_test_opponent_, dir / "quick-test.epd", engines);
    start_error_     = run_.start(setup, engines, topology_, dir);
    if (!start_error_.empty()) {
        Console::instance().add(trf("Cannot start the quick test: {}", start_error_), true);
        return;
    }
    resetRunView();
    quick_test_engine_ = engines_[index].name;
    tab_request_       = Tab::Live;
    Console::instance().add(trf("Quick test of {} started.", quick_test_engine_));
}

std::function<void()> App::quickTestLater(const std::string& name) {
    return [this, name] {
        const auto it = std::find_if(engines_.begin(), engines_.end(), [&](const auto& e) { return e.name == name; });
        if (it != engines_.end()) startQuickTest(static_cast<int>(it - engines_.begin()));
    };
}

void App::finishQuickTest() {
    const auto name = std::exchange(quick_test_engine_, {});
    const auto* t   = run_.tournament();
    const auto it   = std::find_if(engines_.begin(), engines_.end(), [&](const auto& e) { return e.name == name; });
    if (!t || it == engines_.end()) return;

    QuickTestReport report;
    if (!run_.error().empty()) {
        report.message = trf("Error: {}", run_.error());
    } else if (run_.stoppedByUser()) {
        report.message = TR("Stopped before the end.");
    } else {
        const auto totals = totals_.empty() ? EngineTotals{} : totals_[0];  // [0]: the engine tested, A
        report            = quickTestReport(*t, t->spec().participants.at(0).id, totals.time_ms, totals.moves);
    }
    it->test_ok      = report.ok ? 1 : 0;
    it->test_message = report.message;
    it->test_when    = nowText();
    storage_.saveEngines(engines_);

    finish_message_ = trf("{}: {}", name, report.message);
    Console::instance().add(finish_message_, !report.ok);
}

void App::pollCheck() {
    // The next queued check when idle: not while a run plays, whose games it would slow down, nor waits to start.
    if (!check_.valid() && !check_queue_.empty() && !run_.running() && !pending_start_) {
        const auto name = check_queue_.front();
        check_queue_.erase(check_queue_.begin());
        const auto it = std::find_if(engines_.begin(), engines_.end(), [&](const auto& e) { return e.name == name; });
        if (it != engines_.end()) startCheck(static_cast<int>(it - engines_.begin()));
    }

    if (!check_.valid() || check_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    CheckResult result;
    try {
        result = check_.get();
    } catch (const std::exception& e) {
        result.message = e.what();
    }
    const int index = check_index_;
    check_index_    = -1;
    if (index < 0 || index >= static_cast<int>(engines_.size())) return;
    // Interrupted for a start: checked again, first, once checks go on.
    if (check_interrupted_ && *check_interrupted_) {
        std::erase(check_queue_, engines_[index].name);
        check_queue_.insert(check_queue_.begin(), engines_[index].name);
        return;
    }

    auto& e         = engines_[index];
    e.check_ok      = result.ok ? 1 : 0;
    e.check_message = result.message;
    e.check_report  = result.report;
    e.check_when    = nowText();
    storage_.saveEngines(engines_);
    Console::instance().add(fmt::format("{}: {}", e.name, result.message), !result.ok);

    // No options yet: their reading failed once (an earlier version refused some engines). The engine has just been
    // started, so they are read again.
    if (e.uci_options.empty()) startProbe(index);
}

void App::importEngines(const ImportResult& imported, const std::filesystem::path& refreshed) {
    // Shown first, to choose: a folder scan takes every program, the GUI it holds too (Arena.exe).
    ImportPreview preview;
    preview.result.notes = imported.notes;
    preview.refreshed    = refreshed.string();
    for (const auto& e : imported.engines) {
        // Already in the list: not added again, nor even shown when the remembered folder is looked in again.
        const bool known =
            std::any_of(engines_.begin(), engines_.end(), [&](const EngineEntry& o) { return sameEngine(o, e); });
        if (known && !refreshed.empty()) continue;
        preview.result.engines.push_back(e);
        preview.known.push_back(known);
        preview.chosen.push_back(!known);
    }
    import_preview_      = std::move(preview);
    open_import_preview_ = true;
}

const std::filesystem::path& App::engineFolder() {
    if (!engine_folder_) engine_folder_ = storage_.engineFolder();
    return *engine_folder_;
}

void App::refreshEngineFolder() {
    const auto folder = engineFolder();
    if (folder.empty()) return;
    ImportResult found;
    std::error_code ec;
    if (fs::is_directory(folder, ec))
        found = findEngines(folder);
    else
        found.notes.push_back(trf("The folder {} is not there any more.", folder.string()));
    importEngines(found, folder);
}

void App::setInactive(const std::vector<std::string>& names, bool inactive) {
    for (auto& e : engines_) {
        if (std::find(names.begin(), names.end(), e.name) != names.end()) e.inactive = inactive;
    }
    storage_.saveEngines(engines_);
}

void App::addImported() {
    if (!import_preview_) return;
    const auto& imported = import_preview_->result;
    int added            = 0;
    for (std::size_t i = 0; i < imported.engines.size(); ++i) {
        if (!import_preview_->chosen[i] || import_preview_->known[i]) continue;
        auto e = imported.engines[i];
        e.name = uniqueEngineName(e.name.empty() ? fs::path(e.cmd).stem().string() : e.name, -1);
        engines_.push_back(e);
        probe_queue_.push_back(e.name);
        ++added;
    }
    for (const auto& note : imported.notes) Console::instance().add(note, true);
    Console::instance().add(trn("{} engine imported; its options are being read.",
                                "{} engines imported; their options are being read.", added, added));
    if (added > 0) selected_engine_ = static_cast<int>(engines_.size()) - added;
    storage_.saveEngines(engines_);
    import_preview_.reset();
}

void App::drawImportPreview() {
    const char* title = TR("Import engines###import_preview");
    if (open_import_preview_) {
        ImGui::OpenPopup(title);
        open_import_preview_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(px(760), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title, nullptr)) return;
    if (!import_preview_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    auto& p           = *import_preview_;
    const auto& found = p.result.engines;

    if (!p.refreshed.empty()) {
        ImGui::TextDisabled("%s", p.refreshed.c_str());
        if (found.empty()) ImGui::TextUnformatted(TR("No new engine in the folder."));
    }
    if (!found.empty()) {
        const auto text = p.refreshed.empty()
                              ? trn("{} engine found. Untick those to leave out.",
                                    "{} engines found. Untick those to leave out.", found.size(), found.size())
                              : trn("{} new engine found. Untick those to leave out.",
                                    "{} new engines found. Untick those to leave out.", found.size(), found.size());
        ImGui::TextUnformatted(text.c_str());
    }
    if (!p.result.notes.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        for (const auto& note : p.result.notes) ImGui::TextWrapped("%s", note.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::SmallButton(TR("All"))) std::fill(p.chosen.begin(), p.chosen.end(), true);
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("None"))) std::fill(p.chosen.begin(), p.chosen.end(), false);

    const float rows = std::min(16.0f, static_cast<float>(found.size()) + 1);
    if (ImGui::BeginTable("##import_list", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0, ImGui::GetFrameHeightWithSpacing() * rows))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupColumn(TR("Name"), ImGuiTableColumnFlags_WidthStretch, 0.3f);
        ImGui::TableSetupColumn(TR("Program"), ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(found.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& e = found[i];
                ImGui::TableNextRow();
                ImGui::PushID(i);
                ImGui::TableNextColumn();
                bool chosen = p.chosen[i] && !p.known[i];
                ImGui::BeginDisabled(p.known[i]);
                if (ImGui::Checkbox("##chosen", &chosen)) p.chosen[i] = chosen;
                ImGui::EndDisabled();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(e.name.empty() ? fs::path(e.cmd).stem().string().c_str() : e.name.c_str());
                ImGui::TableNextColumn();
                const auto program = e.args.empty() ? e.cmd : e.cmd + " " + e.args;
                if (p.known[i])
                    ImGui::TextDisabled("%s  %s", program.c_str(), TR("(already in the list)"));
                else
                    ImGui::TextUnformatted(program.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    int count = 0;
    for (std::size_t i = 0; i < found.size(); ++i) count += p.chosen[i] && !p.known[i];
    ImGui::BeginDisabled(count == 0);
    if (ImGui::Button(trn("Add {} engine", "Add {} engines", count, count).c_str())) {
        addImported();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"))) {
        import_preview_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::openRatingImport(const std::string& text) {
    rating_import_.emplace();
    rating_import_->text = text;
    readRatingImport();
    open_rating_import_ = true;
}

void App::loadRatingList(const std::string& path) {
    std::ifstream in(fs::path(path), std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (!in && !in.eof()) {
        Console::instance().add(trf("Cannot read {}", path), true);
        return;
    }
    openRatingImport(toUtf8(std::move(text)));
}

void App::readRatingImport() {
    if (!rating_import_) return;
    auto& r         = *rating_import_;
    const auto list = tour::parseRatingList(r.text);
    auto names      = std::vector<std::string>{};
    for (const auto& e : engines_) names.push_back(e.name);
    const auto found = tour::matchRatings(list, names);

    r.listed    = list.size();
    r.unmatched = found.unmatched;
    r.rows.clear();
    for (const auto& m : found.matched) {
        const auto it =
            std::find_if(engines_.begin(), engines_.end(), [&](const auto& e) { return e.name == m.engine; });
        if (it == engines_.end()) continue;
        r.rows.push_back({m.engine, m.listed, it->elo, m.elo, it->elo != m.elo});
    }
    // In alphabetical order, as the engine list shows them.
    std::stable_sort(r.rows.begin(), r.rows.end(),
                     [](const auto& a, const auto& b) { return lessCaseInsensitive(a.engine, b.engine); });
}

void App::applyRatingImport() {
    if (!rating_import_) return;
    int applied = 0;
    for (const auto& row : rating_import_->rows) {
        if (!row.chosen || row.elo == row.elo_now) continue;
        for (auto& e : engines_) {
            if (e.name != row.engine) continue;
            e.elo = row.elo;
            ++applied;
        }
    }
    storage_.saveEngines(engines_);
    ratings_dirty_ = true;  // a reference rating anchors the rating list
    Console::instance().add(trn("{} reference Elo set from the rating list.",
                                "{} reference Elo set from the rating list.", applied, applied));
    rating_import_.reset();
}

void App::drawRatingImport() {
    const char* title = TR("Import a rating list###rating_import");
    if (open_rating_import_) {
        ImGui::OpenPopup(title);
        open_rating_import_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(px(820), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title, nullptr)) return;
    if (!rating_import_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    auto& r = *rating_import_;

    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", TR("Paste a rating list, or open its file: a rating list's text version or its web page "
                                 "(copied from the browser or saved), Ordo's output, or CSV (name,elo). The library "
                                 "engines it names get their reference Elo from it: the same name, letter case and "
                                 "spaces aside."));
    ImGui::PopTextWrapPos();
    if (ImGui::Button(TR("Open a file…"))) {
        dialogs_.openFile(Dialogs::Purpose::ImportRatingList,
                          {{TR("Rating lists"), "txt;csv;htm;html"}, {TR("All files"), "*"}});
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Paste"))) {
        if (const char* clip = ImGui::GetClipboardText()) {
            r.text = clip;
            readRatingImport();
        }
    }
    if (editLines("##rating_text", r.text, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 6))) readRatingImport();

    // What it changes.
    int changes = 0;
    for (const auto& row : r.rows) changes += row.chosen && row.elo != row.elo_now;
    if (!r.text.empty()) {
        ImGui::TextUnformatted(trf("{} engines on the list · {} in the library · {} not in the library", r.listed,
                                   r.rows.size(), r.unmatched.size())
                                   .c_str());
    }
    if (!r.unmatched.empty() && ImGui::TreeNode(TR("Names of the list that are no library engine"))) {
        ImGui::BeginChild("##unmatched", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() *
                                                       std::min(8.0f, static_cast<float>(r.unmatched.size()) + 0.5f)));
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(r.unmatched.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                ImGui::TextDisabled("%s", r.unmatched[i].c_str());
        }
        ImGui::EndChild();
        ImGui::TreePop();
    }

    if (!r.rows.empty()) {
        const float rows = std::min(12.0f, static_cast<float>(r.rows.size()) + 1);
        if (ImGui::BeginTable("##rating_rows", 4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                              ImVec2(0, ImGui::GetFrameHeightWithSpacing() * rows))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableSetupColumn(TR("Engine"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
            ImGui::TableSetupColumn(TR("On the list"), ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn(TR("Reference Elo"), ImGuiTableColumnFlags_WidthStretch, 0.25f);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(r.rows.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    auto& row       = r.rows[i];
                    const bool same = row.elo == row.elo_now;
                    ImGui::TableNextRow();
                    ImGui::PushID(i);
                    ImGui::TableNextColumn();
                    bool chosen = row.chosen && !same;
                    ImGui::BeginDisabled(same);
                    if (ImGui::Checkbox("##chosen", &chosen)) row.chosen = chosen;
                    ImGui::EndDisabled();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(row.engine.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", row.listed.c_str());
                    ImGui::TableNextColumn();
                    if (same) {
                        ImGui::TextDisabled("%d  %s", row.elo, TR("(the same)"));
                    } else if (row.elo_now == 0) {
                        ImGui::Text("— → %d", row.elo);
                    } else {
                        ImGui::Text("%d → %d", row.elo_now, row.elo);
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%+d)", row.elo - row.elo_now);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    } else if (!r.text.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::TextWrapped("%s", r.listed == 0 ? TR("No engine and rating found in this text.")
                                               : TR("None of these engines is in the library under the same name."));
        ImGui::PopStyleColor();
    }

    ImGui::BeginDisabled(changes == 0);
    if (ImGui::Button(trn("Set {} reference Elo", "Set {} reference Elo", changes, changes).c_str())) {
        applyRatingImport();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"))) {
        rating_import_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace fcg
