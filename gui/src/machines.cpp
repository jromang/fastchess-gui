#include "machines.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <random>
#include <set>

#include <psa/crypto.h>

#include "i18n.hpp"
#include "portable.hpp"
#include "storage.hpp"
#include "tablebases.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

uint64_t number(const std::map<std::string, std::string>& values, const std::string& key) {
    const auto it = values.find(key);
    if (it == values.end()) return 0;
    uint64_t n = 0;
    for (const char c : it->second) {
        if (!std::isdigit(static_cast<unsigned char>(c))) break;
        n = n * 10 + static_cast<uint64_t>(c - '0');
    }
    return n;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Quoting

std::string shQuote(std::string_view arg) {
    std::string out = "'";
    for (const char c : arg) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    return out + "'";
}

std::optional<std::string> cmdQuote(std::string_view arg) {
    // Inside double quotes cmd takes & | < > ^ ( ) as they are, but expands %VAR%, and cannot hold a quote. A
    // backslash before the closing quote would escape it for the program reading its command line
    // (CommandLineToArgvW): doubled.
    if (arg.find_first_of("\"%\r\n") != std::string_view::npos) return std::nullopt;
    std::string out = "\"";
    out += arg;
    std::size_t backslashes = 0;
    for (auto it = arg.rbegin(); it != arg.rend() && *it == '\\'; ++it) ++backslashes;
    out.append(backslashes, '\\');
    return out + "\"";
}

std::optional<std::string> quoteFor(RemoteShell shell, std::string_view arg) {
    if (shell == RemoteShell::Cmd) return cmdQuote(arg);
    return shQuote(arg);
}

std::string viaSh(const std::string& command) { return "sh -c " + shQuote(command); }

std::string psQuote(std::string_view text) {
    std::string out;
    for (const char c : text) {
        out += c;
        if (c == '\'') out += '\'';
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Systems

Platform normalizePlatform(const std::string& os, const std::string& arch) {
    Platform p;
    std::string o = os, a = arch;
    for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (os.rfind("MINGW", 0) == 0 || os.rfind("MSYS", 0) == 0 || os.rfind("CYGWIN", 0) == 0 ||
        os.rfind("Windows", 0) == 0) {
        o = "Windows";
    }
    p.os   = o;
    p.arch = a == "amd64" || a == "x86_64" || a == "x64" ? "x86_64" : a == "aarch64" || a == "arm64" ? "arm64" : a;
    return p;
}

Platform thisPlatform() {
#if defined(_WIN32)
    const std::string os = "Windows";
#elif defined(__APPLE__)
    const std::string os = "Darwin";
#elif defined(__linux__)
    const std::string os = "Linux";
#else
    const std::string os = "Unknown";
#endif
#if defined(__x86_64__) || defined(_M_X64)
    const std::string arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    const std::string arch = "arm64";
#else
    const std::string arch = "unknown";
#endif
    return {os, arch};
}

RemoteShell shellFromEcho(const std::string& output) {
    const auto text = trim(output);
    if (text.empty()) return RemoteShell::Unknown;
    if (text.find("%OS%") != std::string::npos) return RemoteShell::Posix;  // not expanded: told by uname next
    if (text.find("Windows") != std::string::npos) return RemoteShell::Cmd;
    return RemoteShell::Unknown;
}

std::map<std::string, std::string> keyValues(const std::string& output) {
    std::map<std::string, std::string> out;
    std::size_t start = 0;
    while (start < output.size()) {
        auto end = output.find('\n', start);
        if (end == std::string::npos) end = output.size();
        const auto line = trim(std::string_view(output).substr(start, end - start));
        start           = end + 1;
        const auto eq   = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        const auto key = trim(std::string_view(line).substr(0, eq));
        if (!std::all_of(key.begin(), key.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; })) {
            continue;
        }
        out[key] = trim(std::string_view(line).substr(eq + 1));
    }
    return out;
}

std::pair<std::string, int> parseVersion(const std::string& output) {
    std::string version;
    int protocol           = 0;
    constexpr auto PROGRAM = std::string_view("fastchess-gui ");
    std::size_t start      = 0;
    while (start < output.size()) {
        auto end = output.find('\n', start);
        if (end == std::string::npos) end = output.size();
        const auto line = trim(std::string_view(output).substr(start, end - start));
        start           = end + 1;
        if (line.rfind(PROGRAM, 0) == 0 && version.empty()) {
            version = trim(std::string_view(line).substr(PROGRAM.size()));
            // A version is digits and dots (and a suffix): anything else is not this answer.
            if (version.empty() || !std::isdigit(static_cast<unsigned char>(version[0]))) version.clear();
            if (const auto space = version.find(' '); space != std::string::npos) version.erase(space);
        } else if (line.rfind("protocol ", 0) == 0) {
            protocol = std::atoi(line.c_str() + 9);
        }
    }
    if (version.empty()) protocol = 0;
    return {version, protocol};
}

void readPosixProbe(const std::string& output, RemoteSystem& system) {
    const auto v   = keyValues(output);
    const auto get = [&](const char* key) {
        const auto it = v.find(key);
        return it == v.end() ? std::string() : it->second;
    };
    system.os      = get("os");
    system.arch    = get("arch");
    system.release = get("release");
    system.threads = static_cast<int>(number(v, "threads"));
    if (v.count("memory_kb")) {
        system.memory_mb = number(v, "memory_kb") / 1024;
    } else {
        system.memory_mb = number(v, "memory_bytes") / (1024 * 1024);
    }
    system.disk_free_mb = number(v, "disk_kb") / 1024;
    system.home         = get("home");
}

void readWindowsProbe(const std::string& output, RemoteSystem& system) {
    const auto v   = keyValues(output);
    const auto get = [&](const char* key) {
        const auto it = v.find(key);
        return it == v.end() ? std::string() : it->second;
    };
    system.os           = "Windows";
    system.arch         = get("arch");
    system.threads      = static_cast<int>(number(v, "threads"));
    system.memory_mb    = number(v, "memory_bytes") / (1024 * 1024);
    system.disk_free_mb = number(v, "disk_bytes") / (1024 * 1024);
    system.home         = get("home");
    // `ver`: "Microsoft Windows [Version 10.0.22631.4317]", the word translated in other languages.
    const auto open = output.find('[');
    const auto end  = open == std::string::npos ? std::string::npos : output.find(']', open);
    if (end != std::string::npos) {
        const auto inside = output.substr(open + 1, end - open - 1);
        const auto digit  = inside.find_first_of("0123456789");
        if (digit != std::string::npos) system.release = trim(inside.substr(digit));
    }
}

std::string posixProbe(const std::string& folder) {
    const auto dir = shQuote(folder);
    const auto bin = shQuote(folder + "/fastchess-gui");
    // One line at a time, joined by ';': csh, if it is the user's shell, takes no line break inside quotes.
    return viaSh(
        "echo os=$(uname -s); echo arch=$(uname -m); echo release=$(uname -r); "
        "echo threads=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null); "
        "if [ -r /proc/meminfo ]; then echo memory_kb=$(awk '/^MemTotal:/ {print $2}' /proc/meminfo); "
        "else echo memory_bytes=$(sysctl -n hw.memsize 2>/dev/null); fi; "
        "d=" +
        dir +
        "; while [ ! -d \"$d\" ]; do d=$(dirname \"$d\"); done; "
        "echo disk_kb=$(df -Pk \"$d\" | awk 'NR==2 {print $4}'); echo home=$HOME; "
        "if [ -x " +
        bin +
        " ]; then t=; command -v timeout >/dev/null 2>&1 && t='timeout 15'; "
        "$t " +
        bin + " --version 2>/dev/null; fi");
}

std::optional<std::string> windowsProbe(const std::string& folder) {
    const auto exe = cmdQuote(folder + "\\fastchess-gui.exe");
    if (!exe || folder.find('\'') != std::string::npos) return std::nullopt;
    // Memory and free space through PowerShell (cmd has neither), its command in double quotes for cmd: no double
    // quote nor % inside.
    return "echo os=Windows& echo arch=%PROCESSOR_ARCHITECTURE%& echo threads=%NUMBER_OF_PROCESSORS%& "
           "echo home=%USERPROFILE%& ver& "
           "powershell -NoProfile -NonInteractive -Command \"'memory_bytes=' + "
           "(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory; 'disk_bytes=' + "
           "([System.IO.DriveInfo]::new([System.IO.Path]::GetPathRoot([System.IO.Path]::GetFullPath('" +
           psQuote(folder) + "')))).AvailableFreeSpace\"& if exist " + *exe + " (" + *exe + " --version)";
}

std::optional<std::string> hashCommand(RemoteShell shell, const std::string& path) {
    if (shell == RemoteShell::Cmd) {
        const auto quoted = cmdQuote(path);
        if (!quoted) return std::nullopt;
        return "certutil -hashfile " + *quoted + " SHA256";
    }
    const auto quoted = shQuote(path);
    return viaSh("sha256sum " + quoted + " 2>/dev/null || shasum -a 256 " + quoted);
}

std::string readHash(const std::string& output) {
    std::size_t start = 0;
    while (start < output.size()) {
        auto end = output.find('\n', start);
        if (end == std::string::npos) end = output.size();
        std::string digits;
        for (std::size_t i = start; i < end; ++i) {
            const auto c = static_cast<unsigned char>(output[i]);
            if (std::isxdigit(c)) {
                digits += static_cast<char>(std::tolower(c));
                if (digits.size() == 64) {
                    // Followed by another digit, it is something else.
                    if (i + 1 < end && std::isxdigit(static_cast<unsigned char>(output[i + 1]))) break;
                    return digits;
                }
            } else if (c != ' ') {
                digits.clear();
            }
        }
        start = end + 1;
    }
    return {};
}

std::optional<std::string> startCommand(RemoteShell shell, const std::string& program, const std::string& args) {
    const auto dir = remoteParent(program);
    if (shell == RemoteShell::Cmd) {
        const auto exe = cmdQuote(program);
        if (!exe) return std::nullopt;
        if (args.find_first_of("%\r\n") != std::string::npos) return std::nullopt;
        std::string line = *exe + (args.empty() ? "" : " " + args);
        if (dir.empty()) return line;
        const auto folder = cmdQuote(dir);
        if (!folder) return std::nullopt;
        return "cd /d " + *folder + " && " + line;
    }
    std::string line = "exec " + shQuote(program) + (args.empty() ? "" : " " + args);
    if (!dir.empty()) line = "cd " + shQuote(dir) + " && " + line;
    return viaSh(line);
}

// ---------------------------------------------------------------------------------------------------------------
// Machines

std::string Machine::booksDir() const { return books_dir.empty() ? remoteJoin(folder, "books") : books_dir; }
std::string Machine::syzygyDir() const { return syzygy_dir.empty() ? remoteJoin(folder, "syzygy") : syzygy_dir; }

std::string Machine::programPath() const {
    if (system.shell == RemoteShell::Cmd) return folder + "\\fastchess-gui.exe";
    return remoteJoin(folder, "fastchess-gui");
}

int Machine::coreBudget() const {
    if (cores > 0) return cores;
    return std::max(1, system.threads - 1);
}

int Machine::memoryBudget() const {
    if (memory_mb > 0) return memory_mb;
    return static_cast<int>(system.memory_mb * 8 / 10);
}

const MachineEngine* Machine::engine(const std::string& name) const {
    for (const auto& e : engines) {
        if (e.engine == name) return &e;
    }
    return nullptr;
}

std::string Machine::address() const {
    std::string out = user.empty() ? host : user + "@" + host;
    if (port != 22) out += ":" + std::to_string(port);
    return out;
}

namespace {

const char* shellName(RemoteShell shell) {
    return shell == RemoteShell::Posix ? "posix" : shell == RemoteShell::Cmd ? "cmd" : "";
}

RemoteShell shellOf(const std::string& name) {
    return name == "posix" ? RemoteShell::Posix : name == "cmd" ? RemoteShell::Cmd : RemoteShell::Unknown;
}

}  // namespace

void to_json(nlohmann::json& j, const Machine& m) {
    j = nlohmann::json{{"id", m.id},        {"name", m.name}, {"host", m.host},
                       {"port", m.port},    {"user", m.user}, {"auth", m.auth == SshAuth::KeyFile ? "key" : "agent"},
                       {"folder", m.folder}};
    if (!m.key_file.empty()) j["key_file"] = portable::stored(m.key_file);
    if (!m.books_dir.empty()) j["books_dir"] = m.books_dir;
    if (!m.syzygy_dir.empty()) j["syzygy_dir"] = m.syzygy_dir;
    if (!m.program.empty()) j["program"] = portable::stored(m.program);
    if (m.cores > 0) j["cores"] = m.cores;
    if (m.memory_mb > 0) j["memory_mb"] = m.memory_mb;
    if (!m.host_key.empty()) j["host_key"] = m.host_key;
    auto engines = nlohmann::json::array();
    for (const auto& e : m.engines) {
        nlohmann::json je{{"engine", e.engine}, {"path", e.path}};
        if (e.check_ok >= 0) {
            je["check_ok"]      = e.check_ok;
            je["check_message"] = e.check_message;
        }
        if (!e.id_name.empty()) je["id_name"] = e.id_name;
        engines.push_back(std::move(je));
    }
    j["engines"]  = std::move(engines);
    const auto& s = m.system;
    if (!s.checked.empty()) {
        j["system"] = {{"shell", shellName(s.shell)},    {"os", s.os},           {"arch", s.arch},
                       {"release", s.release},           {"threads", s.threads}, {"memory_mb", s.memory_mb},
                       {"disk_free_mb", s.disk_free_mb}, {"home", s.home},       {"fcg_version", s.fcg_version},
                       {"fcg_protocol", s.fcg_protocol}, {"checked", s.checked}};
    }
}

void from_json(const nlohmann::json& j, Machine& m) {
    m            = Machine{};
    m.id         = j.value("id", std::string());
    m.name       = j.value("name", std::string());
    m.host       = j.value("host", std::string());
    m.port       = j.value("port", 22);
    m.user       = j.value("user", std::string());
    m.auth       = j.value("auth", std::string()) == "key" ? SshAuth::KeyFile : SshAuth::Agent;
    m.key_file   = portable::resolved(j.value("key_file", std::string()));
    m.folder     = j.value("folder", std::string("fastchess-gui"));
    m.books_dir  = j.value("books_dir", std::string());
    m.syzygy_dir = j.value("syzygy_dir", std::string());
    m.program    = portable::resolved(j.value("program", std::string()));
    m.cores      = j.value("cores", 0);
    m.memory_mb  = j.value("memory_mb", 0);
    m.host_key   = j.value("host_key", std::string());
    if (const auto it = j.find("engines"); it != j.end() && it->is_array()) {
        for (const auto& je : *it) {
            if (!je.is_object()) continue;
            MachineEngine e;
            e.engine        = je.value("engine", std::string());
            e.path          = je.value("path", std::string());
            e.check_ok      = je.value("check_ok", -1);
            e.check_message = je.value("check_message", std::string());
            e.id_name       = je.value("id_name", std::string());
            if (!e.engine.empty()) m.engines.push_back(std::move(e));
        }
    }
    if (const auto it = j.find("system"); it != j.end() && it->is_object()) {
        auto& s        = m.system;
        s.shell        = shellOf(it->value("shell", std::string()));
        s.os           = it->value("os", std::string());
        s.arch         = it->value("arch", std::string());
        s.release      = it->value("release", std::string());
        s.threads      = it->value("threads", 0);
        s.memory_mb    = it->value("memory_mb", uint64_t(0));
        s.disk_free_mb = it->value("disk_free_mb", uint64_t(0));
        s.home         = it->value("home", std::string());
        s.fcg_version  = it->value("fcg_version", std::string());
        s.fcg_protocol = it->value("fcg_protocol", 0);
        s.checked      = it->value("checked", std::string());
    }
}

std::vector<Machine> loadMachines(const fs::path& profile) {
    std::vector<Machine> out;
    const auto json = readJson<nlohmann::json>(profile / "machines.json");
    if (!json || !json->is_array()) return out;
    for (const auto& j : *json) {
        if (!j.is_object()) continue;
        try {
            out.push_back(j.get<Machine>());
        } catch (const std::exception&) {
            // A machine written by hand that cannot be read is left out, the others kept.
        }
    }
    return out;
}

bool saveMachines(const fs::path& profile, const std::vector<Machine>& machines) {
    return writeJson(profile / "machines.json", nlohmann::json(machines));
}

std::string newMachineId(const std::vector<Machine>& machines) {
    std::random_device device;
    std::mt19937_64 random(device());
    while (true) {
        const auto id = fmt::format("m{:08x}", static_cast<uint32_t>(random()));
        if (std::none_of(machines.begin(), machines.end(), [&](const Machine& m) { return m.id == id; })) return id;
    }
}

fs::path knownHostsFile(const fs::path& profile) { return profile / "known_hosts.json"; }

SshTarget targetOf(const Machine& machine, const fs::path& profile, const std::string& passphrase) {
    SshTarget t;
    t.name        = machine.name.empty() ? machine.host : machine.name;
    t.host        = machine.host;
    t.port        = machine.port;
    t.user        = machine.user;
    t.auth        = machine.auth;
    t.key_file    = machine.key_file;
    t.passphrase  = passphrase;
    t.known_hosts = knownHostsFile(profile);
    return t;
}

std::string incompleteMachine(const Machine& machine) {
    if (machine.host.empty()) return TR("Give the machine's host name or address.");
    if (machine.user.empty()) return TR("Give the user to log in as.");
    if (machine.port < 1 || machine.port > 65535) return TR("The port is 1 to 65535 (22 for SSH).");
    if (machine.auth == SshAuth::KeyFile && machine.key_file.empty()) {
        return TR("Choose the key file, or log in with the SSH agent.");
    }
    if (machine.folder.empty()) return TR("Give the machine's folder for fastchess-gui.");
    return {};
}

fs::path programToInstall(const Machine& machine, const fs::path& self, std::string& error) {
    const auto& s = machine.system;
    if (s.os.empty()) {
        error = TR("Test the connection first: it tells the machine's system.");
        return {};
    }
    const auto remote = normalizePlatform(s.os, s.arch);
    if (!machine.program.empty()) {
        std::error_code ec;
        if (!fs::is_regular_file(machine.program, ec)) {
            error = trf("{} is not there: choose the fastchess-gui for {} {} again.", machine.program, remote.os,
                        remote.arch);
            return {};
        }
        return machine.program;
    }
    if (remote == thisPlatform()) return self;
    if (remote.os == "Windows") {
        error =
            trf("The machine runs Windows ({}): download fastchess-gui for Windows from the releases page, then "
                "choose its fastchess-gui.exe below.",
                remote.arch);
    } else {
        error =
            trf("The machine runs {} {}, this computer {} {}: download fastchess-gui for {} {} from the releases "
                "page and choose its program below, or install it on the machine yourself, in {}.",
                remote.os, remote.arch, thisPlatform().os, thisPlatform().arch, remote.os, remote.arch, machine.folder);
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// Books and tablebases

SyncPlan planSync(const std::vector<SyncFile>& local, const std::vector<SyncFile>& remote, bool by_hash) {
    std::map<std::string, const SyncFile*> there;
    for (const auto& f : remote) there[f.name] = &f;
    SyncPlan plan;
    for (const auto& f : local) {
        const auto it = there.find(f.name);
        if (it == there.end()) {
            plan.missing.push_back(f);
            plan.bytes += f.size;
        } else if (it->second->size != f.size || (by_hash && it->second->hash != f.hash)) {
            plan.different.push_back(f);
            plan.bytes += f.size;
        } else {
            ++plan.same;
        }
    }
    return plan;
}

int syzygyPieces(const std::string& file_name) {
    const auto dot = file_name.rfind('.');
    if (dot == std::string::npos) return 0;
    const auto ext = file_name.substr(dot);
    if (ext != ".rtbw" && ext != ".rtbz") return 0;
    const auto table = file_name.substr(0, dot);
    // K...vK...: letters of pieces, one 'v'.
    if (table.size() < 4 || table[0] != 'K' || std::count(table.begin(), table.end(), 'v') != 1) return 0;
    const auto v = table.find('v');
    if (v + 1 >= table.size() || table[v + 1] != 'K') return 0;
    for (const char c : table) {
        if (c != 'v' && std::string_view("KQRBNP").find(c) == std::string_view::npos) return 0;
    }
    return static_cast<int>(table.size()) - 1;
}

std::vector<SyncFile> localSyzygyFiles(const std::string& paths, int max_pieces) {
    const int most = max_pieces > 0 ? std::min(max_pieces, MAX_PUSHED_PIECES) : MAX_PUSHED_PIECES;
    std::vector<SyncFile> out;
    std::set<std::string> seen;
    for (const auto& dir : splitTablebasePath(paths)) {
        std::error_code ec;
        fs::directory_iterator it(dir, ec);
        for (const fs::directory_iterator last; !ec && it != last; it.increment(ec)) {
            const auto name  = it->path().filename().string();
            const int pieces = syzygyPieces(name);
            if (pieces == 0 || pieces > most || seen.count(name)) continue;
            std::error_code size_ec;
            const auto size = it->file_size(size_ec);
            if (size_ec) continue;
            seen.insert(name);
            out.push_back({name, size, {}, it->path()});
        }
    }
    std::sort(out.begin(), out.end(), [](const SyncFile& a, const SyncFile& b) { return a.name < b.name; });
    return out;
}

std::vector<SyncFile> remoteSyzygyFiles(const std::vector<RemoteEntry>& listing, int max_pieces) {
    const int most = max_pieces > 0 ? std::min(max_pieces, MAX_PUSHED_PIECES) : MAX_PUSHED_PIECES;
    std::vector<SyncFile> out;
    for (const auto& e : listing) {
        const int pieces = syzygyPieces(e.name);
        if (pieces == 0 || pieces > most || e.directory) continue;
        out.push_back({e.name, e.size, {}, {}});
    }
    return out;
}

std::string sha256File(const fs::path& file, const std::atomic_bool* cancel) {
    std::ifstream in(file, std::ios::binary);
    if (!in || psa_crypto_init() != PSA_SUCCESS) return {};
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS) return {};
    std::string buffer(std::size_t(1) << 20, '\0');
    while (in.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || in.gcount() > 0) {
        if ((cancel && *cancel) || psa_hash_update(&op, reinterpret_cast<const uint8_t*>(buffer.data()),
                                                   static_cast<std::size_t>(in.gcount())) != PSA_SUCCESS) {
            psa_hash_abort(&op);
            return {};
        }
    }
    uint8_t hash[32];
    std::size_t n = 0;
    if (psa_hash_finish(&op, hash, sizeof hash, &n) != PSA_SUCCESS || n != sizeof hash) return {};
    std::string out;
    for (const auto b : hash) out += fmt::format("{:02x}", b);
    return out;
}

std::string formatBytes(uint64_t bytes) {
    constexpr uint64_t KB = 1024, MB = KB * 1024, GB = MB * 1024;
    if (bytes < KB) return trf("{} bytes", bytes);
    if (bytes < MB) return trf("{} KB", (bytes + KB / 2) / KB);
    if (bytes < GB) return trf("{} MB", (bytes + MB / 2) / MB);
    return trf("{:.1f} GB", static_cast<double>(bytes) / static_cast<double>(GB));
}

}  // namespace fcg
