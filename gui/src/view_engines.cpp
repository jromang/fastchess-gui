// "Engines": the engine list and the UCI option editor; "Console": fastchess output.

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

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
    follow(quick_test_engine_);
    follow(quick_test_opponent_);
    follow(relaunch_engine_);
    follow(edit_engine_);
    loadSuiteSetup();  // before it is changed and written
    bool suite = false;
    for (auto& n : suite_setup_.engines) suite |= follow(n);
    if (suite) writeJson(storage_.root() / "suite.json", suite_setup_);
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
    // Asked for by name (a button, a new protocol, a check): first in the queue, not dropped.
    if (probe_.valid() || start_after_reads_) {
        std::erase(probe_queue_, name);
        probe_queue_.insert(probe_queue_.begin(), name);
        return;
    }

    std::erase(probe_queue_, name);
    probe_index_   = index;
    probe_entry_   = engines_[index];
    probe_again_   = false;
    probe_message_ = TR("Reading the engine…");
    g_probes_in_flight++;

    probe_ = std::async(std::launch::async, [entry = engines_[index]] {
        const InFlight in_flight;
        return probeEngine(entry);
    });
}

bool App::probing(const std::string& name) const {
    if (probe_.valid() && probe_index_ >= 0 && probe_index_ < static_cast<int>(engines_.size()) &&
        engines_[probe_index_].name == name) {
        return true;
    }
    return std::find(probe_queue_.begin(), probe_queue_.end(), name) != probe_queue_.end();
}

bool App::startAfterReads(std::function<void()> start) {
    if (g_probes_in_flight == 0 && !probe_.valid() && !check_.valid()) return false;
    if (!start_after_reads_) Console::instance().add(TR("Starts once the engine being read or checked is done…"));
    start_after_reads_ = std::move(start);
    start_error_.clear();
    return true;
}

void App::pollProbe() {
    pollFingerprints();

    // Start the next queued probe when idle, but never while a run is being set up, or waits for the reads to end.
    // The engines of the setup first: its Start waits for them.
    if (!probe_.valid() && !probe_queue_.empty() && !run_.running() && !start_after_reads_) {
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

    // A start put off until the reads and checks in flight end.
    if (start_after_reads_ && g_probes_in_flight == 0 && !probe_.valid() && !check_.valid()) {
        const auto start = std::exchange(start_after_reads_, {});
        start();
    }
}

void App::applyProbe() {
    ProbeResult result;
    try {
        result = probe_.get();
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    const int index  = probe_index_;
    const bool again = std::exchange(probe_again_, false);
    probe_index_     = -1;

    if (index < 0 || index >= static_cast<int>(engines_.size())) return;
    auto& e = engines_[index];

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
        if (ImGui::MenuItem(TR("An engine list…"))) {
            dialogs_.openFile(Dialogs::Purpose::ImportEngineList,
                              {{TR("Engine lists"), "txt;json;cfg;ini"}, {TR("All files"), "*"}});
        }
        ImGui::TextDisabled("%s", TR("ChessGUI (ChsGUI_Eng.TXT), cutechess or Banksia (engines.json), Arena…"));
        if (ImGui::MenuItem(TR("Every engine of a folder…"))) dialogs_.openFolder(Dialogs::Purpose::ImportEngineFolder);
        ImGui::EndPopup();
    }
    const auto duplicate = [&] {
        auto copy = engines_[selected_engine_];
        copy.name = uniqueEngineName(copy.name, -1);
        engines_.push_back(copy);
        selected_engine_ = static_cast<int>(engines_.size()) - 1;
        storage_.saveEngines(engines_);
    };
    ui::sameLineOrWrap(TR("Duplicate"));
    ImGui::BeginDisabled(selected_engine_ < 0);
    if (ImGui::Button(TR("Duplicate")) && selected_engine_ >= 0) duplicate();
    ui::sameLineOrWrap(TR("Remove"));
    if (ImGui::Button(TR("Remove")) && selected_engine_ >= 0) ImGui::OpenPopup(TR("Remove the engine?###remove"));
    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal(TR("Remove the engine?###remove"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (selected_engine_ >= 0) {
            ImGui::Text(TR("Remove “%s” from the list? The file is not deleted."),
                        engines_[selected_engine_].name.c_str());
        }
        if (ImGui::Button(TR("Remove##confirm")) && selected_engine_ >= 0) {
            // A read or a check still running belongs to an index: keep it on its engine, or drop it with it.
            for (int* busy : {&probe_index_, &check_index_}) {
                if (*busy == selected_engine_)
                    *busy = -1;
                else if (*busy > selected_engine_)
                    --*busy;
            }
            engines_.erase(engines_.begin() + selected_engine_);
            selected_engine_ = std::min(selected_engine_, static_cast<int>(engines_.size()) - 1);
            storage_.saveEngines(engines_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Search and groups.
    {
        char buffer[128];
        const auto n = std::min(engine_search_.size(), sizeof(buffer) - 1);
        std::copy_n(engine_search_.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##engine_search", TR("Search"), buffer, sizeof(buffer))) engine_search_ = buffer;

        std::vector<std::string> groups;
        for (const auto& e : engines_) {
            for (const auto& g : e.groups) {
                if (std::find(groups.begin(), groups.end(), g) == groups.end()) groups.push_back(g);
            }
        }
        std::sort(groups.begin(), groups.end());
        if (!groups.empty()) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##engine_group", engine_group_.empty() ? TR("All groups") : engine_group_.c_str())) {
                if (ImGui::Selectable(TR("All groups"), engine_group_.empty())) engine_group_.clear();
                for (const auto& g : groups) {
                    if (ImGui::Selectable(g.c_str(), g == engine_group_)) engine_group_ = g;
                }
                ImGui::EndCombo();
            }
        }
    }

    ImGui::Separator();
    // In alphabetical order; engines_ keeps the order they were added in, which the indexes follow.
    std::vector<int> shown(engines_.size());
    for (int i = 0; i < static_cast<int>(shown.size()); ++i) shown[i] = i;
    std::stable_sort(shown.begin(), shown.end(),
                     [&](int a, int b) { return lessCaseInsensitive(engines_[a].name, engines_[b].name); });
    bool remove_asked = false, duplicate_asked = false;

    // Only the engines scroll: the buttons above and the legend below stay in sight.
    const char* legend   = TR("✓ works · ! problem · ↔ WinBoard · right click: more");
    const float legend_h = engines_.empty()
                               ? 0.0f
                               : ImGui::CalcTextSize(legend, nullptr, false, ImGui::GetContentRegionAvail().x).y +
                                     ImGui::GetStyle().ItemSpacing.y * 2;
    ImGui::BeginChild("##engine_entries", ImVec2(0, -legend_h));
    for (const int i : shown) {
        const auto& e = engines_[i];
        if (!engine_group_.empty() && std::find(e.groups.begin(), e.groups.end(), engine_group_) == e.groups.end()) {
            continue;
        }
        if (!engine_search_.empty() && !containsCaseInsensitive(e.name, engine_search_) &&
            !containsCaseInsensitive(e.id_author, engine_search_)) {
            continue;
        }
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
        const bool missing = e.program_state == ProgramState::Missing || e.program_state == ProgramState::MissingDir;
        ImGui::PushStyleColor(ImGuiCol_Text, missing            ? ui::BAD
                                             : !problem.empty() ? ui::WARN
                                             : e.check_ok == 1  ? ui::GOOD
                                             : e.check_ok == 0  ? ui::WARN
                                                                : ui::MUTED);
        ImGui::TextUnformatted(mark);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        const auto label = fmt::format("{}{}##engine{}", e.name, tag, i);
        if (ImGui::Selectable(label.c_str(), selected_engine_ == i)) selected_engine_ = i;
        if (ImGui::IsItemHovered()) {
            // What the mark means, then what was found.
            const std::string meaning = !problem.empty()  ? problem
                                        : e.check_ok == 1 ? std::string(TR("Checked: it works."))
                                        : e.check_ok == 0 ? std::string(TR("The last check or reading failed."))
                                        : isWinboard(e)   ? std::string(TR("A WinBoard engine."))
                                                          : std::string(TR("Not checked yet."));
            const bool more           = problem.empty() && !e.check_message.empty();
            ImGui::SetTooltip("%s %s%s%s", mark, meaning.c_str(), more ? "\n" : "",
                              more ? e.check_message.c_str() : "");
        }
        if (ImGui::BeginPopupContextItem()) {
            selected_engine_ = i;
            if (ImGui::MenuItem(TR("Check"), nullptr, false, !check_.valid() && !start_after_reads_)) startCheck(i);
            const bool being_read = probe_.valid() && probe_index_ == i;
            if (ImGui::MenuItem(probing(e.name) && !being_read ? TR("Read it next") : TR("Read the UCI options again"),
                                nullptr, false, !being_read))
                startProbe(i);
            if (ImGui::MenuItem(TR("Duplicate"))) duplicate_asked = true;
            if (ImGui::MenuItem(TR("Remove…"))) remove_asked = true;
            ImGui::EndPopup();
        }
    }
    if (engines_.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", TR("No engine yet. Click Add."));
        ImGui::PopTextWrapPos();
    }
    if (selected_engine_ >= 0 && ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        remove_asked = true;
    }
    ImGui::EndChild();
    if (!engines_.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", legend);
        ImGui::PopTextWrapPos();
    }
    // Here, in the list's window: the confirmation is drawn there, and a popup is found by the window it is opened in.
    if (remove_asked) ImGui::OpenPopup(TR("Remove the engine?###remove"));
    if (duplicate_asked) duplicate();  // after the loop, which holds references into engines_

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
        auto name = e.name;
        if (editText("##name", name)) {
            renameEngine(selected_engine_, name);
            changed = true;
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
    if (ImGui::InputInt("##elo", &e.elo, 0, 0)) {
        e.elo   = std::clamp(e.elo, 0, 5000);
        changed = true;
    }
    ui::help(TR("Its rating on a list you trust, to anchor a tournament's ratings. 0 = none."));
    ImGui::SameLine(0, ImGui::GetFontSize() * 1.5f);
    if (ImGui::SmallButton(TR("Its results…"))) showEngineCard(e.name);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", TR("Its card in Ratings: results, health and rating over every tournament."));

    // Health check.
    ImGui::Spacing();
    ImGui::BeginDisabled(check_.valid() || start_after_reads_ || in_use);
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
    ImGui::BeginDisabled(run_.running());
    if (ImGui::Button(quick_test_engine_ == e.name ? TR("Testing…") : TR("Quick test")))
        startQuickTest(selected_engine_);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", run_.running() ? TR("After the running match.")
                                               : TR("10 games at 10+0.1 from 5 positions, to see before a long "
                                                    "tournament whether it loses on time, crashes, plays an illegal "
                                                    "move or uses its clock badly."));
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("against"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f * ImGui::GetFontSize() / 16.0f);
    if (ImGui::BeginCombo("##test_opponent",
                          quick_test_opponent_.empty() ? TR("itself") : quick_test_opponent_.c_str())) {
        if (ImGui::Selectable(TR("itself"), quick_test_opponent_.empty())) quick_test_opponent_.clear();
        for (const auto& o : engines_) {
            if (o.name != e.name && ImGui::Selectable(o.name.c_str(), o.name == quick_test_opponent_)) {
                quick_test_opponent_ = o.name;
            }
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
                long long v  = number(value, 0);
                long long lo = number(opt.min, LLONG_MIN);
                long long hi = number(opt.max, LLONG_MAX);
                if (lo > hi) std::swap(lo, hi);
                if (ImGui::InputScalar("##v", ImGuiDataType_S64, &v)) {
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
        storage_.saveEngines(engines_);
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
        }
    }
    return false;
}

void App::startCheck(int index) {
    if (check_.valid() || start_after_reads_ || index < 0 || index >= static_cast<int>(engines_.size())) return;
    check_index_     = index;
    const auto entry = engines_[index];
    // Counted like a read: a run must not replace fastchess' configuration while the check reads its timeouts.
    g_probes_in_flight++;
    check_ = std::async(std::launch::async, [entry] {
        const InFlight in_flight;
        return checkEngine(entry);
    });
}

void App::startQuickTest(int index) {
    if (index < 0 || index >= static_cast<int>(engines_.size()) || run_.running()) return;
    if (startAfterReads([this, name = engines_[index].name] {
            const auto it =
                std::find_if(engines_.begin(), engines_.end(), [&](const auto& e) { return e.name == name; });
            if (it != engines_.end()) startQuickTest(static_cast<int>(it - engines_.begin()));
        })) {
        return;
    }
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

void App::importEngines(const ImportResult& imported) {
    // Shown first, to choose: a folder scan takes every program, the GUI it holds too (Arena.exe).
    ImportPreview preview;
    preview.result = imported;
    for (const auto& e : imported.engines) {
        // Already in the list: not added again.
        const bool known =
            std::any_of(engines_.begin(), engines_.end(), [&](const EngineEntry& o) { return sameEngine(o, e); });
        preview.known.push_back(known);
        preview.chosen.push_back(!known);
    }
    import_preview_      = std::move(preview);
    open_import_preview_ = true;
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

    ImGui::TextUnformatted(trn("{} engine found. Untick those to leave out.",
                               "{} engines found. Untick those to leave out.", found.size(), found.size())
                               .c_str());
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

}  // namespace fcg
