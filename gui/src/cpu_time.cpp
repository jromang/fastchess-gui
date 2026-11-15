#include "cpu_time.hpp"

#include <algorithm>
#include <set>

#ifdef _WIN32
#    include <windows.h>
// windows.h first
#    include <tlhelp32.h>
#elif defined(__APPLE__)
#    include <libproc.h>
#    include <mach/kern_return.h>
#    include <mach/mach_time.h>
#    include <sys/proc_info.h>

#    include "proc_tree.hpp"
#else
#    include <unistd.h>

#    include <filesystem>
#    include <fstream>
#    include <sstream>
#    include <string>
#endif

namespace fcg {

std::vector<int64_t> descendants(int64_t pid, const std::vector<std::pair<int64_t, int64_t>>& parents) {
    std::vector<int64_t> out;
    if (pid <= 0) return out;
    out.push_back(pid);
    std::set<int64_t> seen{pid};
    for (std::size_t i = 0; i < out.size() && out.size() < MAX_PROCESS_TREE; ++i) {
        for (const auto& [p, parent] : parents) {
            if (parent == out[i] && p > 0 && seen.insert(p).second) out.push_back(p);
            if (out.size() >= MAX_PROCESS_TREE) break;
        }
    }
    return out;
}

#ifdef _WIN32

std::vector<int64_t> processTree(int64_t pid) {
    std::vector<int64_t> out;
    if (pid <= 0) return out;
    out.push_back(pid);

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return out;
    std::vector<std::pair<int64_t, int64_t>> all;  // (pid, parent)
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32First(snapshot, &entry); more; more = Process32Next(snapshot, &entry)) {
        all.emplace_back(entry.th32ProcessID, entry.th32ParentProcessID);
    }
    CloseHandle(snapshot);
    return descendants(pid, all);
}

int64_t cpuTimeMs(const std::vector<int64_t>& pids) {
    int64_t total = 0;
    bool any      = false;
    for (const auto pid : pids) {
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
        if (!process) continue;
        FILETIME created, exited, kernel, user;
        if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
            const auto ticks = [](const FILETIME& t) {
                return (static_cast<int64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
            };
            total += (ticks(kernel) + ticks(user)) / 10'000;  // 100 ns units
            any = true;
        }
        CloseHandle(process);
    }
    return any ? total : -1;
}

#elif defined(__APPLE__)

std::vector<int64_t> processTree(int64_t pid) {
    std::vector<int64_t> out;
    if (pid <= 0) return out;
    out.push_back(pid);
    // Bounded, for a process that starts processes without end; each once.
    std::set<int64_t> seen{pid};
    for (std::size_t i = 0; i < out.size() && out.size() < MAX_PROCESS_TREE; ++i) {
        for (const int child : childProcesses(static_cast<int>(out[i]))) {
            if (seen.insert(child).second) out.push_back(child);
        }
    }
    return out;
}

int64_t cpuTimeMs(const std::vector<int64_t>& pids) {
    // The times are in Mach absolute time units: nanoseconds on Intel, not on Apple silicon.
    static const mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t t{};
        if (mach_timebase_info(&t) != KERN_SUCCESS || t.numer == 0 || t.denom == 0) t = {1, 1};
        return t;
    }();
    uint64_t units = 0;
    bool any       = false;
    for (const auto pid : pids) {
        proc_taskinfo info{};
        if (proc_pidinfo(static_cast<int>(pid), PROC_PIDTASKINFO, 0, &info, sizeof(info)) !=
            static_cast<int>(sizeof(info)))
            continue;
        units += info.pti_total_user + info.pti_total_system;
        any = true;
    }
    return any ? static_cast<int64_t>(units * timebase.numer / timebase.denom / 1'000'000) : -1;
}

#else

namespace {

// The children of every thread of a process (/proc/<pid>/task/<tid>/children), and theirs.
void children(int64_t pid, std::vector<int64_t>& out) {
    std::error_code ec;
    for (const auto& task : std::filesystem::directory_iterator("/proc/" + std::to_string(pid) + "/task", ec)) {
        std::ifstream list(task.path() / "children");
        for (int64_t child; list >> child;) {
            if (out.size() >= MAX_PROCESS_TREE || std::find(out.begin(), out.end(), child) != out.end()) continue;
            out.push_back(child);
            children(child, out);
        }
    }
}

}  // namespace

std::vector<int64_t> processTree(int64_t pid) {
    std::vector<int64_t> out;
    if (pid <= 0) return out;
    out.push_back(pid);
    children(pid, out);
    return out;
}

int64_t cpuTimeMs(const std::vector<int64_t>& pids) {
    static const long ticks_per_second = sysconf(_SC_CLK_TCK);
    int64_t ticks                      = 0;
    bool any                           = false;
    for (const auto pid : pids) {
        std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
        std::string stat;
        if (!std::getline(in, stat)) continue;
        // pid (comm) state ppid ... utime stime: fields 14 and 15, counted after the command name, which may hold
        // spaces and parentheses.
        const auto close = stat.rfind(')');
        if (close == std::string::npos) continue;
        std::istringstream fields(stat.substr(close + 2));
        std::string field;
        int64_t utime = 0, stime = 0;
        for (int i = 3; i <= 15 && fields >> field; ++i) {
            if (i == 14) utime = std::stoll(field);
            if (i == 15) stime = std::stoll(field);
        }
        ticks += utime + stime;
        any = true;
    }
    return any && ticks_per_second > 0 ? ticks * 1000 / ticks_per_second : -1;
}

#endif

}  // namespace fcg
