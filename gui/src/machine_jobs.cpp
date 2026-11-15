#include "machine_jobs.hpp"

#include <algorithm>
#include <ctime>
#include <set>

#include "i18n.hpp"
#include "memory.hpp"
#include "ssh_tunnel.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

std::string nowText() {
    const auto t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M", &tm);
    return buffer;
}

std::string firstLine(const std::string& text, std::size_t max = 200) {
    std::string out;
    for (const char c : text) {
        if (c == '\n' || c == '\r') {
            if (!out.empty()) break;
            continue;
        }
        out += c;
    }
    if (out.size() > max) out = out.substr(0, max) + "…";
    return printableText(out);
}

// A path of the machine as its shell writes it: backslashes on Windows.
std::string shellPath(RemoteShell shell, std::string path) {
    if (shell == RemoteShell::Cmd) std::replace(path.begin(), path.end(), '/', '\\');
    return path;
}

std::string machineName(const Machine& m) { return m.name.empty() ? m.host : m.name; }

// The machine's shell, from its last test, or asked now.
RemoteShell shellOf(SshClient& client, const Machine& machine, JobResult& result) {
    if (machine.system.shell != RemoteShell::Unknown) return machine.system.shell;
    std::string error;
    const auto shell = detectShell(client, machineName(machine), error);
    if (shell == RemoteShell::Unknown) result.error = {SshError::Kind::Remote, error, {}};
    return shell;
}

// A copy's progress for JobState: `base` bytes copied before this file; the pace counts only what this job sends.
SshClient::Progress copyProgress(JobState& state, uint64_t base) {
    auto last = std::make_shared<std::optional<uint64_t>>();
    return [&state, base, last](uint64_t done, uint64_t) {
        if (*last && done > **last) state.bytes_sent += done - **last;
        *last            = done;
        state.bytes_done = base + done;
        return !state.cancel.load();
    };
}

}  // namespace

bool FilesCheck::booksMissing() const {
    return std::any_of(books.begin(), books.end(),
                       [](const BookCheck& b) { return b.state != BookCheck::State::Same; });
}

uint64_t FilesCheck::bytesToPush() const {
    uint64_t bytes = syzygy.bytes;
    for (const auto& b : books) {
        if (b.state == BookCheck::State::Missing || b.state == BookCheck::State::Different) bytes += b.size;
    }
    return bytes;
}

void JobState::setStep(std::string step) {
    std::lock_guard<std::mutex> lock(mutex_);
    step_ = std::move(step);
}

std::string JobState::step() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return step_;
}

MachineJob::MachineJob(Kind kind, SshTarget target, Work work) : kind_(kind), shared_(std::make_shared<Shared>()) {
    thread_ = std::thread([shared = shared_, target = std::move(target), work = std::move(work)] {
        auto& result = shared->result;
        try {
            SshClient client(&shared->state.cancel);
            shared->state.setStep(trf("Connecting to {}…", target.name));
            if (!client.connect(target)) {
                result.error    = client.error();
                result.host_key = client.hostKey();
            } else {
                result.host_key     = client.hostKey();
                result.host_key_new = client.hostKeyNew();
                work(client, shared->state, result);
                // The job's own failure, unless it set one.
                if (!result.ok && !result.error && client.error()) result.error = client.error();
            }
        } catch (const std::exception& e) {
            result.ok    = false;
            result.error = {SshError::Kind::Local, trf("Internal error: {}", e.what()), {}};
        } catch (...) {
            result.ok    = false;
            result.error = {SshError::Kind::Local, TR("Internal error."), {}};
        }
        shared->finished = true;
    });
}

MachineJob::~MachineJob() {
    cancel();
    // A connection under way gives up at once, unless it is still finding the host's address, which nothing cuts
    // short: not waited for then (the thread owns what it uses).
    const auto until = Clock::now() + 2s;
    while (!shared_->finished && Clock::now() < until) std::this_thread::sleep_for(10ms);
    if (shared_->finished) {
        thread_.join();
    } else {
        thread_.detach();
    }
}

JobResult MachineJob::take() { return std::move(shared_->result); }

std::optional<std::chrono::seconds> MachineJob::remaining() const {
    const auto& s     = shared_->state;
    const auto sent   = s.bytes_sent.load();
    const auto total  = s.bytes_total.load();
    const auto done   = s.bytes_done.load();
    const auto passed = std::chrono::duration<double>(Clock::now() - s.started).count();
    if (sent < (1u << 20) || passed < 2 || done >= total) return std::nullopt;
    const double rate = static_cast<double>(sent) / passed;
    return std::chrono::seconds(static_cast<int64_t>(static_cast<double>(total - done) / rate));
}

// ---------------------------------------------------------------------------------------------------------------

RemoteShell detectShell(SshClient& client, const std::string& name, std::string& error) {
    // cmd expands %OS%; a POSIX shell, fish or csh print it as it is, and have uname.
    const auto echo = client.run("echo %OS%", 15s);
    if (!echo) {
        error = client.error().message;
        return RemoteShell::Unknown;
    }
    if (shellFromEcho(echo->out) == RemoteShell::Cmd) return RemoteShell::Cmd;
    const auto uname = client.run(viaSh("uname -s"), 15s);
    if (uname && uname->exit_code == 0 && !firstLine(uname->out).empty()) return RemoteShell::Posix;
    if (!uname && client.error().kind != SshError::Kind::Remote) {
        error = client.error().message;
        return RemoteShell::Unknown;
    }
    error =
        trf("{} runs commands with a shell fastchess-gui does not speak (PowerShell?): on Windows, make cmd the SSH "
            "server's shell again (remove the value DefaultShell of HKEY_LOCAL_MACHINE\\SOFTWARE\\OpenSSH).",
            name);
    return RemoteShell::Unknown;
}

void testMachine(SshClient& client, const Machine& machine, JobState& state, JobResult& result) {
    const auto name = machineName(machine);
    result.lines.push_back(
        client.hostKeyNew()
            ? trf("Connected. Host key {}: seen for the first time, trusted from now on.", client.hostKey())
            : trf("Connected. Host key {}, as before.", client.hostKey()));
    state.setStep(TR("Finding out the system…"));
    std::string error;
    RemoteSystem system;
    system.shell = detectShell(client, name, error);
    if (system.shell == RemoteShell::Unknown) {
        result.error = {SshError::Kind::Remote, error, {}};
        return;
    }
    const auto probe = system.shell == RemoteShell::Cmd ? windowsProbe(machine.folder)
                                                        : std::optional<std::string>(posixProbe(machine.folder));
    if (!probe) {
        result.error = {
            SshError::Kind::Remote,
            trf("The folder {} cannot be named in a Windows command: leave out quotes, % and '.", machine.folder),
            {}};
        return;
    }
    const auto out = client.run(*probe, 60s);
    if (!out) {
        result.error = client.error();
        return;
    }
    if (system.shell == RemoteShell::Cmd) {
        readWindowsProbe(out->out, system);
    } else {
        readPosixProbe(out->out, system);
    }
    const auto [version, protocol] = parseVersion(out->out);
    system.fcg_version             = version;
    system.fcg_protocol            = protocol;
    system.checked                 = nowText();
    if (system.os.empty() || system.threads <= 0) {
        result.error = {SshError::Kind::Remote,
                        trf("{} did not tell its system and processors: {}", name, firstLine(out->out + out->err)),
                        {}};
        return;
    }

    std::string os = system.os + " " + system.arch;
    if (!system.release.empty()) os += " (" + system.release + ")";
    result.lines.push_back(trf("{}, {} threads, {} of memory.", os, system.threads, formatMemory(system.memory_mb)));
    if (system.disk_free_mb > 0) {
        result.lines.push_back(
            trf("{} free where its folder {} is.", formatMemory(system.disk_free_mb), machine.folder));
    }
    if (version.empty()) {
        result.lines.push_back(trf("fastchess-gui is not in {}: Install puts it there.", machine.folder));
    } else if (protocol != REMOTE_PROTOCOL) {
        result.lines.push_back(
            trf("fastchess-gui {} is in {}, too old to run tournaments for this window: Update "
                "replaces it.",
                version, machine.folder));
    } else {
        result.lines.push_back(trf("fastchess-gui {} is in {} (protocol {}).", version, machine.folder, protocol));
    }
    result.system = system;
    result.ok     = true;
}

void installProgram(SshClient& client, const Machine& machine, const fs::path& program, JobState& state,
                    JobResult& result) {
    auto m         = machine;
    m.system.shell = shellOf(client, machine, result);
    if (m.system.shell == RemoteShell::Unknown) return;
    const bool windows = m.system.shell == RemoteShell::Cmd;
    const auto dir     = sftpPath(m.folder);
    const auto remote  = remoteJoin(dir, windows ? "fastchess-gui.exe" : "fastchess-gui");
    state.setStep(trf("Making the folder {}…", m.folder));
    if (!client.makeDirs(dir)) {
        result.error = client.error();
        return;
    }
    // A program running there (a tournament's) cannot be replaced on Windows, but it can be renamed.
    if (client.stat(remote)) client.rename(remote, remote + ".old");
    std::error_code ec;
    const auto size   = fs::file_size(program, ec);
    state.bytes_total = size;
    state.setStep(trf("Copying {} ({})…", program.filename().string(), formatBytes(size)));
    if (!client.upload(program, remote, copyProgress(state, 0), 0755)) {
        result.error = client.error();
        return;
    }
    client.remove(remote + ".old");
    if (!windows) client.setPermissions(remote, 0755);

    state.setStep(TR("Checking the program on the machine…"));
    const auto path    = m.programPath();
    const auto quoted  = quoteFor(m.system.shell, path);
    const auto command = windows ? *quoted + " --version" : viaSh(*quoted + " --version");
    const auto out     = client.run(command, 30s);
    if (!out) {
        result.error = client.error();
        return;
    }
    const auto [version, protocol] = parseVersion(out->out);
    if (version.empty()) {
        result.error = {SshError::Kind::Remote,
                        trf("fastchess-gui was copied to {}, but it does not start there: {}", machineName(m),
                            firstLine(out->err.empty() ? out->out : out->err)),
                        {}};
        return;
    }
    m.system.fcg_version  = version;
    m.system.fcg_protocol = protocol;
    result.system         = m.system;
    result.lines.push_back(trf("fastchess-gui {} installed in {} (protocol {}).", version, m.folder, protocol));
    result.ok = true;
}

namespace {

// The engine's answer to uci (or xboard): its name, or why it did not answer.
MachineEngine checkEngine(SshClient& client, const std::string& machine, RemoteShell shell, MachineEngine e,
                          const std::string& protocol, std::chrono::milliseconds limit) {
    e.check_ok = 0;
    e.id_name.clear();
    const auto st = client.stat(sftpPath(e.path));
    if (!st) {
        e.check_message = client.error().kind == SshError::Kind::NotFound
                              ? trf("{} is not on {}: give the program's path there.", e.path, machine)
                              : client.error().message;
        return e;
    }
    if (st->directory) {
        e.check_message = trf("{} is a folder: give the engine's program in it.", e.path);
        return e;
    }
    if (shell == RemoteShell::Posix && st->perms != 0 && (st->perms & 0111) == 0) {
        e.check_message = trf("{} cannot be run on {}: make it executable there (chmod +x).", e.path, machine);
        return e;
    }
    const auto command = startCommand(shell, shellPath(shell, e.path));
    if (!command) {
        e.check_message = trf("{} cannot be named in a Windows command: leave out quotes and %.", e.path);
        return e;
    }
    auto channel = client.start(*command);
    if (!channel) {
        e.check_message = client.error().message;
        return e;
    }
    const bool xboard = protocol == "xboard";
    channel->write(xboard ? "xboard\nprotover 2\n" : "uci\n");
    std::string text;
    bool answered    = false;
    const auto until = Clock::now() + limit;
    while (!answered && Clock::now() < until) {
        const auto chunk = channel->read(200ms);
        if (!chunk) break;
        text += *chunk;
        if (text.size() > (1u << 20)) break;
        answered = xboard ? text.find("done=1") != std::string::npos
                          : text.find("\nuciok") != std::string::npos || text.rfind("uciok", 0) == 0;
    }
    const auto err = channel->errorOutput();
    channel->write("quit\n");
    channel->closeInput();
    channel.reset();

    // Its name: "id name X" (UCI), myname="X" (xboard).
    if (xboard) {
        if (const auto at = text.find("myname=\""); at != std::string::npos) {
            const auto end = text.find('"', at + 8);
            if (end != std::string::npos) e.id_name = printableText(text.substr(at + 8, end - at - 8));
        }
    } else if (const auto at = text.find("id name "); at != std::string::npos) {
        e.id_name = firstLine(text.substr(at + 8));
    }
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(limit).count();
    if (answered) {
        e.check_ok      = 1;
        e.check_message = e.id_name.empty() ? trf("Answers {}.", xboard ? "xboard" : "uci")
                                            : trf("Answers {} as {}.", xboard ? "xboard" : "uci", e.id_name);
    } else if (!err.empty()) {
        e.check_message = trf("{} does not start on {}: {}", e.path, machine, firstLine(err));
    } else if (text.empty()) {
        e.check_message = trf("{} said nothing within {} s: is it an engine for {}'s system, and a {} engine?", e.path,
                              seconds, machine, xboard ? "WinBoard" : "UCI");
    } else {
        e.check_message = trf("{} did not answer {} within {} s; it printed: {}", e.path, xboard ? "xboard" : "uci",
                              seconds, firstLine(text));
    }
    return e;
}

}  // namespace

void checkEngines(SshClient& client, const Machine& machine, const std::map<std::string, std::string>& protocols,
                  std::chrono::milliseconds limit, JobState& state, JobResult& result) {
    const auto shell = shellOf(client, machine, result);
    if (shell == RemoteShell::Unknown) return;
    int failed = 0;
    for (const auto& e : machine.engines) {
        if (state.cancel) {
            result.error = {SshError::Kind::Cancelled, TR("Cancelled."), {}};
            return;
        }
        if (e.path.empty()) {
            result.engines.push_back(e);
            continue;
        }
        state.setStep(trf("Checking {}…", e.engine));
        const auto it = protocols.find(e.engine);
        auto checked  = checkEngine(client, machineName(machine), shell, e,
                                    it == protocols.end() ? std::string("uci") : it->second, limit);
        if (checked.check_ok != 1) ++failed;
        result.lines.push_back(e.engine + ": " + checked.check_message);
        result.engines.push_back(std::move(checked));
        if (!client.connected()) {
            result.error = client.error();
            return;
        }
    }
    result.ok = failed == 0;
    if (failed > 0) {
        result.error = {
            SshError::Kind::Remote,
            trn("{} engine does not run on {}.", "{} engines do not run on {}.", failed, failed, machineName(machine)),
            {}};
    }
}

void checkFiles(SshClient& client, const Machine& machine, const FilesRequest& request, JobState& state,
                JobResult& result) {
    const auto name = machineName(machine);
    FilesCheck check;
    check.syzygy_dir  = machine.syzygyDir();
    RemoteShell shell = RemoteShell::Unknown;
    for (const auto& book : request.books) {
        if (state.cancel) {
            result.error = {SshError::Kind::Cancelled, TR("Cancelled."), {}};
            return;
        }
        BookCheck b;
        b.local  = book;
        b.remote = remoteJoin(machine.booksDir(), book.filename().string());
        std::error_code ec;
        b.size = fs::file_size(book, ec);
        if (ec) {
            b.state = BookCheck::State::Unreadable;
            check.books.push_back(b);
            continue;
        }
        state.setStep(trf("Looking for {} on {}…", book.filename().string(), name));
        const auto there = client.stat(sftpPath(b.remote));
        if (!there) {
            if (client.error().kind != SshError::Kind::NotFound) {
                result.error = client.error();
                return;
            }
            b.state = BookCheck::State::Missing;
        } else if (there->size != b.size) {
            b.state = BookCheck::State::Different;
        } else {
            // The same size: the same content? A book of the same name may be another one.
            if (shell == RemoteShell::Unknown) shell = shellOf(client, machine, result);
            if (shell == RemoteShell::Unknown) return;
            state.setStep(trf("Comparing {} here and on {}…", book.filename().string(), name));
            const auto mine    = sha256File(book, &state.cancel);
            const auto command = hashCommand(shell, shellPath(shell, b.remote));
            const auto out     = command ? client.run(*command, std::chrono::minutes(10)) : std::nullopt;
            if (!out && client.error().kind != SshError::Kind::Remote) {
                result.error = client.error();
                return;
            }
            const auto theirs = out ? readHash(out->out) : std::string();
            b.state           = !mine.empty() && mine == theirs ? BookCheck::State::Same : BookCheck::State::Different;
        }
        check.books.push_back(b);
    }

    const auto local   = localSyzygyFiles(request.syzygy_paths, request.syzygy_pieces);
    check.syzygy_local = local.size();
    if (!local.empty()) {
        state.setStep(trf("Listing the tablebases of {}…", name));
        const auto listing = client.list(sftpPath(check.syzygy_dir));
        if (!listing && client.error().kind != SshError::Kind::NotFound) {
            result.error = client.error();
            return;
        }
        check.syzygy = planSync(
            local, listing ? remoteSyzygyFiles(*listing, request.syzygy_pieces) : std::vector<SyncFile>{}, false);
    }

    for (const auto& b : check.books) {
        const auto file = b.local.filename().string();
        switch (b.state) {
            case BookCheck::State::Same:
                result.lines.push_back(trf("Book {}: on {}, the same.", file, name));
                break;
            case BookCheck::State::Missing:
                result.lines.push_back(trf("Book {} missing on {} ({}).", file, name, formatBytes(b.size)));
                break;
            case BookCheck::State::Different:
                result.lines.push_back(
                    trf("Book {}: {} has another file of that name ({} to copy).", file, name, formatBytes(b.size)));
                break;
            case BookCheck::State::Unreadable:
                result.lines.push_back(trf("Book {} cannot be read on this computer.", b.local.string()));
                break;
        }
    }
    if (check.syzygy_local > 0) {
        const auto lacking = check.syzygy.missing.size() + check.syzygy.different.size();
        if (lacking == 0) {
            result.lines.push_back(trn("Syzygy: the {} file is on {}.", "Syzygy: the {} files are on {}.",
                                       check.syzygy_local, check.syzygy_local, name));
        } else {
            result.lines.push_back(trn("Syzygy: {} file missing on {} ({}).", "Syzygy: {} files missing on {} ({}).",
                                       lacking, lacking, name, formatBytes(check.syzygy.bytes)));
        }
    }
    result.files = std::move(check);
    result.ok    = true;
}

void pushFiles(SshClient& client, const Machine& machine, const FilesCheck& check, JobState& state, JobResult& result) {
    const auto name   = machineName(machine);
    state.bytes_total = check.bytesToPush();
    uint64_t base     = 0;
    bool made_books = false, made_syzygy = false;
    RemoteShell shell = RemoteShell::Unknown;
    for (const auto& b : check.books) {
        if (b.state != BookCheck::State::Missing && b.state != BookCheck::State::Different) continue;
        if (!made_books) {
            if (!client.makeDirs(sftpPath(machine.booksDir()))) {
                result.error = client.error();
                return;
            }
            made_books = true;
        }
        const auto file = b.local.filename().string();
        state.setStep(trf("Copying {} to {}…", file, name));
        if (!client.upload(b.local, sftpPath(b.remote), copyProgress(state, base))) {
            result.error = client.error();
            return;
        }
        base += b.size;
        // The copy's hash against the book's: what is read there is what is here.
        if (shell == RemoteShell::Unknown) shell = shellOf(client, machine, result);
        if (shell == RemoteShell::Unknown) return;
        state.setStep(trf("Checking the copy of {}…", file));
        const auto command = hashCommand(shell, shellPath(shell, b.remote));
        const auto out     = command ? client.run(*command, std::chrono::minutes(10)) : std::nullopt;
        const auto mine    = sha256File(b.local, &state.cancel);
        if (!out || readHash(out->out) != mine || mine.empty()) {
            result.error = {SshError::Kind::Remote,
                            trf("The copy of {} on {} is not the same as here: copy it again.", file, name),
                            {}};
            return;
        }
        result.lines.push_back(trf("Book {} copied to {}.", file, name));
    }
    std::vector<const SyncFile*> tables;
    for (const auto& f : check.syzygy.missing) tables.push_back(&f);
    for (const auto& f : check.syzygy.different) tables.push_back(&f);
    for (const auto* f : tables) {
        if (!made_syzygy) {
            if (!client.makeDirs(sftpPath(check.syzygy_dir))) {
                result.error = client.error();
                return;
            }
            made_syzygy = true;
        }
        state.setStep(trf("Copying {} to {}…", f->name, name));
        // upload() checks the copy's size before it takes its name: what a Syzygy file is compared by.
        if (!client.upload(f->local, sftpPath(remoteJoin(check.syzygy_dir, f->name)), copyProgress(state, base))) {
            result.error = client.error();
            return;
        }
        base += f->size;
    }
    if (!tables.empty()) {
        result.lines.push_back(
            trn("{} Syzygy file copied to {}.", "{} Syzygy files copied to {}.", tables.size(), tables.size(), name));
    }
    result.ok = true;
}

}  // namespace fcg
