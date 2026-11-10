#include "https.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#    include <engine/process/process_win.hpp>
#else
#    include <engine/process/process_posix.hpp>
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

HttpResult run(const fs::path& dir, const std::string& args, std::chrono::seconds timeout) {
    using fastchess::engine::process::Line;
    using fastchess::engine::process::Process;
    using fastchess::engine::process::Status;

    HttpResult result;
    Process process;
    process.setRealtimeLogging(false);
    if (const auto started = process.init(dir.string(), curlProgram(), args, "curl"); started.code != Status::OK) {
        result.error = "curl: " + started.message;
        return result;
    }
    std::vector<Line> lines;
    const auto read = process.readOutput(lines, STATUS_LINE, timeout);
    if (read.code != Status::OK) {
        result.error = read.code == Status::TIMEOUT ? "no answer in time" : "curl stopped: " + read.message;
        return result;
    }
    for (std::size_t i = 0; i + 1 < lines.size(); ++i) result.body += lines[i].line + "\n";
    const auto& last = lines.back().line;
    const auto rest  = last.substr(STATUS_LINE.size());
    try {
        result.status = std::stoi(rest);
    } catch (...) {
    }
    if (result.status == 0) {
        const auto space = rest.find(' ');
        result.error     = space == std::string::npos ? "no answer" : rest.substr(space + 1);
    }
    return result;
}

}  // namespace

bool curlAvailable() {
    static const bool available = [] {
        // "--version" prints a first line starting with "curl".
        using fastchess::engine::process::Line;
        using fastchess::engine::process::Process;
        using fastchess::engine::process::Status;
        Process process;
        process.setRealtimeLogging(false);
        if (process.init(fs::current_path().string(), curlProgram(), "--version", "curl").code != Status::OK) {
            return false;
        }
        std::vector<Line> lines;
        return process.readOutput(lines, "curl", std::chrono::seconds(5)).code == Status::OK;
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
        result = run(tmp_dir, fmt::format("--config \"{}\"", config.filename().string()),
                     std::chrono::seconds(request.timeout_s + 5));
    }
    fs::remove(config, ec);
    fs::remove(body, ec);
    return result;
}

}  // namespace fcg
