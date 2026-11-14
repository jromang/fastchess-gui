#include "report.hpp"

#include <algorithm>
#include <fstream>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/utsname.h>
#endif
#ifdef __APPLE__
#    include <sys/sysctl.h>
#endif

#include <fmt/include/fmt/format.h>

namespace fcg::report {

namespace {

// At most `max` bytes of `text`, not cutting a letter in two; "…" after it when something was left out.
std::string cut(std::string_view text, std::size_t max) {
    if (text.size() <= max) return std::string(text);
    std::size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;
    return std::string(text.substr(0, n)) + "…";
}

std::string field(std::string_view name, std::string_view value) {
    return "&" + std::string(name) + "=" + percentEncode(value);
}

// One line of the log is rarely worth more than this: an engine's long info line, an option list.
constexpr std::size_t MAX_LINE = 400;

}  // namespace

std::string percentEncode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '-' || u == '.' ||
            u == '_' || u == '~') {
            out += c;
        } else {
            out += fmt::format("%{:02X}", u);
        }
    }
    return out;
}

std::string issueUrl(const Facts& facts, std::size_t limit) {
    // Without the log, everything should fit; a very long message or system description is cut to leave the log room.
    std::string url  = std::string(NEW_ISSUE) + "?template=bug_report.yml" + field("version", facts.version);
    const auto fixed = [&](std::size_t max) {
        auto out = url;
        if (!facts.seen.empty()) out += field("seen", cut(facts.seen, max));
        out += field("system", cut(facts.system, max));
        return out;
    };
    std::string head = fixed(std::string::npos);
    for (std::size_t max = 2000; head.size() > limit / 2 && max >= 100; max /= 2) head = fixed(max);
    if (head.size() > limit) return head.substr(0, std::min(head.size(), limit));  // not reached with sane facts

    // The newest lines of the log first, as many as fit, written oldest first.
    std::vector<std::string> kept;
    std::size_t size = head.size() + 5;  // "&log="
    for (auto it = facts.log.rbegin(); it != facts.log.rend(); ++it) {
        auto encoded = percentEncode(cut(*it, MAX_LINE) + "\n");
        if (size + encoded.size() > limit) break;
        size += encoded.size();
        kept.push_back(std::move(encoded));
    }
    if (kept.empty()) return head;
    head += "&log=";
    for (auto it = kept.rbegin(); it != kept.rend(); ++it) head += *it;
    return head;
}

std::vector<std::string> lastLines(const std::filesystem::path& file, std::size_t count) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto size  = static_cast<std::size_t>(std::max<std::streamoff>(0, in.tellg()));
    const auto start = size > 65536 ? size - 65536 : 0;
    in.seekg(static_cast<std::streamoff>(start));
    std::string text(size - start, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, in.gcount())));

    std::vector<std::string> lines;
    std::size_t at = 0;
    if (start > 0) at = text.find('\n') == std::string::npos ? text.size() : text.find('\n') + 1;  // a line's end
    while (at < text.size()) {
        auto end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        auto line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        at = end + 1;
    }
    if (lines.size() > count) lines.erase(lines.begin(), lines.end() - static_cast<std::ptrdiff_t>(count));
    return lines;
}

std::string systemName() {
#ifdef _WIN32
    // GetVersionEx tells programs without a manifest that they run on Windows 8: RtlGetVersion tells the truth.
    std::string name    = "Windows";
    using RtlGetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (const auto ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (const auto get =
                reinterpret_cast<RtlGetVersion>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))) {
            OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (get(&info) == 0) {
                name =
                    fmt::format("Windows {}.{} build {}", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
            }
        }
    }
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    const char* arch = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64   ? "x86_64"
                       : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64"
                                                                                   : "other";
    return fmt::format("{} ({})", name, arch);
#else
    struct utsname u{};
    const bool known          = uname(&u) == 0;
    const std::string machine = known ? u.machine : "?";
#    ifdef __APPLE__
    char version[64]   = {};
    std::size_t length = sizeof(version);
    const std::string product =
        sysctlbyname("kern.osproductversion", version, &length, nullptr, 0) == 0 ? std::string(version) : "?";
    return fmt::format("macOS {} ({})", product, machine);
#    else
    // The distribution, from os-release: PRETTY_NAME="Ubuntu 24.04.1 LTS".
    std::string distribution;
    for (const char* path : {"/etc/os-release", "/usr/lib/os-release"}) {
        std::ifstream in(path);
        for (std::string line; distribution.empty() && std::getline(in, line);) {
            if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
            distribution = line.substr(12);
            if (distribution.size() >= 2 && (distribution.front() == '"' || distribution.front() == '\'')) {
                distribution = distribution.substr(1, distribution.size() - 2);
            }
        }
        if (!distribution.empty()) break;
    }
    const std::string kernel = known ? fmt::format("{} {}", u.sysname, u.release) : "Linux";
    return distribution.empty() ? fmt::format("{} ({})", kernel, machine)
                                : fmt::format("{}, {} ({})", distribution, kernel, machine);
#    endif
#endif
}

}  // namespace fcg::report
