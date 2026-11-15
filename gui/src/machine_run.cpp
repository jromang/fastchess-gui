#include "machine_run.hpp"

#include "engines.hpp"

#include <algorithm>
#include <fstream>
#include <set>

#include "i18n.hpp"
#include "machine_jobs.hpp"
#include "ssh_tunnel.hpp"
#include "storage.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr const char* PLACE_FILE = "machine.json";

bool isAbsolute(const std::string& path) {
    return (!path.empty() && (path[0] == '/' || path[0] == '\\')) ||
           (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':');
}

// `path` of the machine, absolute: a relative one is in the machine's folder.
std::string inFolder(const Machine& m, const std::string& path, const char* fallback) {
    if (path.empty()) return remoteJoin(machineFolder(m), fallback);
    if (isAbsolute(path)) return path;
    // The machine's own folders default to <folder>/books and <folder>/syzygy: a relative path the user gave is
    // relative to the home folder, as the folder itself.
    if (m.system.home.empty()) return path;
    return remoteJoin(m.system.home, path);
}

std::string firstLine(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '\n' || c == '\r') {
            if (!out.empty()) break;
            continue;
        }
        out += c;
    }
    return printableText(out.substr(0, 300));
}

// A path as the machine's shell writes it.
std::string shellPath(RemoteShell shell, std::string path) {
    if (shell == RemoteShell::Cmd) std::replace(path.begin(), path.end(), '/', '\\');
    return path;
}

// The files of a tournament's folder that are not its own state: they stay where they are.
bool keptHere(const std::string& name) {
    static const std::set<std::string> here = {"tournament.json", "tournament.lock",   "control.json",
                                               PLACE_FILE,        "fastchess-gui.log", "control.sock"};
    return here.count(name) > 0 || name.ends_with(".part");
}

// The files copied back from the machine: those that grow as the tournament plays.
bool copiedBack(const std::string& name) {
    return name == "journal.jsonl" || name.ends_with(".pgn") || name == "processor.tsv" || name == "lichess.json";
}

}  // namespace

std::optional<MachinePlace> readMachinePlace(const fs::path& dir) {
    const auto json = readJson<nlohmann::json>(dir / PLACE_FILE);
    if (!json || !json->is_object()) return std::nullopt;
    MachinePlace p;
    p.machine = json->value("machine", std::string());
    p.name    = json->value("name", std::string());
    p.dir     = json->value("dir", std::string());
    if (p.machine.empty() || p.dir.empty()) return std::nullopt;
    return p;
}

bool writeMachinePlace(const fs::path& dir, const MachinePlace& place) {
    return writeJson(dir / PLACE_FILE, {{"machine", place.machine}, {"name", place.name}, {"dir", place.dir}});
}

std::string machineFolder(const Machine& m) {
    if (isAbsolute(m.folder) || m.system.home.empty()) return m.folder;
    return m.system.shell == RemoteShell::Cmd ? m.system.home + "\\" + m.folder : remoteJoin(m.system.home, m.folder);
}

std::string remoteTournamentDir(const Machine& m, const fs::path& local_dir) {
    auto name = local_dir.filename().string();
    if (name.empty()) name = local_dir.parent_path().filename().string();
    return remoteJoin(remoteJoin(machineFolder(m), "tournaments"), name);
}

std::string remoteSpec(const tour::TournamentSpec& local, const Machine& machine,
                       const std::vector<EngineEntry>& library, const fs::path& local_dir,
                       const std::string& remote_dir, tour::TournamentSpec& out) {
    out               = local;
    const auto books  = inFolder(machine, machine.books_dir, "books");
    const auto syzygy = inFolder(machine, machine.syzygy_dir, "syzygy");
    std::error_code ec;
    const auto here = fs::weakly_canonical(local_dir, ec);
    // A file of the tournament's folder goes in its folder there; any other book in the machine's books folder.
    const auto there = [&](const std::string& file) -> std::string {
        if (file.empty()) return {};
        const fs::path p(file);
        std::error_code e;
        if (fs::weakly_canonical(p.parent_path(), e) == here) return remoteJoin(remote_dir, p.filename().string());
        return remoteJoin(books, p.filename().string());
    };
    for (auto& p : out.participants) {
        const MachineEngine* engine = machine.engine(p.engine.name);
        if (!engine || engine->path.empty()) {
            // The participant's engine by its program: a participant may be named otherwise than its engine.
            for (const auto& e : library) {
                if (!e.cmd.empty() && e.cmd == p.engine.getEnginePath().string()) {
                    if (const auto* found = machine.engine(e.name); found && !found->path.empty()) engine = found;
                }
            }
        }
        if (!engine || engine->path.empty()) {
            if (!p.active) continue;  // withdrawn: it plays no more
            return trf("{} has no path on {}: give it in Settings > Machines > Engines on this machine.", p.engine.name,
                       machine.name);
        }
        p.engine.cmd = engine->path;
        // The engine's folder copied with it is its working directory there, as the folder was here; otherwise it
        // runs from its program's folder.
        p.engine.dir = engine->copied_dir;
        p.program    = {};
        // An engine that runs on this machine plays there as its own: its program, arguments and folder there, no
        // relay. One of another machine cannot (the window refuses such a start first).
        if (p.remote) {
            if (p.remote->machine != machine.id) {
                return trf("{} runs on another machine than {}: run the tournament here, or on its machine.",
                           p.engine.name, machine.name);
            }
            p.engine.cmd  = p.remote->program;
            p.engine.args = joinArguments(p.remote->args);
            if (!p.remote->dir.empty()) p.engine.dir = p.remote->dir;
            p.remote.reset();
        }
        for (auto& [name, value] : p.engine.options) {
            if (name == "SyzygyPath" && !value.empty()) {
                value = syzygy;
            } else {
                value = optionThere(*engine, machine.system.shell, value);  // a file of its folder, in its copy there
            }
        }
        p.book = there(p.book);
    }
    out.conditions.opening.file = there(out.conditions.opening.file);
    out.book                    = {};
    out.polyglot_book           = there(out.polyglot_book);
    if (!out.conditions.tb_adjudication.syzygy_dirs.empty()) out.conditions.tb_adjudication.syzygy_dirs = syzygy;
    for (auto& [name, value] : out.forced_options) {
        if (name == "SyzygyPath" && !value.empty()) value = syzygy;
    }
    out.pgn_out.clear();  // a file of this computer's
    return {};
}

std::vector<std::string> requiredBooks(const tour::TournamentSpec& remote, const std::string& remote_dir) {
    std::vector<std::string> out;
    const auto add = [&](const std::string& file) {
        if (file.empty() || remoteParent(file) == remote_dir) return;
        if (std::find(out.begin(), out.end(), file) == out.end()) out.push_back(file);
    };
    add(remote.conditions.opening.file);
    for (const auto& p : remote.participants) {
        if (p.active) add(p.book);
    }
    return out;
}

std::optional<std::string> remoteStartCommand(const Machine& m, const std::string& remote_dir) {
    const auto shell  = m.system.shell;
    const auto folder = machineFolder(m);
    const auto program =
        shell == RemoteShell::Cmd ? folder + "\\fastchess-gui.exe" : remoteJoin(folder, "fastchess-gui");
    const auto profile = remoteJoin(folder, "profile");
    const auto p = quoteFor(shell, shellPath(shell, program)), pr = quoteFor(shell, shellPath(shell, profile)),
               d = quoteFor(shell, shellPath(shell, remote_dir));
    if (!p || !pr || !d) return std::nullopt;
    const auto line = *p + " --profile " + *pr + " --run " + *d + " --detach";
    return shell == RemoteShell::Cmd ? line : viaSh(line);
}

std::optional<std::string> remoteAttachCommand(const Machine& m, const std::string& remote_dir) {
    const auto shell  = m.system.shell;
    const auto folder = machineFolder(m);
    const auto program =
        shell == RemoteShell::Cmd ? folder + "\\fastchess-gui.exe" : remoteJoin(folder, "fastchess-gui");
    const auto p = quoteFor(shell, shellPath(shell, program)), d = quoteFor(shell, shellPath(shell, remote_dir));
    if (!p || !d) return std::nullopt;
    // exec: the shell steps aside, the channel is the program's own input and output.
    return shell == RemoteShell::Cmd ? *p + " --attach " + *d : viaSh("exec " + *p + " --attach " + *d);
}

bool startOnMachine(SshClient& client, const Machine& machine, const fs::path& local_dir, const std::string& remote_dir,
                    const std::string& spec_json, std::string& error, std::vector<std::string>& notes) {
    const auto name = machine.name.empty() ? machine.host : machine.name;
    tour::TournamentSpec spec;
    try {
        spec = nlohmann::json::parse(spec_json).get<tour::TournamentSpec>();
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    auto shell = machine.system.shell;
    if (shell == RemoteShell::Unknown) {
        shell = detectShell(client, name, error);
        if (shell == RemoteShell::Unknown) return false;
    }
    auto m         = machine;
    m.system.shell = shell;

    // fastchess-gui, in the version that speaks this window's protocol.
    const auto folder = machineFolder(m);
    const auto program =
        shell == RemoteShell::Cmd ? folder + "\\fastchess-gui.exe" : remoteJoin(folder, "fastchess-gui");
    const auto quoted  = quoteFor(shell, shellPath(shell, program));
    const auto version = quoted ? client.run(shell == RemoteShell::Cmd ? *quoted + " --version"
                                                                       : viaSh(*quoted + " --version 2>/dev/null"),
                                             30s)
                                : std::nullopt;
    if (!version && client.error().kind != SshError::Kind::Remote) {
        error = client.error().message;
        return false;
    }
    const auto [installed, protocol] = parseVersion(version ? version->out : std::string());
    if (installed.empty()) {
        error = trf("fastchess-gui is not installed on {}: Install it from Settings > Machines.", name);
        return false;
    }
    if (protocol != REMOTE_PROTOCOL) {
        error =
            trf("fastchess-gui {} on {} cannot run tournaments for this window: Update it from Settings > "
                "Machines.",
                installed, name);
        return false;
    }

    // The engines' programs, and the books: there, or the tournament cannot start.
    for (const auto& p : spec.participants) {
        if (!p.active) continue;
        if (!client.stat(sftpPath(p.engine.cmd))) {
            if (client.error().kind != SshError::Kind::NotFound) {
                error = client.error().message;
                return false;
            }
            error = trf("{} is not on {} ({}): give its path there in Settings > Machines > Engines on this machine.",
                        p.engine.name, name, p.engine.cmd);
            return false;
        }
    }
    for (const auto& book : requiredBooks(spec, remote_dir)) {
        if (!client.stat(sftpPath(book))) {
            if (client.error().kind != SshError::Kind::NotFound) {
                error = client.error().message;
                return false;
            }
            error = trf("Book {} is missing on {}: push it from Settings > Machines > Books and tablebases.",
                        remoteName(book), name);
            return false;
        }
    }
    // The tablebases: without them, the adjudication and the engines do without.
    if (const auto& tb = spec.conditions.tb_adjudication.syzygy_dirs; !tb.empty()) {
        const auto listing = client.list(sftpPath(tb));
        const bool any     = listing && std::any_of(listing->begin(), listing->end(),
                                                    [](const RemoteEntry& e) { return syzygyPieces(e.name) > 0; });
        if (!any) {
            notes.push_back(
                trf("{}: no Syzygy file in {}: the adjudication does without them; push them from "
                    "Settings > Machines > Books and tablebases.",
                    name, tb));
        }
    }

    // The folder, its spec, the files it starts from.
    if (!client.makeDirs(sftpPath(remote_dir)) || !client.makeDirs(sftpPath(remoteJoin(folder, "profile")))) {
        error = client.error().message;
        return false;
    }
    const bool resumed_there = client.stat(sftpPath(remoteJoin(remote_dir, "journal.jsonl"))).has_value();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(local_dir, ec)) {
        const auto file = entry.path().filename().string();
        if (!entry.is_regular_file() || keptHere(file)) continue;
        // The machine's own files are the tournament's: those here are copies of them.
        if (resumed_there && copiedBack(file)) continue;
        if (!client.upload(entry.path(), sftpPath(remoteJoin(remote_dir, file)))) {
            error = client.error().message;
            return false;
        }
    }
    const auto spec_file = local_dir / ".tournament-remote.json";
    {
        std::ofstream out(spec_file, std::ios::binary | std::ios::trunc);
        out << spec_json;
    }
    const bool written = client.upload(spec_file, sftpPath(remoteJoin(remote_dir, "tournament.json")));
    fs::remove(spec_file, ec);
    if (!written) {
        error = client.error().message;
        return false;
    }

    // Started there, out of the SSH session: it plays on when the connection ends.
    const auto command = remoteStartCommand(m, remote_dir);
    if (!command) {
        error = trf("The folder {} cannot be named in a Windows command: leave out quotes, % and '.", remote_dir);
        return false;
    }
    const auto started = client.run(*command, 60s);
    if (!started) {
        error = client.error().message;
        return false;
    }
    if (started->exit_code != 0) {
        error = trf("The tournament did not start on {}: {}", name,
                    firstLine(started->err.empty() ? started->out : started->err));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------

void LinkStatus::set(std::string text, bool is_gone) {
    std::lock_guard<std::mutex> lock(mutex);
    error = std::move(text);
    if (is_gone) gone = true;
}

std::string LinkStatus::message() {
    std::lock_guard<std::mutex> lock(mutex);
    return error;
}

SshLink::SshLink(SshTarget target, std::string attach_command, Prepare prepare, std::shared_ptr<LinkStatus> status,
                 std::chrono::seconds patience)
    : target_(std::move(target)),
      command_(std::move(attach_command)),
      prepare_(std::move(prepare)),
      status_(std::move(status)),
      patience_(patience) {
    thread_ = std::thread([this] {
        try {
            run();
        } catch (const std::exception& e) {
            status_->set(trf("Internal error: {}", e.what()), false);
        }
        end();
    });
}

SshLink::~SshLink() {
    close();
    if (thread_.joinable()) thread_.join();
}

void SshLink::end() {
    std::lock_guard<std::mutex> lock(mutex_);
    ended_ = true;
    cv_.notify_all();
}

long SshLink::read(char* buffer, std::size_t size) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] { return !in_.empty() || ended_ || stop_; });
    if (in_.empty()) return 0;
    const auto n = std::min(size, in_.size());
    std::copy_n(in_.data(), n, buffer);
    in_.erase(0, n);
    return static_cast<long>(n);
}

bool SshLink::write(const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ended_ || stop_) return false;
    out_ += data;
    return true;
}

void SshLink::close() {
    stop_ = true;
    std::lock_guard<std::mutex> lock(mutex_);
    cv_.notify_all();
}

void SshLink::run() {
    SshClient client(&stop_);
    if (!client.connect(target_)) {
        if (stop_) return;
        const auto kind = client.error().kind;
        // A key refused, a host key changed: trying again cannot mend it.
        const bool permanent =
            kind == SshError::Kind::Auth || kind == SshError::Kind::HostKey || kind == SshError::Kind::Passphrase;
        ++status_->failures;
        status_->set(client.error().message, permanent);
        return;
    }
    if (prepare_) {
        std::string error;
        std::vector<std::string> notes;
        const bool ok = prepare_(client, error, notes);
        {
            std::lock_guard<std::mutex> lock(status_->mutex);
            status_->notes.insert(status_->notes.end(), notes.begin(), notes.end());
        }
        if (!ok) {
            if (!stop_) status_->set(error, true);
            return;
        }
    }
    const auto until = Clock::now() + patience_;
    // What the window wrote (its hello first), kept until the process answered: written again to the next try.
    std::string sent;
    while (!stop_) {
        auto channel = client.start(command_);
        if (!channel) {
            ++status_->failures;
            status_->set(client.error().message, false);
            return;
        }
        bool answered = false;
        if (!sent.empty() && !channel->write(sent)) break;
        while (!stop_) {
            std::string data;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                data.swap(out_);
            }
            if (!data.empty()) {
                if (!answered) sent += data;
                if (!channel->write(data)) break;
            }
            const auto chunk = channel->read(30ms);
            if (!chunk) break;
            if (chunk->empty()) continue;
            if (!answered) {
                answered = true;
                sent.clear();
                status_->attached = true;
                status_->failures = 0;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            in_ += *chunk;
            cv_.notify_all();
        }
        if (stop_ || answered) {
            if (!stop_ && !client.connected()) {
                ++status_->failures;
                status_->set(client.error().message, false);
            }
            return;
        }
        // Nothing listens there (yet): the process starting makes its socket in a moment; one that ended does not.
        const auto said = channel->errorOutput();
        channel.reset();
        if (!client.connected()) {
            ++status_->failures;
            status_->set(client.error().message, false);
            return;
        }
        if (Clock::now() < until) {
            std::this_thread::sleep_for(500ms);
            continue;
        }
        status_->set(said.find("no tournament process") != std::string::npos
                         ? trf("The tournament does not play on {} any more.", target_.name)
                         : trf("The tournament cannot be reached on {}: {}", target_.name, firstLine(said)),
                     true);
        return;
    }
}

// ---------------------------------------------------------------------------------------------------------------

MachineSync::MachineSync(SshTarget target, std::string remote_dir, fs::path local_dir)
    : target_(std::move(target)), remote_dir_(std::move(remote_dir)), local_dir_(std::move(local_dir)) {
    thread_ = std::thread([this] {
        try {
            run();
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex_);
            error_ = e.what();
        }
        done_ = true;
    });
}

MachineSync::~MachineSync() {
    stop_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void MachineSync::now() {
    nudge_ = true;
    cv_.notify_all();
}

void MachineSync::finish() {
    finish_ = true;
    cv_.notify_all();
}

std::string MachineSync::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

std::optional<uint64_t> MachineSync::pass(SshClient& client, const std::string& remote_dir, const fs::path& local_dir,
                                          std::string& error) {
    uint64_t copied = 0;
    const auto copy = [&](const std::string& remote, const fs::path& local, uint64_t size) {
        std::error_code ec;
        const auto have = fs::exists(local, ec) ? fs::file_size(local, ec) : 0;
        if (size == have) return true;
        // Grown: the rest; shorter (written afresh there): all of it.
        const auto n = client.fetch(remote, local, size > have ? have : 0);
        if (!n) {
            error = client.error().message;
            return false;
        }
        copied += *n;
        return true;
    };
    const auto listing = client.list(sftpPath(remote_dir));
    if (!listing) {
        error = client.error().message;
        return std::nullopt;
    }
    for (const auto& e : *listing) {
        if (e.directory && e.name == "logs") {
            const auto logs = client.list(sftpPath(remoteJoin(remote_dir, "logs")));
            if (!logs) continue;
            std::error_code ec;
            fs::create_directories(local_dir / "logs", ec);
            for (const auto& log : *logs) {
                if (log.regular && log.name.ends_with(".log") &&
                    !copy(sftpPath(remoteJoin(remoteJoin(remote_dir, "logs"), log.name)), local_dir / "logs" / log.name,
                          log.size)) {
                    return std::nullopt;
                }
            }
            continue;
        }
        if (!e.regular || !copiedBack(e.name)) continue;
        if (!copy(sftpPath(remoteJoin(remote_dir, e.name)), local_dir / e.name, e.size)) return std::nullopt;
    }
    return copied;
}

void MachineSync::run() {
    std::unique_ptr<SshClient> client;
    auto retry_at = Clock::now();
    while (!stop_) {
        const bool last = finish_;
        if (!client || !client->connected()) {
            if (Clock::now() >= retry_at) {
                client = std::make_unique<SshClient>(&stop_);
                if (!client->connect(target_)) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    error_   = client->error().message;
                    retry_at = Clock::now() + 10s;
                }
            }
        }
        if (client && client->connected()) {
            std::string error;
            const bool ok = pass(*client, remote_dir_, local_dir_, error).has_value();
            std::lock_guard<std::mutex> lock(mutex_);
            error_ = ok ? std::string() : error;
        }
        if (last) return;
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, 3s, [&] { return stop_ || finish_ || nudge_; });
        nudge_ = false;
    }
}

}  // namespace fcg
