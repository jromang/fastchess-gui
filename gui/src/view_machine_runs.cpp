// Tournaments played on a remote machine (machine_run.hpp): started there from the setup's Run on, or resumed there,
// followed in a tab as a tournament of this computer is (RemoteRun, its control channel through SSH), connected again
// after a cut, their files copied back as they grow; each machine's budget shared by its tournaments; Run on in the
// setup.

#include <algorithm>
#include <cfloat>

#include <imgui.h>

#include <core/globals/globals.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "memory.hpp"
#include "remote_run.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

}  // namespace

std::string App::machinePassphrase(const Machine& machine, bool& missing) {
    missing = false;
    if (machine.auth != SshAuth::KeyFile) return {};
    const auto info = inspectKeyFile(machine.key_file);
    if (!info.encrypted) return {};
    const auto it = machine_passphrases_.find(machine.key_file);
    if (it != machine_passphrases_.end()) return it->second;
    missing = true;
    return {};
}

std::string App::openMachineRun(const fs::path& dir, const Machine& machine, const std::string& remote_dir,
                                std::unique_ptr<SshLink> link, std::shared_ptr<LinkStatus> status, double weight) {
    bool missing          = false;
    const auto passphrase = machinePassphrase(machine, missing);
    const auto target     = targetOf(machine, storage_.root(), passphrase);
    const auto attach     = remoteAttachCommand(machine, remote_dir);
    if (!attach)
        return trf("The folder {} cannot be named in a Windows command: leave out quotes, % and '.", remote_dir);

    auto remote = std::make_unique<RemoteRun>();
    if (auto error = remote->attach(dir, std::move(link)); !error.empty()) return error;
    // Connected again after a cut, as long as its process plays there; a little longer after each failure.
    remote->setReconnect([target, command = *attach, status](std::string& gone) -> std::unique_ptr<Link> {
        if (status->gone) {
            gone = status->message();
            if (gone.empty()) gone = trf("The tournament does not play on {} any more.", target.name);
            return nullptr;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now < status->next_try) return nullptr;
        const int failures = std::min(status->failures.load(), 4);
        status->next_try   = now + std::chrono::seconds(std::min(30, 2 << failures));
        status->attached   = false;
        return std::make_unique<SshLink>(target, command, nullptr, status, std::chrono::seconds(3));
    });

    OpenRun open;
    open.run          = std::move(remote);
    open.weight       = weight;
    open.machine_id   = machine.id;
    open.machine_name = machine.name.empty() ? machine.host : machine.name;
    open.link_status  = status;
    open.sync         = std::make_shared<MachineSync>(target, remote_dir, dir);
    // The tab of the same tournament (stopped, looked at) is taken over, with its place.
    if (const int i = findRun(dir); i >= 0 && !runs_[static_cast<std::size_t>(i)].run->running()) {
        const auto index = static_cast<std::size_t>(i);
        open.weight      = runs_[index].weight;
        stashRunView();
        retired_runs_.push_back(std::move(runs_[index].run));  // what this frame drew from it stays valid (#192)
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

std::string App::launchOnMachine(const fs::path& dir, const std::string& machine_id) {
    const auto* machine = machineById(machine_id);
    if (!machine) return TR("The machine chosen in Run on is not in Settings > Machines any more: choose another.");
    const auto name = machine->name.empty() ? machine->host : machine->name;
    if (const auto missing = incompleteMachine(*machine); !missing.empty()) return trf("{}: {}", name, missing);
    if (machine->system.checked.empty()) {
        return trf("Test the connection to {} first (Settings > Machines): it finds out the machine.", name);
    }
    bool no_passphrase = false;
    (void)machinePassphrase(*machine, no_passphrase);
    if (no_passphrase) {
        // Asked now; the start is made again once the connection works.
        passphrase_machine_ = machine->id;
        passphrase_kind_    = MachineJob::Kind::Test;
        open_passphrase_    = true;
        page_               = Page::Settings;
        scroll_to_machines_ = true;
        machine_open_       = machine->id;
        return trf("The key of {} needs its passphrase: type it, then start again.", name);
    }

    // The tournament as the machine plays it.
    tour::TournamentSpec local, there;
    try {
        local = tour::TournamentState::open(dir, true)->spec();
    } catch (const std::exception& e) {
        return trf("Cannot open the tournament: {}", e.what());
    }
    const auto remote_dir = remoteTournamentDir(*machine, dir);
    if (auto error = remoteSpec(local, *machine, engines_, dir, remote_dir, there); !error.empty()) return error;
    if (!writeMachinePlace(dir, {machine->id, name, remote_dir})) {
        return trf("Cannot write {}", (dir / "machine.json").string());
    }

    // Prepared and started there by the link's thread, then its control channel.
    bool missing          = false;
    const auto passphrase = machinePassphrase(*machine, missing);
    const auto target     = targetOf(*machine, storage_.root(), passphrase);
    const auto attach     = remoteAttachCommand(*machine, remote_dir);
    if (!attach)
        return trf("The folder {} cannot be named in a Windows command: leave out quotes, % and '.", remote_dir);
    auto status        = std::make_shared<LinkStatus>();
    const Machine copy = *machine;
    const auto spec    = nlohmann::json(there).dump(4, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
    SshLink::Prepare prepare = [copy, dir, remote_dir, spec](SshClient& client, std::string& error,
                                                             std::vector<std::string>& notes) {
        return startOnMachine(client, copy, dir, remote_dir, spec, error, notes);
    };
    auto link = std::make_unique<SshLink>(target, *attach, std::move(prepare), status, std::chrono::seconds(20));
    Console::instance().add(trf("{}: starting on {} ({}).", local.name, name, remote_dir));
    return openMachineRun(dir, *machine, remote_dir, std::move(link), status, 1.0);
}

bool App::attachOnMachine(const fs::path& dir, double weight) {
    const auto place = readMachinePlace(dir);
    if (!place) return false;
    const auto* machine = machineById(place->machine);
    if (!machine) {
        Console::instance().add(
            trf("{} plays on {}, which is not in Settings > Machines any more.", dir.string(), place->name), true);
        return false;
    }
    bool missing = false;
    (void)machinePassphrase(*machine, missing);
    if (missing) {
        Console::instance().add(trf("{} plays on {}: test the connection there (Settings > Machines) to see it again.",
                                    dir.filename().string(), place->name),
                                true);
        return false;
    }
    const auto attach = remoteAttachCommand(*machine, place->dir);
    if (!attach) return false;
    auto status           = std::make_shared<LinkStatus>();
    const auto passphrase = machinePassphrase(*machine, missing);
    auto link = std::make_unique<SshLink>(targetOf(*machine, storage_.root(), passphrase), *attach, nullptr, status,
                                          std::chrono::seconds(3));
    if (auto error = openMachineRun(dir, *machine, place->dir, std::move(link), status, weight); !error.empty()) {
        Console::instance().add(error, true);
        return false;
    }
    if (const int i = findRun(dir); i >= 0) runs_[static_cast<std::size_t>(i)].found = true;
    Console::instance().add(trf("{} plays on {}: shown again.", run_->info().name, place->name));
    return true;
}

void App::pollMachineRuns() {
    for (auto& open : runs_) {
        if (!open.link_status) continue;
        // What its start said (tablebases missing there), once.
        std::vector<std::string> notes;
        {
            std::lock_guard<std::mutex> lock(open.link_status->mutex);
            notes.swap(open.link_status->notes);
        }
        for (const auto& note : notes) Console::instance().add(note, true, open.run->info().name);
        // Ended there: its files copied back a last time.
        if (open.sync && !open.run->running() && !open.sync_finishing) {
            open.sync->finish();
            open.sync_finishing = true;
        }
    }
}

void App::arbitrateMachines() {
    // Each machine's budget, shared by its tournaments as this computer's is (budget.hpp).
    std::map<std::string, std::vector<std::size_t>> by_machine;
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        if (!runs_[i].machine_id.empty() && runs_[i].run->running()) by_machine[runs_[i].machine_id].push_back(i);
    }
    for (auto& [id, order] : by_machine) {
        const auto* machine = machineById(id);
        if (!machine) continue;
        tour::Budget budget;
        budget.threads   = machine->coreBudget();
        budget.memory_mb = static_cast<uint64_t>(std::max(0, machine->memoryBudget()));
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) { return runs_[a].weight > runs_[b].weight; });
        std::vector<tour::Claim> claims;
        for (const auto i : order) {
            tour::Claim c;
            const auto& run = *runs_[i].run;
            const auto* t   = run.tournament();
            if (!t || run.stopping() || run.paused()) {
                c.active = false;
            } else {
                c.weight  = runs_[i].weight;
                c.game    = tour::largestGame(t->spec());
                c.ceiling = std::max(1, t->spec().conditions.concurrency);
            }
            claims.push_back(c);
        }
        const auto allowances = tour::share(budget, claims);
        for (std::size_t k = 0; k < order.size(); ++k) runs_[order[k]].run->setAllowance(allowances[k]);
    }
}

// ---------------------------------------------------------------------------------------------------------------

void App::drawRunOn(float label) {
    ui::label(TR("Run on"), label);
    const auto* chosen     = setup_.machine.empty() ? nullptr : machineById(setup_.machine);
    const std::string here = TR("This computer");
    const auto current     = chosen                   ? (chosen->name.empty() ? chosen->host : chosen->name)
                             : setup_.machine.empty() ? here
                                                      : std::string(TR("a machine removed"));
    ImGui::SetNextItemWidth(px(260));
    if (ImGui::BeginCombo("##run_on", current.c_str())) {
        if (ImGui::Selectable(here.c_str(), setup_.machine.empty())) {
            setup_.machine.clear();
            saveSetupSoon();
        }
        for (const auto& m : machines_) {
            const auto label = (m.name.empty() ? m.host : m.name) + "##" + m.id;
            if (ImGui::Selectable(label.c_str(), setup_.machine == m.id)) {
                setup_.machine = m.id;
                saveSetupSoon();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", trf("{} · {} cores, {} for tournaments", m.address(), m.system.threads,
                                            formatMemory(static_cast<uint64_t>(m.memoryBudget())))
                                            .c_str());
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", TR("Where the tournament plays: this computer, or a machine of Settings > Machines, "
                                   "followed from here as if it played here. Its engines play there at their paths "
                                   "there; its books and tablebases must be there (Settings > Machines > Books and "
                                   "tablebases)."));
    if (machines_.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Add a machine…"))) {
            page_               = Page::Settings;
            scroll_to_machines_ = true;
            addMachine();
        }
        ImGui::SetItemTooltip("%s", TR("Opens Settings > Machines with a new machine to fill in."));
        ui::topic(guide::topics::Machines);
    }
}

}  // namespace fcg
