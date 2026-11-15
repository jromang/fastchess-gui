#include "child_process.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <signal.h>
#    include <spawn.h>
#    include <sys/socket.h>
#    include <sys/wait.h>
#    include <unistd.h>
extern char** environ;
#endif

namespace fcg {

#ifdef _WIN32

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// An argument quoted as CommandLineToArgvW reads it back.
std::wstring quoted(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out    = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"')
            out.append(slashes * 2 + 1, L'\\');
        else
            out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}

}  // namespace

ChildProcess::~ChildProcess() {
    closeInput();
    closeOutput();
    if (process_) CloseHandle(process_);
}

bool ChildProcess::start(const std::string& program, const std::vector<std::string>& args, std::string& error,
                         bool detached) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE in_read = nullptr, in_write = nullptr, out_read = nullptr, out_write = nullptr;
    if (!CreatePipe(&in_read, &in_write, &sa, 0) || !CreatePipe(&out_read, &out_write, &sa, 0)) {
        error = "cannot create a pipe";
        return false;
    }
    // Our ends stay ours: no other program started meanwhile inherits them.
    SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = quoted(wide(program));
    for (const auto& a : args) command += L" " + quoted(wide(a));

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = in_read;
    si.hStdOutput = out_write;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
    // Detached: no console of ours, a process group of its own, and out of this program's job when allowed.
    flags |= detached ? DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB : CREATE_NO_WINDOW;
    BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si, &pi);
    if (!ok && detached) {
        // Not allowed to leave this program's job: started inside it all the same.
        flags &= ~static_cast<DWORD>(CREATE_BREAKAWAY_FROM_JOB);
        ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si, &pi);
    }
    CloseHandle(in_read);
    CloseHandle(out_write);
    if (!ok) {
        CloseHandle(in_write);
        CloseHandle(out_read);
        error = "cannot start " + program + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    pid_     = static_cast<int64_t>(pi.dwProcessId);
    in_      = in_write;
    out_     = out_read;
    return true;
}

bool ChildProcess::write(const std::string& data) {
    if (!in_) return false;
    const char* p    = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        DWORD n = 0;
        if (!WriteFile(in_, p, static_cast<DWORD>(std::min<std::size_t>(left, 1 << 20)), &n, nullptr) || n == 0)
            return false;
        p += n;
        left -= n;
    }
    return true;
}

long ChildProcess::read(char* buffer, std::size_t size) {
    if (!out_) return -1;
    DWORD n = 0;
    if (!ReadFile(out_, buffer, static_cast<DWORD>(size), &n, nullptr)) {
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
    }
    return static_cast<long>(n);
}

void ChildProcess::closeInput() {
    if (in_) CloseHandle(in_);
    in_ = nullptr;
}

void ChildProcess::closeOutput() {
    if (out_) {
        CancelIoEx(out_, nullptr);
        CloseHandle(out_);
    }
    out_ = nullptr;
}

bool ChildProcess::running() {
    if (!process_ || exit_code_) return false;
    if (WaitForSingleObject(process_, 0) != WAIT_OBJECT_0) return true;
    DWORD code = 0;
    exit_code_ = GetExitCodeProcess(process_, &code) ? static_cast<int>(code) : -1;
    return false;
}

int ChildProcess::wait() {
    if (exit_code_) return *exit_code_;
    if (!process_) return -1;
    WaitForSingleObject(process_, INFINITE);
    DWORD code = 0;
    exit_code_ = GetExitCodeProcess(process_, &code) ? static_cast<int>(code) : -1;
    return *exit_code_;
}

void ChildProcess::kill() {
    if (process_ && running()) TerminateProcess(process_, 1);
}

#else

ChildProcess::~ChildProcess() {
    closeInput();
    closeOutput();
    // Reaped, so that it is not left a zombie; a detached one goes on, reaped by the system once this program ends.
    if (pid_ > 0 && !exit_code_) (void)::waitpid(static_cast<pid_t>(pid_), nullptr, WNOHANG);
}

namespace {

// A pipe whose ends no other program started meanwhile inherits (macOS has no pipe2()).
int closingPipe(int fds[2]) {
#    ifdef __APPLE__
    if (::pipe(fds) != 0) return -1;
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return 0;
#    else
    return ::pipe2(fds, O_CLOEXEC);
#    endif
}

}  // namespace

bool ChildProcess::start(const std::string& program, const std::vector<std::string>& args, std::string& error,
                         bool detached) {
    int in[2], out[2];
    if (closingPipe(in) != 0) {
        error = std::string("cannot create a pipe: ") + std::strerror(errno);
        return false;
    }
    if (closingPipe(out) != 0) {
        error = std::string("cannot create a pipe: ") + std::strerror(errno);
        ::close(in[0]);
        ::close(in[1]);
        return false;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    sigset_t none, defaults;
    sigemptyset(&none);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);  // ignored here; its default there, but the program ignores it again
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGTERM);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setsigdefault(&attr, &defaults);
#    ifdef POSIX_SPAWN_SETSID
    if (detached) flags |= POSIX_SPAWN_SETSID;
#    else
    // A process group of its own at least: Ctrl+C in the terminal does not reach it.
    if (detached) {
        flags |= POSIX_SPAWN_SETPGROUP;
        posix_spawnattr_setpgroup(&attr, 0);
    }
#    endif
    posix_spawnattr_setflags(&attr, flags);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid      = 0;
    const int code = ::posix_spawn(&pid, program.c_str(), &actions, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    ::close(in[0]);
    ::close(out[1]);
    if (code != 0) {
        ::close(in[1]);
        ::close(out[0]);
        error = "cannot start " + program + ": " + std::strerror(code);
        return false;
    }
    pid_ = pid;
    in_  = in[1];
    out_ = out[0];
    return true;
}

bool ChildProcess::write(const std::string& data) {
    if (in_ < 0) return false;
    const char* p    = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        const auto n = ::write(in_, p, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        left -= static_cast<std::size_t>(n);
    }
    return true;
}

long ChildProcess::read(char* buffer, std::size_t size) {
    for (;;) {
        if (out_ < 0) return -1;
        const auto n = ::read(out_, buffer, size);
        if (n < 0 && errno == EINTR) continue;
        return static_cast<long>(n);
    }
}

void ChildProcess::closeInput() {
    if (in_ >= 0) ::close(in_);
    in_ = -1;
}

void ChildProcess::closeOutput() {
    if (out_ >= 0) {
        // A read() waiting on another thread returns.
        ::shutdown(out_, SHUT_RDWR);
        ::close(out_);
    }
    out_ = -1;
}

bool ChildProcess::running() {
    if (pid_ <= 0 || exit_code_) return false;
    int status       = 0;
    const pid_t done = ::waitpid(static_cast<pid_t>(pid_), &status, WNOHANG);
    if (done == 0) return true;
    exit_code_ = done > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return false;
}

int ChildProcess::wait() {
    if (exit_code_) return *exit_code_;
    if (pid_ <= 0) return -1;
    int status = 0;
    pid_t done = 0;
    do {
        done = ::waitpid(static_cast<pid_t>(pid_), &status, 0);
    } while (done < 0 && errno == EINTR);
    exit_code_ = done > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return *exit_code_;
}

void ChildProcess::kill() {
    if (running()) ::kill(static_cast<pid_t>(pid_), SIGKILL);
}

#endif

}  // namespace fcg
