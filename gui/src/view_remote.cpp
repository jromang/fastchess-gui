// The engines of the library that run on a machine, on the window's side: its engine hub (engine_hub.hpp), told which
// engines it may start and the passphrases typed, and the passphrase asked before such an engine starts.

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <map>

#include <imgui.h>
#include <mbedtls/platform_util.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "console.hpp"
#include "engine_hub.hpp"
#include "engines.hpp"
#include "guide.hpp"
#include "i18n.hpp"
#include "program_format.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

std::string machineName(const Machine& m) { return m.name.empty() ? m.host : m.name; }

// A Windows machine's path as SFTP gives it ("/C:/Engines/sf.exe") as cmd takes it, and the others as they are.
std::string fromSftp(const std::string& path, RemoteShell shell) {
    if (shell != RemoteShell::Cmd) return path;
    auto out = path;
    if (out.size() >= 3 && out[0] == '/' && std::isalpha(static_cast<unsigned char>(out[1])) && out[2] == ':')
        out.erase(0, 1);
    std::replace(out.begin(), out.end(), '/', '\\');
    return out;
}

// A path of the home folder as ~/..., and back.
std::string tilde(const std::string& path, const std::string& home) {
    if (home.empty() || home == "/") return path;
    if (path == home) return "~";
    if (path.rfind(home + "/", 0) == 0) return "~" + path.substr(home.size());
    return path;
}
std::string untilde(const std::string& path, const std::string& home) {
    if (home.empty() || path.empty() || path[0] != '~') return path;
    if (path == "~") return home;
    if (path.rfind("~/", 0) == 0) return home + path.substr(1);
    return path;
}

std::string dayOf(int64_t time) {
    if (time <= 0) return {};
    const auto t = static_cast<std::time_t>(time);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char text[16];
    std::strftime(text, sizeof text, "%Y-%m-%d", &tm);
    return text;
}

// A file that can be run there: executable for its owner, or a Windows program (a server that tells no permissions
// gives the benefit of the doubt).
bool runnable(const RemoteEntry& e, RemoteShell shell) {
    if (!e.regular) return false;
    if (shell == RemoteShell::Cmd) {
        auto name = e.name;
        for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return name.size() > 4 && (name.ends_with(".exe") || name.ends_with(".bat") || name.ends_with(".cmd"));
    }
    return e.perms == 0 || (e.perms & 0111) != 0;
}

}  // namespace

void App::syncEngineHub() {
    std::vector<tour::RemoteEngine> remote;
    std::string signature;
    for (const auto& e : engines_) {
        auto r = remoteEngine(e);
        if (!r) continue;
        signature += r->machine + '\n' + r->program + '\n' + e.args + '\n' + r->dir + '\n';
        remote.push_back(std::move(*r));
    }
    if (remote.empty() && !hub_) return;
    if (!hub_) {
        EngineHub::Options options;
        options.profile = storage_.root();
        auto hub        = std::make_unique<EngineHub>(options);
        std::string error;
        if (!hub->start(error)) {
            Console::instance().add(trf("The engines on machines cannot be started: {}", error), true);
            return;
        }
        hub->exportEnvironment();  // for the programs started from now on: fastchess' engines are relays to it
        for (const auto& [file, secret] : machine_passphrases_) hub->setPassphrase(file, secret);
        hub_ = std::move(hub);
        hub_signature_.clear();
    }
    if (signature != hub_signature_) {
        hub_->allow(remote);
        hub_signature_ = std::move(signature);
    }
}

bool App::remoteReady(const EngineEntry& e, std::function<void()> then) {
    if (e.machine.empty()) return true;
    syncEngineHub();
    const auto* machine = machineById(e.machine);
    if (!machine) return true;  // the hub says the machine is gone
    bool missing = false;
    (void)machinePassphrase(*machine, missing);
    if (!missing) return true;
    passphrase_machine_ = machine->id;
    passphrase_then_    = std::move(then);
    open_passphrase_    = true;
    return false;
}

void App::rememberPassphrase(const std::string& key_file, const std::string& passphrase) {
    if (passphrase.empty()) {
        if (auto it = machine_passphrases_.find(key_file); it != machine_passphrases_.end()) {
            if (!it->second.empty()) mbedtls_platform_zeroize(it->second.data(), it->second.size());
            machine_passphrases_.erase(it);
        }
    } else {
        machine_passphrases_[key_file] = passphrase;
    }
    if (hub_) hub_->setPassphrase(key_file, passphrase);
}

std::map<std::string, std::string> App::secretsFor(const tour::TournamentSpec& spec) {
    std::map<std::string, std::string> out;
    for (const auto& p : spec.participants) {
        if (!p.remote) continue;
        const auto* m = machineById(p.remote->machine);
        if (!m) continue;
        bool missing          = false;
        const auto passphrase = machinePassphrase(*m, missing);
        if (!passphrase.empty()) out[m->key_file] = passphrase;
    }
    return out;
}

bool App::remoteEnginesReady(const std::vector<std::string>& engines, std::function<void()> then) {
    for (const auto& name : engines) {
        const auto i = engineIndex(name);
        if (i < 0 || engines_[i].machine.empty()) continue;
        if (!remoteReady(engines_[i], then)) return false;
    }
    return true;
}

int App::engineIndex(const std::string& name) const {
    for (std::size_t i = 0; i < engines_.size(); ++i) {
        if (engines_[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

std::string App::remoteFailure(const EngineEntry& e) const {
    if (e.machine.empty() || !hub_) return {};
    for (const auto& m : hub_->machines()) {
        if (m.id == e.machine && m.engines == 0 && !m.error.empty()) return m.error;
    }
    return {};
}

}  // namespace fcg

namespace fcg {

App::MachineLine App::machineLine(const std::string& id) const {
    const auto it = std::find_if(machines_.begin(), machines_.end(), [&](const Machine& m) { return m.id == id; });
    if (it == machines_.end()) return {TR("this machine is no longer in Settings > Machines"), ui::BAD};
    if (it->system.checked.empty()) return {TR("never tested: test its connection in Settings > Machines"), ui::WARN};
    if (hub_) {
        for (const auto& state : hub_->machines()) {
            if (state.id != id) continue;
            if (state.connections > 0) {
                auto text = trf("connected · {}", describeSystem(it->system));
                if (state.engines > 0)
                    text += " · " + trn("{} engine playing", "{} engines playing", state.engines, state.engines);
                return {text, ui::GOOD};
            }
            if (!state.error.empty()) return {state.error, ui::BAD};
        }
    }
    return {describeSystem(it->system), ui::MUTED};
}

bool App::drawRunsOn(EngineEntry& e, float label) {
    if (machines_.empty() && e.machine.empty()) return false;
    ui::label(TR("Runs on"), label);
    const auto* current = e.machine.empty() ? nullptr : machineById(e.machine);
    const std::string shown =
        e.machine.empty() ? TR("This computer") : current ? machineName(*current) : TR("a machine no longer listed");
    bool changed = false;
    ImGui::SetNextItemWidth(px(200));
    if (ImGui::BeginCombo("##runs_on", shown.c_str())) {
        if (ImGui::Selectable(TR("This computer"), e.machine.empty()) && !e.machine.empty()) {
            moveEngine(e, {});
            changed = true;
        }
        for (const auto& m : machines_) {
            const auto item = machineName(m) + "##" + m.id;
            if (ImGui::Selectable(item.c_str(), e.machine == m.id) && e.machine != m.id) {
                moveEngine(e, m.id);
                changed = true;
                break;  // moveEngine() may have changed the list of machines' engines, not the machines
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", TR("Where the engine runs when it plays from this computer: here, or on a machine of "
                                   "Settings > Machines, reached with SSH."));
    ui::topic(guide::topics::RemoteEngines);
    if (!e.machine.empty()) {
        ImGui::SameLine();
        const auto line = machineLine(e.machine);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(line.color));
        ImGui::TextUnformatted("●");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        auto text = line.text;
        if (e.round_trip_ms > 0) text += trf(" · round trip {:.1f} ms", e.round_trip_ms);
        ImGui::TextDisabled("%s", text.c_str());
    }
    return changed;
}

void App::moveEngine(EngineEntry& e, const std::string& machine) {
    if (e.machine == machine) return;
    if (e.machine.empty()) {
        e.local_cmd = e.cmd;
        e.local_dir = e.dir;
    }
    e.machine = machine;
    e.remote_sha.clear();
    e.round_trip_ms = 0;
    e.program       = {};
    e.program_state = ProgramState::Unknown;
    e.check_ok      = -1;
    e.check_message.clear();
    if (machine.empty()) {
        e.cmd = std::exchange(e.local_cmd, {});
        e.dir = std::exchange(e.local_dir, {});
        refreshProgram(e);
    } else {
        // The path the machine's list of engines has for it, if any (a tournament on the machine played it).
        const auto* m     = machineById(machine);
        const auto* there = m ? m->engine(e.name) : nullptr;
        e.cmd             = there ? there->path : std::string();
        e.dir.clear();
        mirrorOnMachine(e);
    }
    storage_.saveEngines(engines_);
    syncEngineHub();
    if (!e.cmd.empty()) startProbe(engineIndex(e.name));
}

void App::mirrorOnMachine(const EngineEntry& e) {
    if (e.machine.empty() || e.cmd.empty()) return;
    auto* m = machineById(e.machine);
    if (!m) return;
    for (auto& there : m->engines) {
        if (there.engine != e.name) continue;
        if (there.path == e.cmd) return;
        there.path     = e.cmd;
        there.check_ok = -1;
        there.check_message.clear();
        there.remote_hash.clear();
        there.local_hash.clear();
        saveMachines();
        return;
    }
    MachineEngine added;
    added.engine = e.name;
    added.path   = e.cmd;
    m->engines.push_back(std::move(added));
    saveMachines();
}

void App::addRemoteEngine(const std::string& machine, const std::string& path) {
    EngineEntry entry;
    entry.machine = machine;
    entry.cmd     = path;
    entry.name    = uniqueEngineName(std::filesystem::path(remoteName(path)).stem().string(), -1);
    engines_.push_back(std::move(entry));
    selected_engine_ = static_cast<int>(engines_.size()) - 1;
    engine_marked_.clear();
    mirrorOnMachine(engines_.back());
    storage_.saveEngines(engines_);
    syncEngineHub();
    startProbe(selected_engine_);
}

// --- The programs there ---

void App::hashRemotePrograms(std::vector<std::string> names) {
    if (remote_hashes_.valid()) {
        remote_hashes_next_.insert(remote_hashes_next_.end(), names.begin(), names.end());
        return;
    }
    // By machine, each reached once; those whose passphrase was not typed are left for later.
    std::map<std::string, std::vector<RemoteHash>> by_machine;
    for (const auto& name : names) {
        const auto i = engineIndex(name);
        if (i < 0 || engines_[i].machine.empty() || engines_[i].cmd.empty()) continue;
        by_machine[engines_[i].machine].push_back({name, engines_[i].machine, engines_[i].cmd, {}});
    }
    struct Job {
        SshTarget target;
        RemoteShell shell;
        std::vector<RemoteHash> programs;
    };
    std::vector<Job> jobs;
    for (auto& [id, programs] : by_machine) {
        const auto* m = machineById(id);
        if (!m || m->system.checked.empty()) continue;
        bool missing          = false;
        auto passphrase       = machinePassphrase(*m, missing);
        if (missing) continue;
        Job job{targetOf(*m, storage_.root(), passphrase), m->system.shell, std::move(programs)};
        mbedtls_platform_zeroize(passphrase.data(), passphrase.size());
        job.target.trust_new_host_key = false;
        job.target.timeout            = std::chrono::seconds(15);
        jobs.push_back(std::move(job));
    }
    if (jobs.empty()) return;
    remote_hashes_ = std::async(std::launch::async, [jobs = std::move(jobs)]() mutable {
        std::vector<RemoteHash> out;
        for (auto& job : jobs) {
            SshClient client;
            const bool connected = client.connect(job.target);
            mbedtls_platform_zeroize(job.target.passphrase.data(), job.target.passphrase.size());
            if (!connected) continue;  // the machine away: nothing is concluded
            for (auto& p : job.programs) {
                auto path = p.program;
                if (job.shell == RemoteShell::Cmd) std::replace(path.begin(), path.end(), '/', '\\');
                if (const auto command = hashCommand(job.shell, path)) {
                    if (const auto result = client.run(*command, std::chrono::minutes(2))) p.hash = readHash(result->out);
                }
                out.push_back(std::move(p));
            }
        }
        return out;
    });
}

void App::pollRemoteHashes() {
    if (!remote_hashes_.valid() || remote_hashes_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const auto hashes = remote_hashes_.get();
    bool save = false, machines = false;
    for (const auto& h : hashes) {
        const auto i = engineIndex(h.engine);
        if (i < 0 || h.hash.empty()) continue;
        auto& e = engines_[i];
        if (e.machine != h.machine || e.cmd != h.program) continue;  // moved meanwhile
        if (e.remote_sha.empty()) {
            e.remote_sha    = h.hash;  // the program the last read came from
            e.program_state = ProgramState::Ok;
            save            = true;
        } else if (e.remote_sha != h.hash) {
            e.program_state = ProgramState::Changed;  // rebuilt there: read again, its changes shown
            if (!probing(e.name)) probe_queue_.push_back(e.name);
        } else {
            e.program_state = ProgramState::Ok;
        }
        if (auto* m = machineById(h.machine)) {
            for (auto& there : m->engines) {
                if (there.engine == h.engine && there.path == h.program && there.remote_hash != h.hash) {
                    there.remote_hash = h.hash;
                    machines          = true;
                }
            }
        }
    }
    if (save) storage_.saveEngines(engines_);
    if (machines) saveMachines();
    if (!remote_hashes_next_.empty()) hashRemotePrograms(std::exchange(remote_hashes_next_, {}));
}

// --- The files of a machine ---

void App::openRemotePicker(RemotePicker::Purpose purpose, const std::string& machine, const std::string& engine,
                           const std::string& option, const std::string& start) {
    const auto* m = machineById(machine);
    if (!m) return;
    // The passphrase first, then the files.
    bool missing = false;
    (void)machinePassphrase(*m, missing);
    if (missing && !m->system.checked.empty()) {
        passphrase_machine_ = machine;
        passphrase_then_    = [this, purpose, machine, engine, option, start] {
            openRemotePicker(purpose, machine, engine, option, start);
        };
        open_passphrase_ = true;
        return;
    }
    RemotePicker picker;
    picker.purpose  = purpose;
    picker.machine  = machine;
    picker.engine   = engine;
    picker.option   = option;
    picker.folders  = purpose == RemotePicker::Purpose::Folder || purpose == RemotePicker::Purpose::ImportFolder ||
                     purpose == RemotePicker::Purpose::OptionFolder;
    picker.untested = m->system.checked.empty();
    remote_picker_  = std::move(picker);
    if (remote_picker_->untested) return;
    // From the folder of the file it had, or from the folder it had; the home folder otherwise.
    std::string dir = start;
    if (!dir.empty() && !remote_picker_->folders) dir = remoteParent(dir);
    listRemote(dir);
}

void App::listRemote(const std::string& dir) {
    if (!remote_picker_) return;
    const auto* m = machineById(remote_picker_->machine);
    if (!m) return;
    bool missing          = false;
    auto passphrase       = machinePassphrase(*m, missing);
    auto target           = targetOf(*m, storage_.root(), passphrase);
    target.trust_new_host_key = false;
    target.timeout            = std::chrono::seconds(10);
    mbedtls_platform_zeroize(passphrase.data(), passphrase.size());
    const bool folders = remote_picker_->folders;
    const auto path    = dir.empty() ? std::string(".") : sftpPath(untilde(dir, remote_picker_->home));
    remote_picker_->error.clear();
    remote_picker_->listing = std::async(std::launch::async, [target, path, folders]() mutable {
        RemoteListing out;
        SshClient client;
        const bool connected = client.connect(target);
        mbedtls_platform_zeroize(target.passphrase.data(), target.passphrase.size());
        if (!connected) {
            out.error = client.error().message;
            return out;
        }
        if (const auto home = client.realPath(".")) out.home = *home;
        const auto real = client.realPath(path);
        if (!real) {
            out.error = client.error().message;
            return out;
        }
        if (const auto info = client.stat(*real); info && !info->directory) {
            if (!folders) {
                out.file = *real;
                return out;
            }
        }
        const auto list = client.list(*real);
        if (!list) {
            out.error = client.error().message;
            return out;
        }
        out.dir = *real;
        for (const auto& e : *list) {
            if (e.name != "." && e.name != "..") out.entries.push_back(e);
        }
        std::sort(out.entries.begin(), out.entries.end(), [](const RemoteEntry& a, const RemoteEntry& b) {
            if (a.directory != b.directory) return a.directory;
            return a.name < b.name;
        });
        return out;
    });
}

void App::chooseRemote(const std::string& sftp_path) {
    if (!remote_picker_) return;
    const auto picker = std::move(*remote_picker_);
    remote_picker_.reset();
    const auto* m = machineById(picker.machine);
    if (!m) return;
    const auto path = fromSftp(sftp_path, m->system.shell);
    const auto index = engineIndex(picker.engine);
    using Purpose    = RemotePicker::Purpose;
    switch (picker.purpose) {
        case Purpose::AddEngine:
            addRemoteEngine(picker.machine, path);
            break;
        case Purpose::ImportFolder:
            for (const auto& e : picker.entries) {
                if (!runnable(e, m->system.shell)) continue;
                const auto program = fromSftp(remoteJoin(picker.dir, e.name), m->system.shell);
                const bool known   = std::any_of(engines_.begin(), engines_.end(), [&](const EngineEntry& o) {
                    return o.machine == picker.machine && o.cmd == program;
                });
                if (!known) addRemoteEngine(picker.machine, program);
            }
            break;
        case Purpose::Program:
        case Purpose::Folder:
        case Purpose::Option:
        case Purpose::OptionFolder:
            if (index < 0) break;
            {
                auto& e = engines_[index];
                if (picker.purpose == Purpose::Program) {
                    e.cmd = path;
                    mirrorOnMachine(e);
                } else if (picker.purpose == Purpose::Folder) {
                    e.dir = path;
                } else {
                    e.setOption(picker.option, path);
                }
                storage_.saveEngines(engines_);
                syncEngineHub();
                if (picker.purpose == Purpose::Program || picker.purpose == Purpose::Folder) startProbe(index);
            }
            break;
        case Purpose::MachinePath:
            if (auto* machine = machineById(picker.machine)) {
                for (auto& there : machine->engines) {
                    if (there.engine != picker.engine) continue;
                    there.path     = path;
                    there.check_ok = -1;
                    there.remote_hash.clear();
                    there.local_hash.clear();
                }
                saveMachines();
                // An engine that runs on the machine plays that program from now on.
                if (index >= 0 && engines_[index].machine == picker.machine) {
                    engines_[index].cmd = path;
                    storage_.saveEngines(engines_);
                    syncEngineHub();
                    startProbe(index);
                }
            }
            break;
    }
}

void App::drawRemotePicker() {
    if (!remote_picker_) return;
    auto& p = *remote_picker_;
    if (!p.shown) {
        ImGui::OpenPopup("##remote_picker");
        p.shown = true;
    }
    if (p.listing.valid() && p.listing.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto listing = p.listing.get();
        if (!listing.file.empty()) {
            chooseRemote(listing.file);
            return;
        }
        p.error = listing.error;
        if (!listing.home.empty()) p.home = listing.home;
        if (listing.error.empty()) {
            p.dir     = listing.dir;
            p.entries = std::move(listing.entries);
            p.selected.clear();
            std::snprintf(p.typed.data(), p.typed.size(), "%s", tilde(p.dir, p.home).c_str());
        }
    }
    const auto* m = machineById(p.machine);
    ImGui::SetNextWindowSize(ImVec2(px(660), px(460)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##remote_picker", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        if (!ImGui::IsPopupOpen("##remote_picker")) remote_picker_.reset();
        return;
    }
    if (!m) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        remote_picker_.reset();
        return;
    }
    using Purpose     = RemotePicker::Purpose;
    const auto name   = machineName(*m);
    const auto shell  = m->system.shell;
    const auto title  = p.purpose == Purpose::AddEngine      ? trf("A program on {}", name)
                        : p.purpose == Purpose::ImportFolder ? trf("A folder of engines on {}", name)
                        : p.purpose == Purpose::Folder       ? trf("The working folder of {} on {}", p.engine, name)
                        : p.purpose == Purpose::Option || p.purpose == Purpose::OptionFolder
                            ? trf("{} of {} on {}", p.option, p.engine, name)
                                                             : trf("The program of {} on {}", p.engine, name);
    if (auto* font = ui::titleFont()) ImGui::PushFont(font, 0.0f);
    ImGui::TextUnformatted(title.c_str());
    if (ui::titleFont()) ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", fmt::format("{} · {}", describeSystem(m->system), m->host).c_str());

    bool close = false;
    if (p.untested) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", trf("{} has never been tested: its host key is not known yet, so nothing is sent to "
                                     "it. Test its connection first, then look again.",
                                     name)
                                     .c_str());
        if (ImGui::Button(trf("Test {}", name).c_str())) startMachineJob(m->id, MachineJob::Kind::Test);
        ImGui::SameLine();
        if (ImGui::Button(TR("Look again"))) {
            const auto purpose = p.purpose;
            const auto machine = p.machine, engine = p.engine, option = p.option;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            remote_picker_.reset();
            openRemotePicker(purpose, machine, engine, option);
            return;
        }
        ImGui::SameLine();
        close = ImGui::Button(TR("Cancel##remote_picker"));
    } else {
        const bool loading = p.listing.valid();
        ImGui::BeginDisabled(loading || p.dir.empty());
        if (ui::iconOnlyButton("##remote_up", ui::Icon::FolderUp, TR("The folder above"))) listRemote(remoteParent(p.dir));
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##remote_path", p.typed.data(), p.typed.size(), ImGuiInputTextFlags_EnterReturnsTrue))
            listRemote(p.typed.data());
        ImGui::SetItemTooltip("%s", TR("A path there, typed or pasted, then Enter."));

        const float footer = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::BeginChild("##remote_files", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
            if (loading) {
                ImGui::TextDisabled("%s", TR("Reading the folder…"));
            } else if (!p.error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::BAD));
                ImGui::TextWrapped("%s", p.error.c_str());
                ImGui::PopStyleColor();
            } else if (ImGui::BeginTable("##remote_entries", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, px(20));
                ImGui::TableSetupColumn(TR("Name"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(TR("Size"), ImGuiTableColumnFlags_WidthFixed, px(80));
                ImGui::TableSetupColumn(TR("Modified"), ImGuiTableColumnFlags_WidthFixed, px(90));
                ImGui::TableHeadersRow();
                std::string enter, pick;
                for (const auto& e : p.entries) {
                    if (!p.hidden && !e.name.empty() && e.name[0] == '.') continue;
                    const bool program = runnable(e, shell);
                    const bool usable  = e.directory || (!p.folders && e.regular);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ui::inlineIcon(e.directory ? ui::Icon::FolderClosed : program ? ui::Icon::Program : ui::Icon::File,
                                   usable ? ImGui::GetColorU32(ImGuiCol_Text) : ui::MUTED);
                    ImGui::TableNextColumn();
                    if (!usable) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    const bool bold = program && !p.folders && ui::titleFont();
                    if (bold) ImGui::PushFont(ui::titleFont(), 0.0f);
                    const auto label = e.name + (e.directory ? "/" : "") + "##" + e.name;
                    if (ImGui::Selectable(label.c_str(), p.selected == e.name,
                                          ImGuiSelectableFlags_SpanAllColumns |
                                              ImGuiSelectableFlags_AllowDoubleClick) &&
                        usable) {
                        p.selected = e.name;
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            if (e.directory)
                                enter = e.name;
                            else
                                pick = e.name;
                        }
                    }
                    if (bold) ImGui::PopFont();
                    if (!usable) ImGui::PopStyleColor();
                    ImGui::TableNextColumn();
                    if (e.regular) ImGui::TextDisabled("%s", formatBytes(e.size).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", dayOf(e.mtime).c_str());
                }
                ImGui::EndTable();
                if (!enter.empty()) listRemote(remoteJoin(p.dir, enter));
                if (!pick.empty()) {
                    ImGui::EndChild();
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    chooseRemote(remoteJoin(p.dir, pick));
                    return;
                }
            }
        }
        ImGui::EndChild();
        ImGui::TextDisabled("%s", p.folders ? TR("A folder is chosen: open it, then Choose this folder.")
                                            : TR("The programs that can be run are in bold. A path can be typed or "
                                                 "pasted."));
        const auto selected =
            std::find_if(p.entries.begin(), p.entries.end(), [&](const RemoteEntry& e) { return e.name == p.selected; });
        const bool file_chosen = !p.folders && selected != p.entries.end() && selected->regular;
        std::string action;
        if (p.folders) {
            action = p.purpose == Purpose::ImportFolder ? TR("Add the programs of this folder") : TR("Choose this folder");
        } else if (p.purpose == Purpose::AddEngine) {
            action = file_chosen ? trf("Add {}", selected->name) : TR("Add");
        } else {
            action = TR("Choose");
        }
        if (ImGui::Button(TR("Cancel##remote_picker"))) close = true;
        ImGui::SameLine();
        ImGui::Checkbox(TR("Hidden files"), &p.hidden);
        ImGui::SetItemTooltip("%s", TR("Those whose name starts with a dot."));
        ImGui::SameLine();
        ImGui::BeginDisabled(loading || !p.error.empty() || (!p.folders && !file_chosen) || (p.folders && p.dir.empty()));
        if (ImGui::Button(action.c_str())) {
            const auto chosen = p.folders ? p.dir : remoteJoin(p.dir, selected->name);
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            chooseRemote(chosen);
            return;
        }
        ImGui::EndDisabled();
    }
    if (close || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        remote_picker_.reset();
        return;
    }
    ImGui::EndPopup();
}

}  // namespace fcg
