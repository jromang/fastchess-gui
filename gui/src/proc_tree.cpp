#include "proc_tree.hpp"

#ifndef _WIN32

#    include <cerrno>
#    include <chrono>
#    include <cstdlib>
#    include <filesystem>
#    include <fstream>
#    include <string>
#    include <thread>

#    include <signal.h>
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <unistd.h>

#    ifdef __linux__
#        include <sys/syscall.h>
#    endif
#    ifdef __APPLE__
#        include <libproc.h>
#        include <sys/proc.h>
#        include <sys/proc_info.h>
#    endif

namespace fcg {

namespace {

// A process that has not ended: running, or stopped (zombies and processes gone are not).
enum class State { Running, Stopped, Ended };

#    ifdef __linux__

// From /proc/PID/stat: "PID (NAME) STATE PPID ...", where NAME may hold spaces and parentheses.
bool readStat(int pid, char& state, int& ppid) {
    std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(in, line)) return false;
    const auto close = line.rfind(')');
    if (close == std::string::npos || close + 4 >= line.size()) return false;
    state = line[close + 2];
    ppid  = std::atoi(line.c_str() + close + 4);
    return true;
}

State state(int pid) {
    char s   = 0;
    int ppid = 0;
    if (!readStat(pid, s, ppid) || s == 'Z' || s == 'X' || s == 'x') return State::Ended;
    return s == 'T' || s == 't' ? State::Stopped : State::Running;
}

#    elif defined(__APPLE__)

State state(int pid) {
    proc_bsdinfo info{};
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, PROC_PIDTBSDINFO_SIZE) != static_cast<int>(PROC_PIDTBSDINFO_SIZE))
        return State::Ended;
    if (info.pbi_status == SZOMB) return State::Ended;
    return info.pbi_status == SSTOP ? State::Stopped : State::Running;
}

#    else

State state(int pid) { return kill(pid, 0) == 0 ? State::Running : State::Ended; }

#    endif

// Until the process stopped (or ended): a signal is delivered when the process next runs. A process in an
// uninterruptible wait may take longer: it is not waited for beyond a moment.
void waitStopped(int pid) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (state(pid) == State::Running && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
}

// A child of ours, not reaped: on Linux through a pidfd, which a signal cannot miss for another process.
class Child {
   public:
    explicit Child(int pid) : pid_(pid) {
#    if defined(__linux__) && defined(SYS_pidfd_open)
        fd_ = static_cast<int>(syscall(SYS_pidfd_open, pid, 0));
#    endif
    }
    ~Child() {
        if (fd_ >= 0) close(fd_);
    }
    Child(const Child&)            = delete;
    Child& operator=(const Child&) = delete;

    // Ours, not reaped (still running, or a zombie), and whether it ended.
    [[nodiscard]] bool ours(bool& ended) const {
        siginfo_t info{};
        int r = -1;
#    if defined(__linux__)
        if (fd_ >= 0)
            r = waitid(static_cast<idtype_t>(3) /* P_PIDFD */, static_cast<id_t>(fd_), &info,
                       WEXITED | WNOHANG | WNOWAIT);
        if (fd_ < 0 || (r == -1 && errno == EINVAL))  // no P_PIDFD before Linux 5.4
#    endif
            r = waitid(P_PID, static_cast<id_t>(pid_), &info, WEXITED | WNOHANG | WNOWAIT);
        // macOS also reports a child that stopped, though only WEXITED is asked: that one has not ended.
        ended = info.si_pid != 0 &&
                (info.si_code == CLD_EXITED || info.si_code == CLD_KILLED || info.si_code == CLD_DUMPED);
        return r == 0;
    }
    // Ours, and still running: what /proc said of its pid meanwhile was about it.
    [[nodiscard]] bool running() const {
        bool ended = false;
        return ours(ended) && !ended;
    }

    void signal(int sig) const {
#    if defined(__linux__) && defined(SYS_pidfd_send_signal)
        if (fd_ >= 0 && syscall(SYS_pidfd_send_signal, fd_, sig, nullptr, 0) == 0) return;
#    endif
        kill(pid_, sig);
    }

   private:
    int pid_;
    int fd_ = -1;
};

}  // namespace

std::vector<int> childProcesses(int pid) {
    std::vector<int> children;
#    ifdef __linux__
    std::error_code ec;
    for (std::filesystem::directory_iterator it("/proc", ec), end; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().string();
        if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos) continue;
        char s   = 0;
        int ppid = 0;
        if (readStat(std::atoi(name.c_str()), s, ppid) && ppid == pid && s != 'Z' && s != 'X')
            children.push_back(std::atoi(name.c_str()));
    }
#    elif defined(__APPLE__)
    std::vector<pid_t> pids(256);
    for (;;) {
        const int bytes = proc_listpids(PROC_PPID_ONLY, static_cast<uint32_t>(pid), pids.data(),
                                        static_cast<int>(pids.size() * sizeof(pid_t)));
        if (bytes <= 0) break;
        const auto n = static_cast<std::size_t>(bytes) / sizeof(pid_t);
        if (n < pids.size()) {
            for (std::size_t i = 0; i < n; ++i)
                if (pids[i] > 0 && state(pids[i]) != State::Ended) children.push_back(pids[i]);
            break;
        }
        pids.resize(pids.size() * 2);  // perhaps more than it holds
    }
#    else
    (void)pid;
#    endif
    return children;
}

bool killProcessTree(int pid) {
    if (pid <= 0) return false;
    const Child leader(pid);
    bool ended = false;
    if (!leader.ours(ended)) return false;  // reaped already, or never ours
    // Ended: what it started was given to init when it ended, and cannot be told from other processes any more.
    if (ended) return true;

    leader.signal(SIGSTOP);
    // Reaped just before it was stopped (without a pidfd, the signal went by its pid): whatever has that pid now
    // goes on.
    if (!leader.ours(ended)) {
        leader.signal(SIGCONT);
        return true;
    }
    waitStopped(pid);
    // The processes found are frozen in turn, parents first. A frozen parent can neither reap its children, whose
    // pids stay theirs (running, or zombies) until they are killed, nor start another one. So only the children of a
    // process seen stopped are looked for; the leader's only if it is still ours and running once they are listed:
    // the pid read was its own then. Bounded, for a process that starts processes without end.
    std::vector<int> tree = {pid};
    for (std::size_t i = 0; i < tree.size() && tree.size() < 4096; ++i) {
        if (state(tree[i]) != State::Stopped) continue;
        const auto children = childProcesses(tree[i]);
        if (i == 0 && !leader.running()) break;
        for (const int child : children) {
            kill(child, SIGSTOP);
            waitStopped(child);
            tree.push_back(child);
        }
    }
    for (std::size_t i = tree.size(); i-- > 1;) kill(tree[i], SIGKILL);
    if (leader.ours(ended)) leader.signal(SIGKILL);
    return true;
}

}  // namespace fcg

#endif
