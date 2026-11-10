#include "https.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <optional>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <spawn.h>
#    include <sys/wait.h>
#    include <unistd.h>
extern char** environ;
#endif

#include <fmt/include/fmt/format.h>

namespace fcg {

namespace fs = std::filesystem;

namespace {

// The marker curl writes after the body, with the status and its error message.
constexpr std::string_view STATUS_LINE = "fcg-http-status ";

std::string curlProgram() {
#ifdef _WIN32
    // Windows' own, rather than whatever another program put first in the PATH.
    if (const char* root = std::getenv("SystemRoot")) {
        const auto own = fs::path(root) / "System32" / "curl.exe";
        std::error_code ec;
        if (fs::is_regular_file(own, ec)) return own.string();
    }
    return "curl.exe";
#else
    return "curl";
#endif
}

// A string of a curl config file: in double quotes, with \ and " escaped.
std::string configString(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        if (c == '\n' || c == '\r') continue;
        out += c;
    }
    return out + "\"";
}

// A file readable by its owner only.
bool writePrivate(const fs::path& file, std::string_view text) {
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out) return false;
    }
    std::error_code ec;
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

// Runs `program` with `args` in `dir` and returns everything it wrote on its standard output, read to the end
// (curl's own --max-time bounds it); nullopt when it cannot be started.
std::optional<std::string> capture(const std::string& program, const std::vector<std::string>& args,
                                   const fs::path& dir) {
    std::string out;
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &sa, 0)) return std::nullopt;
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    std::string command = "\"" + program + "\"";
    for (const auto& a : args) command += " \"" + a + "\"";
    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = write;
    si.hStdError  = write;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    const auto cwd = dir.string();
    const bool ok  = CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                    cwd.c_str(), &si, &pi);
    CloseHandle(write);
    if (!ok) {
        CloseHandle(read);
        return std::nullopt;
    }
    char buffer[4096];
    DWORD n = 0;
    while (ReadFile(read, buffer, sizeof(buffer), &n, nullptr) && n > 0) out.append(buffer, n);
    CloseHandle(read);
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#else
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) return std::nullopt;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);
    // The config file is given by its absolute path: no change of directory needed in the child.
    (void)dir;
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid    = 0;
    const int rc = posix_spawnp(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_fds[1]);
    if (rc != 0) {
        close(pipe_fds[0]);
        return std::nullopt;
    }
    char buffer[4096];
    for (ssize_t n; (n = ::read(pipe_fds[0], buffer, sizeof(buffer))) != 0;) {
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        out.append(buffer, static_cast<std::size_t>(n));
    }
    close(pipe_fds[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
#endif
    return out;
}

HttpResult run(const fs::path& config) {
    HttpResult result;
    const auto out = capture(curlProgram(), {"--config", config.string()}, config.parent_path());
    if (!out) {
        result.error = "curl cannot be started";
        return result;
    }
    const auto at = out->rfind(STATUS_LINE);
    if (at == std::string::npos) {
        result.error = "curl: " + *out;
        return result;
    }
    result.body     = out->substr(0, at);
    const auto rest = out->substr(at + STATUS_LINE.size());
    try {
        result.status = std::stoi(rest);
    } catch (...) {
    }
    if (result.status == 0) {
        auto message = rest.substr(std::min(rest.size(), rest.find(' ') + 1));
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) message.pop_back();
        result.error = message.empty() ? "no answer" : message;
    }
    return result;
}

}  // namespace

bool curlAvailable() {
    static const bool available = [] {
        const auto out = capture(curlProgram(), {"--version"}, fs::current_path());
        return out && out->rfind("curl", 0) == 0;
    }();
    return available;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields) {
    const auto encode = [](std::string_view s) {
        std::string out;
        for (const unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out += static_cast<char>(c);
            } else if (c == ' ') {
                out += '+';
            } else {
                out += fmt::format("%{:02X}", c);
            }
        }
        return out;
    };
    std::string out;
    for (const auto& [name, value] : fields) out += (out.empty() ? "" : "&") + encode(name) + "=" + encode(value);
    return out;
}

HttpResult httpPost(const HttpRequest& request, const fs::path& tmp_dir) {
    static std::atomic<unsigned> counter{0};
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);
    const auto stem = fmt::format("http-{}-{}", std::chrono::steady_clock::now().time_since_epoch().count(), counter++);
    const auto config = tmp_dir / (stem + ".curl");
    const auto body   = tmp_dir / (stem + ".body");

    std::string text = fmt::format("url = {}\nrequest = \"POST\"\nsilent\nshow-error\nmax-time = {}\n",
                                   configString(request.url), request.timeout_s);
    if (!request.token.empty()) text += "header = " + configString("Authorization: Bearer " + request.token) + "\n";
    if (!request.content_type.empty())
        text += "header = " + configString("Content-Type: " + request.content_type) + "\n";
    text += "data-binary = " + configString("@" + body.generic_string()) + "\n";
    text +=
        "write-out = " + configString(std::string("\\n") + std::string(STATUS_LINE) + "%{http_code} %{errormsg}\\n") +
        "\n";

    HttpResult result;
    if (!writePrivate(config, text) || !writePrivate(body, request.body)) {
        result.error = "cannot write in " + tmp_dir.string();
    } else {
        result = run(config);
    }
    fs::remove(config, ec);
    fs::remove(body, ec);
    return result;
}

}  // namespace fcg
