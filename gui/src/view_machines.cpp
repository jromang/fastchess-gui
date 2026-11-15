// The remote machines, in Settings: each machine's card (how to reach it, its folder and budgets), the test of the
// connection, Install / Update of fastchess-gui there, the paths of the library's engines on it, and the books and
// tablebases the setup uses, compared with the machine's and copied there. The jobs run off the UI thread
// (machine_jobs.hpp); a key file's passphrase is asked once, and kept in memory until the program ends.

#include <algorithm>
#include <cfloat>
#include <cstring>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <fmt/include/fmt/format.h>
#include <mbedtls/platform_util.h>
#include <core/globals/globals.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "memory.hpp"
#include "notice.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool editField(const char* id, std::string& value, float width, const char* hint = nullptr) {
    char buffer[1024];
    const auto n = std::min(value.size(), sizeof(buffer) - 1);
    std::copy_n(value.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(width);
    const bool changed = hint ? ImGui::InputTextWithHint(id, hint, buffer, sizeof(buffer))
                              : ImGui::InputText(id, buffer, sizeof(buffer));
    if (changed) value = buffer;
    return changed;
}

std::string remainingText(std::chrono::seconds s) {
    const auto minutes = s.count() / 60;
    if (minutes >= 60) return trf("{} h {} min left", minutes / 60, minutes % 60);
    if (minutes >= 1) return trf("{} min left", minutes);
    return trf("{} s left", s.count());
}

// The machine's line when its card is folded: its system and fastchess-gui, as the last test found them.
std::string summaryOf(const Machine& m) {
    const auto& s = m.system;
    if (s.checked.empty()) return TR("not tested yet");
    std::string out = s.os + " " + s.arch;
    out += " · " + trn("{} thread", "{} threads", static_cast<uint64_t>(s.threads), s.threads);
    out += " · " + formatMemory(s.memory_mb);
    out += " · " +
           (s.fcg_version.empty() ? std::string(TR("fastchess-gui not installed")) : "fastchess-gui " + s.fcg_version);
    return out;
}

void wipe(std::string& secret) {
    if (!secret.empty()) mbedtls_platform_zeroize(secret.data(), secret.size());
    secret.clear();
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------

void App::loadMachines() { machines_ = fcg::loadMachines(storage_.root()); }

void App::saveMachines() {
    if (!fcg::saveMachines(storage_.root(), machines_)) {
        Console::instance().add(trf("Cannot write {}", (storage_.root() / "machines.json").string()), true);
    }
}

void App::forgetPassphrases() {
    for (auto& [file, secret] : machine_passphrases_) wipe(secret);
    machine_passphrases_.clear();
    passphrase_input_.fill('\0');
}

Machine* App::machineById(const std::string& id) {
    for (auto& m : machines_) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

void App::addMachine() {
    Machine m;
    m.id   = newMachineId(machines_);
    m.name = trf("Machine {}", machines_.size() + 1);
    machines_.push_back(m);
    machine_open_ = m.id;
    saveMachines();
}

FilesRequest App::setupFiles() const {
    FilesRequest request;
    const auto add = [&](const std::string& path) {
        if (path.empty()) return;
        const std::filesystem::path p(path);
        if (std::find(request.books.begin(), request.books.end(), p) == request.books.end()) {
            request.books.push_back(p);
        }
    };
    add(setup_.book);
    add(setup_.book_a.path);
    add(setup_.book_b.path);
    for (const auto& p : setup_.participants) add(p.book.path);
    if (setup_.tb_enabled || setup_.engine_syzygy) {
        request.syzygy_paths  = setup_.tb_paths;
        request.syzygy_pieces = setup_.tb_enabled ? setup_.tb_pieces : 0;
    }
    return request;
}

void App::startMachineJob(const std::string& id, MachineJob::Kind kind) {
    auto* machine = machineById(id);
    if (!machine) return;
    auto& activity = machine_activity_[id];
    activity.message.clear();
    if (const auto missing = incompleteMachine(*machine); !missing.empty()) {
        activity.message = missing;
        return;
    }
    std::string passphrase;
    if (machine->auth == SshAuth::KeyFile) {
        const auto info = inspectKeyFile(machine->key_file);
        if (!info.error.empty()) {
            activity.message = info.error;
            return;
        }
        if (info.encrypted) {
            const auto known = machine_passphrases_.find(machine->key_file);
            if (known == machine_passphrases_.end()) {
                // Asked first; the job starts once it is typed.
                passphrase_machine_ = id;
                passphrase_kind_    = kind;
                open_passphrase_    = true;
                return;
            }
            passphrase = known->second;
        }
    }
    const auto target = targetOf(*machine, storage_.root(), passphrase);
    wipe(passphrase);
    const Machine copy = *machine;
    MachineJob::Work work;
    switch (kind) {
        case MachineJob::Kind::Test:
            work = [copy](SshClient& c, JobState& s, JobResult& r) { testMachine(c, copy, s, r); };
            break;
        case MachineJob::Kind::Install: {
            std::string error;
            const auto program =
                programToInstall(copy, std::filesystem::path(fastchess::engine::xboard_adapter), error);
            if (program.empty()) {
                activity.message = error;
                return;
            }
            work = [copy, program](SshClient& c, JobState& s, JobResult& r) { installProgram(c, copy, program, s, r); };
            break;
        }
        case MachineJob::Kind::Engines: {
            std::map<std::string, std::string> protocols;
            for (const auto& e : engines_) protocols[e.name] = e.protocol;
            work = [copy, protocols](SshClient& c, JobState& s, JobResult& r) {
                checkEngines(c, copy, protocols, std::chrono::seconds(10), s, r);
            };
            break;
        }
        case MachineJob::Kind::Files: {
            const auto request = setupFiles();
            work = [copy, request](SshClient& c, JobState& s, JobResult& r) { checkFiles(c, copy, request, s, r); };
            break;
        }
        case MachineJob::Kind::Push: {
            if (!activity.files) return;
            const auto files = *activity.files;
            work = [copy, files](SshClient& c, JobState& s, JobResult& r) { pushFiles(c, copy, files, s, r); };
            activity.transfer = std::make_unique<MachineJob>(kind, target, std::move(work));
            return;
        }
    }
    activity.job       = std::make_unique<MachineJob>(kind, target, std::move(work));
    activity.last_kind = kind;
}

void App::pollMachines() {
    bool changed = false;
    for (auto& [id, activity] : machine_activity_) {
        auto* machine = machineById(id);
        for (auto* slot : {&activity.job, &activity.transfer}) {
            if (!*slot || !(*slot)->finished()) continue;
            const auto kind = (*slot)->kind();
            auto result     = (*slot)->take();
            slot->reset();
            if (!machine) continue;
            if (!result.host_key.empty() && result.error.new_key.empty()) machine->host_key = result.host_key;
            if (result.system) {
                machine->system = *result.system;
                changed         = true;
            }
            for (const auto& checked : result.engines) {
                for (auto& e : machine->engines) {
                    if (e.engine == checked.engine && e.path == checked.path) e = checked;
                }
                changed = true;
            }
            if (result.files) activity.files = result.files;
            // A passphrase the key refused is asked again next time.
            if (result.error.kind == SshError::Kind::Passphrase) {
                if (auto it = machine_passphrases_.find(machine->key_file); it != machine_passphrases_.end()) {
                    wipe(it->second);
                    machine_passphrases_.erase(it);
                }
            }
            if (kind == MachineJob::Kind::Push) {
                activity.last_transfer = std::move(result);
                // What is there now: compared again.
                if (activity.last_transfer->ok) {
                    activity.files.reset();
                    startMachineJob(id, MachineJob::Kind::Files);
                }
            } else {
                activity.last      = std::move(result);
                activity.last_kind = kind;
            }
        }
    }
    if (changed) saveMachines();
}

// ---------------------------------------------------------------------------------------------------------------

void App::drawMachineSettings(float label) {
    if (scroll_to_machines_) {
        ImGui::SetScrollHereY(0.0f);
        scroll_to_machines_ = false;
    }
    (void)label;
    ImGui::PushID("machines");
    ui::pageTitle(TR("Machines"), guide::Section::Machines);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", TR("Other computers to run tournaments on, reached with SSH."));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("A test box, a server or a second PC with an SSH server (on Windows, its built-in "
                                   "OpenSSH Server): the window logs in with your SSH agent or a key file, finds out "
                                   "the machine, installs fastchess-gui there and copies the books and tablebases. "
                                   "Nothing secret is saved: the key's passphrase is asked once a session."));
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Add a machine"))) addMachine();
    for (std::size_t i = 0; i < machines_.size(); ++i) drawMachineCard(i, label);
    ImGui::PopID();
    drawPassphrasePrompt();
}

void App::drawMachineCard(std::size_t index, float label) {
    auto& m         = machines_[index];
    auto& activity  = machine_activity_[m.id];
    const bool busy = activity.job != nullptr;
    ImGui::PushID(m.id.c_str());

    // The folded line: a mark for the last test, the name, the address and what was found.
    const bool open  = machine_open_ == m.id;
    const auto& last = activity.last;
    const ImU32 mark = busy                                              ? IM_COL32(150, 150, 150, 255)
                       : last && !last->ok                               ? ui::BAD
                       : !m.system.checked.empty() || (last && last->ok) ? ui::GOOD
                                                                         : IM_COL32(150, 150, 150, 255);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(mark));
    ImGui::TextUnformatted("●");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Selectable(fmt::format("{}##card", m.name.empty() ? m.host : m.name).c_str(), open,
                          ImGuiSelectableFlags_AllowOverlap, ImVec2(px(160), 0))) {
        machine_open_ = open ? std::string() : m.id;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m.host.empty() ? TR("no host yet") : m.address().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", summaryOf(m).c_str());
    if (!open) {
        ImGui::SameLine();
        ImGui::BeginDisabled(busy);
        if (ImGui::SmallButton(TR("Test"))) startMachineJob(m.id, MachineJob::Kind::Test);
        ImGui::EndDisabled();
        ImGui::PopID();
        return;
    }

    ui::beginCard("machine_card");
    const float field = px(220);
    bool save         = false;

    // How to reach it.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Name"));
    ImGui::SameLine(px(110));
    editField("##name", m.name, field);
    save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Host"));
    ImGui::SameLine(px(110));
    editField("##host", m.host, field, "box.example.org");
    save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("%s", TR("The machine's name on the network, or its address (192.168.1.20)."));
    ImGui::SameLine();
    ImGui::TextUnformatted(TR("Port"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(70));
    if (ImGui::InputInt("##port", &m.port, 0)) m.port = std::clamp(m.port, 1, 65535);
    save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("User"));
    ImGui::SameLine(px(110));
    editField("##user", m.user, field);
    save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("%s", TR("The account to log in as on the machine: its tournaments run as this user."));

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Log in with"));
    ImGui::SameLine(px(110));
    int auth = m.auth == SshAuth::KeyFile ? 1 : 0;
    if (ImGui::RadioButton(TR("SSH agent"), &auth, 0)) save = true;
    ImGui::SetItemTooltip("%s", TR("The keys of your SSH agent (ssh-agent, Pageant, or the OpenSSH Authentication "
                                   "Agent service of Windows): add yours with ssh-add. The key never leaves it."));
    ImGui::SameLine();
    if (ImGui::RadioButton(TR("Key file"), &auth, 1)) save = true;
    ImGui::SetItemTooltip("%s", TR("A private key file, RSA or Ed25519 (~/.ssh/id_ed25519): its passphrase, if it has "
                                   "one, is asked once a session and never saved."));
    m.auth = auth == 1 ? SshAuth::KeyFile : SshAuth::Agent;
    if (m.auth == SshAuth::KeyFile) {
        ImGui::SameLine();
        editField("##key", m.key_file, px(260), "~/.ssh/id_ed25519");
        save |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        if (ImGui::Button(TR("Browse…##key"))) {
            machine_dialog_for_ = m.id;
            dialogs_.openFile(Dialogs::Purpose::MachineKey, {}, m.key_file);
        }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Folder there"));
    ImGui::SameLine(px(110));
    editField("##folder", m.folder, field);
    save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("%s", TR("fastchess-gui's folder on the machine, for the program, its tournaments, books and "
                                   "tablebases: relative to the user's home folder, or absolute (/srv/fcg, D:\\fcg)."));

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Budget"));
    ImGui::SameLine(px(110));
    ImGui::SetNextItemWidth(px(70));
    if (ImGui::InputInt("##cores", &m.cores, 0)) m.cores = std::max(0, m.cores);
    save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextUnformatted(trf("cores (now {})", m.coreBudget()).c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(90));
    if (ImGui::InputInt("##memory", &m.memory_mb, 0)) m.memory_mb = std::max(0, m.memory_mb);
    save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextUnformatted(trf("MB of memory (now {})", formatMemory(static_cast<uint64_t>(m.memoryBudget()))).c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("What the tournaments on this machine may use at once. 0 takes what the test "
                                   "found: every thread but one, and 80 % of the memory."));
    }

    // The test, Install / Update, what they found.
    ImGui::Spacing();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(TR("Test the connection"))) startMachineJob(m.id, MachineJob::Kind::Test);
    ImGui::SetItemTooltip("%s", TR("Logs in, then tells the machine's system, threads, memory and free space, and "
                                   "whether fastchess-gui is installed in its folder."));
    ImGui::SameLine();
    const bool installed = !m.system.fcg_version.empty();
    const bool current   = installed && m.system.fcg_version == FCG_VERSION && m.system.fcg_protocol == REMOTE_PROTOCOL;
    if (ImGui::Button(installed ? TR("Update fastchess-gui") : TR("Install fastchess-gui"))) {
        startMachineJob(m.id, MachineJob::Kind::Install);
    }
    ImGui::SetItemTooltip("%s", trf("Copies fastchess-gui {} to {} on the machine (resumed if the copy is cut), then "
                                    "checks it starts there.",
                                    FCG_VERSION, m.folder)
                                    .c_str());
    if (current) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("up to date"));
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel##machine"))) activity.job->cancel();
        ImGui::SameLine();
        const auto& state = activity.job->state();
        ImGui::TextDisabled("%s", state.step().c_str());
        if (state.bytes_total > 0) {
            ImGui::ProgressBar(static_cast<float>(state.bytes_done) / static_cast<float>(state.bytes_total),
                               ImVec2(px(300), 0));
        }
    }
    // The program for another system than this one's.
    const auto remote = normalizePlatform(m.system.os, m.system.arch);
    if (!m.system.os.empty() && (remote != thisPlatform() || !m.program.empty())) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(trf("fastchess-gui for {} {}", remote.os, remote.arch).c_str());
        ImGui::SameLine();
        editField("##program", m.program, px(300));
        save |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("%s", TR("This computer's fastchess-gui does not run on the machine's system: download "
                                       "the release for it and choose its program here (fastchess-gui.exe for "
                                       "Windows). Install copies it."));
        ImGui::SameLine();
        if (ImGui::Button(TR("Browse…##program"))) {
            machine_dialog_for_ = m.id;
            dialogs_.openFile(Dialogs::Purpose::MachineProgram, {}, m.program);
        }
    }
    if (!activity.message.empty()) {
        drawNotice({Notice::Level::Error, activity.message, {}, {}}, "machine_message");
    }
    if (last && !busy && activity.last_kind != MachineJob::Kind::Files) {
        int i = 0;
        for (const auto& line : last->lines) {
            drawNotice({Notice::Level::Ok, line, {}, {}}, fmt::format("line{}", i++).c_str());
        }
        if (last->error) {
            drawNotice({Notice::Level::Error, last->error.message, {}, {}}, "machine_error");
            if (!last->error.new_key.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Trust the new key"))) {
                    rememberHostKey(knownHostsFile(storage_.root()), m.host, m.port, last->error.new_key);
                    activity.last.reset();
                    startMachineJob(m.id, MachineJob::Kind::Test);
                }
                ImGui::SetItemTooltip("%s", TR("Only if the machine was reinstalled, or its owner says its key "
                                               "changed: otherwise someone may be in the way."));
            }
        }
    }

    // The engines, and the books and tablebases.
    ImGui::Spacing();
    if (ImGui::TreeNode(trf("Engines on this machine ({})###engines", m.engines.size()).c_str())) {
        drawMachineEngines(m, activity);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode(TR("Books and tablebases###files"))) {
        drawMachineFiles(m, activity);
        ImGui::TreePop();
    }

    ImGui::Spacing();
    if (ImGui::SmallButton(TR("Remove this machine"))) activity.remove_asked = true;
    if (activity.remove_asked) {
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s",
                           trf("{} is forgotten here; nothing is deleted on it.", m.name).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Remove"))) {
            const auto id = m.id;
            machine_activity_.erase(id);
            machines_.erase(machines_.begin() + static_cast<std::ptrdiff_t>(index));
            machine_open_.clear();
            saveMachines();
            ui::endCard();
            ImGui::PopID();
            return;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Keep"))) activity.remove_asked = false;
    }
    ui::endCard();
    if (save) saveMachines();
    ImGui::PopID();
}

void App::drawMachineEngines(Machine& m, MachineActivity& activity) {
    bool save       = false;
    const bool busy = activity.job != nullptr;
    if (m.engines.empty()) {
        ImGui::TextDisabled("%s", TR("No engine on this machine yet: add the library's engines it has, with their "
                                     "path there."));
    } else if (ImGui::BeginTable("machine_engines", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn(TR("Engine"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(TR("Path on the machine"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn(TR("Check"), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, px(24));
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < m.engines.size(); ++i) {
            auto& e = m.engines[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(e.engine.c_str());
            ImGui::TableNextColumn();
            if (editField("##path", e.path, -FLT_MIN,
                          m.system.shell == RemoteShell::Cmd ? "C:\\Engines\\sf.exe" : "/opt/engines/stockfish")) {
                e.check_ok = -1;
                e.check_message.clear();
            }
            save |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::TableNextColumn();
            if (e.check_ok >= 0) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImGui::ColorConvertU32ToFloat4(e.check_ok == 1 ? ui::GOOD : ui::BAD));
                ImGui::TextUnformatted(e.check_ok == 1 ? "✓" : "✗");
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextUnformatted(e.check_ok == 1 && !e.id_name.empty() ? e.id_name.c_str() : TR("fails"));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.check_message.c_str());
            } else {
                ImGui::TextDisabled("%s", e.path.empty() ? TR("no path") : TR("not checked"));
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("×")) {
                m.engines.erase(m.engines.begin() + static_cast<std::ptrdiff_t>(i));
                save = true;
                ImGui::PopID();
                break;
            }
            ImGui::SetItemTooltip("%s", TR("Off this machine's list (the engine stays in the library)."));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    // The library's engines not on the list yet.
    ImGui::SetNextItemWidth(px(220));
    if (ImGui::BeginCombo("##add_engine", TR("Add an engine…"))) {
        for (const auto& e : engines_) {
            if (e.inactive || m.engine(e.name)) continue;
            if (ImGui::Selectable(e.name.c_str())) {
                m.engines.push_back({e.name, {}, -1, {}, {}});
                save = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || m.engines.empty());
    if (ImGui::Button(TR("Check the engines"))) startMachineJob(m.id, MachineJob::Kind::Engines);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", TR("For each path: the program is there, can be run, and answers uci (xboard for a "
                                   "WinBoard engine) within 10 seconds."));
    if (activity.last && activity.last_kind == MachineJob::Kind::Engines && !busy && activity.last->error) {
        drawNotice({Notice::Level::Error, activity.last->error.message, {}, {}}, "engines_error");
    }
    if (save) saveMachines();
}

void App::drawMachineFiles(Machine& m, MachineActivity& activity) {
    bool save       = false;
    const bool busy = activity.job != nullptr || activity.transfer != nullptr;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Books folder"));
    ImGui::SameLine(px(150));
    editField("##books_dir", m.books_dir, px(260), m.booksDir().c_str());
    save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Tablebases folder"));
    ImGui::SameLine(px(150));
    editField("##syzygy_dir", m.syzygy_dir, px(260), m.syzygyDir().c_str());
    save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("%s", TR("Where the Syzygy files go on the machine, the engines' SyzygyPath there."));

    ImGui::BeginDisabled(busy);
    if (ImGui::Button(TR("Check what the setup uses"))) {
        activity.files.reset();
        startMachineJob(m.id, MachineJob::Kind::Files);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", TR("The setup's opening book and the participants' own books, compared by size and "
                                   "hash with the machine's; its Syzygy files (6 pieces at most), by name and size. A "
                                   "tournament does not start on a machine without its book; tablebases missing are "
                                   "a warning."));
    if (activity.job && activity.job->kind() == MachineJob::Kind::Files) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", activity.job->state().step().c_str());
    }

    if (activity.files && !activity.transfer) {
        const auto& f = *activity.files;
        int i         = 0;
        for (const auto& b : f.books) {
            const auto file = b.local.filename().string();
            Notice n;
            switch (b.state) {
                case BookCheck::State::Same:
                    n = {Notice::Level::Ok, trf("Book {}: on {}, the same.", file, m.name), {}, {}};
                    break;
                case BookCheck::State::Missing:
                    n = {Notice::Level::Error,
                         trf("Book {} missing on {} ({}).", file, m.name, formatBytes(b.size)),
                         b.remote,
                         {}};
                    break;
                case BookCheck::State::Different:
                    n = {Notice::Level::Error,
                         trf("Book {}: {} has another file of that name ({} to copy).", file, m.name,
                             formatBytes(b.size)),
                         b.remote,
                         {}};
                    break;
                case BookCheck::State::Unreadable:
                    n = {Notice::Level::Warning,
                         trf("Book {} cannot be read on this computer.", b.local.string()),
                         {},
                         {}};
                    break;
            }
            drawNotice(n, fmt::format("book{}", i++).c_str());
        }
        if (f.syzygy_local > 0) {
            const auto lacking = f.syzygy.missing.size() + f.syzygy.different.size();
            if (lacking == 0) {
                drawNotice({Notice::Level::Ok,
                            trn("Syzygy: the {} file is on {}.", "Syzygy: the {} files are on {}.", f.syzygy_local,
                                f.syzygy_local, m.name),
                            {},
                            {}},
                           "syzygy");
            } else {
                std::string names;
                for (const auto* list : {&f.syzygy.missing, &f.syzygy.different}) {
                    for (const auto& t : *list) names += (names.empty() ? "" : ", ") + t.name;
                }
                drawNotice({Notice::Level::Warning,
                            trn("Syzygy: {} file missing on {} ({}).", "Syzygy: {} files missing on {} ({}).", lacking,
                                lacking, m.name, formatBytes(f.syzygy.bytes)),
                            names,
                            {}},
                           "syzygy");
            }
        } else if (f.books.empty()) {
            ImGui::TextDisabled("%s", TR("The setup uses no book and no tablebases."));
        }
        if (f.bytesToPush() > 0) {
            ImGui::BeginDisabled(busy);
            if (ImGui::Button(trf("Push the missing files ({})", formatBytes(f.bytesToPush())).c_str())) {
                startMachineJob(m.id, MachineJob::Kind::Push);
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("%s", TR("Copies them to the machine's folders in the background, each one checked "
                                           "after; a copy cut short resumes where it stopped."));
        }
    }
    if (activity.transfer) {
        const auto& state = activity.transfer->state();
        const auto total  = state.bytes_total.load();
        const auto done   = state.bytes_done.load();
        std::string text  = trf("{} of {}", formatBytes(done), formatBytes(total));
        if (const auto left = activity.transfer->remaining()) text += " — " + remainingText(*left);
        ImGui::ProgressBar(total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.0f, ImVec2(px(320), 0),
                           text.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel##push"))) activity.transfer->cancel();
        ImGui::TextDisabled("%s", state.step().c_str());
    } else if (activity.last_transfer) {
        if (activity.last_transfer->error) {
            drawNotice({Notice::Level::Error, activity.last_transfer->error.message, {}, {}}, "push_error");
        }
    }
    if (activity.last && activity.last_kind == MachineJob::Kind::Files && activity.last->error && !activity.job) {
        drawNotice({Notice::Level::Error, activity.last->error.message, {}, {}}, "files_error");
    }
    if (save) saveMachines();
}

void App::drawPassphrasePrompt() {
    if (open_passphrase_) {
        ImGui::OpenPopup("##machine_passphrase");
        open_passphrase_ = false;
        passphrase_input_.fill('\0');
    }
    if (!ImGui::BeginPopupModal("##machine_passphrase", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const auto* machine = machineById(passphrase_machine_);
    if (!machine) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::TextUnformatted(trf("The passphrase of {}, for {}", machine->key_file, machine->name).c_str());
    ImGui::TextDisabled("%s", TR("Kept in memory until fastchess-gui ends, never saved."));
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(px(320));
    const bool enter = ImGui::InputText("##passphrase", passphrase_input_.data(), passphrase_input_.size(),
                                        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    const auto id    = passphrase_machine_;
    const auto kind  = passphrase_kind_;
    if (ImGui::Button(TR("OK##passphrase")) || enter) {
        machine_passphrases_[machine->key_file] = passphrase_input_.data();
        passphrase_input_.fill('\0');
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        startMachineJob(id, kind);
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel##passphrase")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        passphrase_input_.fill('\0');
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace fcg
