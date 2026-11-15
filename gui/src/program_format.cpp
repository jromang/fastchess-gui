#include "program_format.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "i18n.hpp"
#include "machines.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

uint32_t readLe(std::string_view s, std::size_t at, int bytes) {
    uint32_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(s[at + static_cast<std::size_t>(i)]);
    return v;
}

uint32_t readBe(std::string_view s, std::size_t at, int bytes) {
    uint32_t v = 0;
    for (int i = 0; i < bytes; ++i) v = (v << 8) | static_cast<unsigned char>(s[at + static_cast<std::size_t>(i)]);
    return v;
}

std::string elfArch(uint32_t machine, bool is64, bool little) {
    switch (machine) {
        case 3:
            return "x86";
        case 62:
            return "x86_64";
        case 40:
            return "arm";
        case 183:
            return "arm64";
        case 243:
            return is64 ? "riscv64" : "riscv32";
        case 21:
            return little ? "ppc64le" : "ppc64";
        case 22:
            return "s390x";
        case 258:
            return "loongarch64";
        default:
            return fmt::format("elf-{}", machine);
    }
}

std::string peArch(uint32_t machine) {
    switch (machine) {
        case 0x14c:
            return "x86";
        case 0x8664:
            return "x86_64";
        case 0xaa64:
        case 0xa641:
            return "arm64";  // ARM64EC too: Windows on arm64 only
        case 0x1c0:
        case 0x1c4:
            return "arm";
        default:
            return fmt::format("pe-{:x}", machine);
    }
}

std::string machoArch(uint32_t cpu) {
    switch (cpu) {
        case 7:
            return "x86";
        case 0x01000007:
            return "x86_64";
        case 12:
            return "arm";
        case 0x0100000c:
            return "arm64";
        case 0x0200000c:
            return "arm64_32";
        case 18:
            return "ppc";
        case 0x01000012:
            return "ppc64";
        default:
            return fmt::format("macho-{:x}", cpu);
    }
}

// An architecture as uname or Windows say it, in the names above: "aarch64" is arm64, "i686" x86, "armv7l" arm.
std::string normalArch(std::string arch) {
    for (auto& c : arch) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (arch == "amd64" || arch == "x64" || arch == "x86_64") return "x86_64";
    if (arch == "aarch64" || arch == "arm64") return "arm64";
    if (arch == "i386" || arch == "i486" || arch == "i586" || arch == "i686" || arch == "x86") return "x86";
    if (arch.rfind("armv", 0) == 0 || arch == "arm" || arch == "armhf") return "arm";
    return arch;
}

std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& s : items) out += (out.empty() ? "" : ", ") + s;
    return out;
}

bool has(const std::vector<std::string>& items, const std::string& item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool reservedWindowsName(std::string_view name) {
    // The device names, with an extension too ("nul.txt"), spaces before it left out.
    auto base = upper(name.substr(0, name.find('.')));
    while (!base.empty() && base.back() == ' ') base.pop_back();
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") return true;
    return base.size() == 4 && (base.rfind("COM", 0) == 0 || base.rfind("LPT", 0) == 0) && base[3] >= '1' &&
           base[3] <= '9';
}

constexpr std::string_view WINDOWS_FORBIDDEN = "<>:\"/\\|?*";

}  // namespace

ProgramFormat programFormat(std::string_view head) {
    ProgramFormat f;
    if (head.size() >= 2 && head[0] == '#' && head[1] == '!') {
        f.kind    = ProgramFormat::Kind::Script;
        auto end  = head.find('\n');
        auto line = head.substr(2, end == std::string_view::npos ? std::string_view::npos : end - 2);
        while (!line.empty() && (line.back() == '\r' || std::isspace(static_cast<unsigned char>(line.back())))) {
            line.remove_suffix(1);
        }
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.remove_prefix(1);
        f.shebang = std::string(line);
        // The words of the line: the interpreter, or env and what it runs (its options and variables left out).
        std::vector<std::string> words;
        std::size_t at = 0;
        while (at < line.size()) {
            const auto start = line.find_first_not_of(" \t", at);
            if (start == std::string_view::npos) break;
            const auto stop = line.find_first_of(" \t", start);
            words.emplace_back(
                line.substr(start, stop == std::string_view::npos ? std::string_view::npos : stop - start));
            at = stop == std::string_view::npos ? line.size() : stop;
        }
        if (!words.empty()) {
            const auto slash = words[0].rfind('/');
            const auto name  = slash == std::string::npos ? words[0] : words[0].substr(slash + 1);
            f.interpreter    = words[0];
            if (name == "env") {
                f.interpreter.clear();
                for (std::size_t i = 1; i < words.size(); ++i) {
                    if (words[i].empty() || words[i][0] == '-' || words[i].find('=') != std::string::npos) continue;
                    f.interpreter = words[i];
                    break;
                }
            }
        }
        return f;
    }
    if (head.size() >= 20 && head.substr(0, 4) == std::string_view("\x7f"
                                                                   "ELF",
                                                                   4)) {
        f.kind             = ProgramFormat::Kind::Elf;
        const bool is64    = head[4] == 2;
        const bool le      = head[5] != 2;
        const auto abi     = static_cast<unsigned char>(head[7]);
        const auto machine = le ? readLe(head, 18, 2) : readBe(head, 18, 2);
        f.os = abi == 9 ? "FreeBSD" : abi == 2 ? "NetBSD" : abi == 12 ? "OpenBSD" : abi == 6 ? "SunOS" : "Linux";
        f.archs.push_back(elfArch(machine, is64, le));
        return f;
    }
    if (head.size() >= 2 && head[0] == 'M' && head[1] == 'Z') {
        f.kind = ProgramFormat::Kind::Windows;
        f.os   = "Windows";
        if (head.size() >= 0x40) {
            const auto pe = readLe(head, 0x3c, 4);
            if (pe + 6 <= head.size() && head.substr(pe, 4) == std::string_view("PE\0\0", 4)) {
                f.archs.push_back(peArch(readLe(head, pe + 4, 2)));
            }
        }
        return f;
    }
    if (head.size() >= 8) {
        const auto magic_be = readBe(head, 0, 4);
        const auto magic_le = readLe(head, 0, 4);
        if (magic_le == 0xfeedface || magic_le == 0xfeedfacf) {
            f.kind = ProgramFormat::Kind::MachO;
            f.os   = "Darwin";
            f.archs.push_back(machoArch(readLe(head, 4, 4)));
            return f;
        }
        if (magic_be == 0xfeedface || magic_be == 0xfeedfacf) {
            f.kind = ProgramFormat::Kind::MachO;
            f.os   = "Darwin";
            f.archs.push_back(machoArch(readBe(head, 4, 4)));
            return f;
        }
        // Universal: CAFEBABE, which Java's class files begin with too; their version follows, 45 or more where a
        // universal program counts its architectures.
        if (magic_be == 0xcafebabe || magic_be == 0xcafebabf) {
            const auto count = readBe(head, 4, 4);
            if (count >= 1 && count < 20) {
                f.kind                 = ProgramFormat::Kind::MachO;
                f.os                   = "Darwin";
                const std::size_t step = magic_be == 0xcafebabf ? 32 : 20;
                for (uint32_t i = 0; i < count; ++i) {
                    const auto at = 8 + i * step;
                    if (at + 4 > head.size()) break;
                    const auto arch = machoArch(readBe(head, at, 4));
                    if (!has(f.archs, arch)) f.archs.push_back(arch);
                }
                return f;
            }
        }
    }
    return f;
}

ProgramFormat readProgramFormat(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::string head(4096, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(in.gcount()));
    // A PE header further than that: read up to it.
    if (head.size() >= 0x40 && head[0] == 'M' && head[1] == 'Z') {
        const auto pe = readLe(head, 0x3c, 4);
        if (pe + 6 > head.size() && pe < (1u << 20)) {
            in.clear();
            in.seekg(0);
            head.assign(pe + 6, '\0');
            in.read(head.data(), static_cast<std::streamsize>(head.size()));
            head.resize(static_cast<std::size_t>(in.gcount()));
        }
    }
    return programFormat(head);
}

std::string systemName(const std::string& os) {
    if (os == "Darwin") return "macOS";
    return os;
}

std::string describeSystem(const RemoteSystem& system) {
    const auto arch = normalArch(system.arch);
    return arch.empty() ? systemName(system.os) : systemName(system.os) + " " + arch;
}

std::string describeFormat(const ProgramFormat& f) {
    switch (f.kind) {
        case ProgramFormat::Kind::Unknown:
            return TR("a program of a format fastchess-gui does not know");
        case ProgramFormat::Kind::Script:
            return f.interpreter.empty() ? std::string(TR("a script")) : trf("a script for {}", f.interpreter);
        case ProgramFormat::Kind::Windows:
        case ProgramFormat::Kind::Elf:
        case ProgramFormat::Kind::MachO: {
            const auto os = systemName(f.os);
            if (f.archs.empty()) return trf("a {} program", os);
            if (f.archs.size() > 1) return trf("a {} program (universal: {})", os, joined(f.archs));
            return trf("a {} program for {}", os, f.archs.front());
        }
    }
    return {};
}

Suitability programSuits(const ProgramFormat& f, const RemoteSystem& system, const std::string& engine,
                         const std::string& machine) {
    using L = Suitability::Level;
    if (system.os.empty()) {
        return {L::Refused,
                trf("Test the connection to {} first: it tells the system the program must suit.", machine)};
    }
    const auto os      = normalizePlatform(system.os, system.arch).os;
    const auto arch    = normalArch(system.arch);
    const auto there   = describeSystem(system);
    const bool windows = os == "Windows";
    switch (f.kind) {
        case ProgramFormat::Kind::Unknown:
            return {L::Warning,
                    trf("The program of {} is not a Windows, Linux or macOS program fastchess-gui knows, nor a script: "
                        "copied all the same, the check after the copy tells whether it runs on {}.",
                        engine, machine)};
        case ProgramFormat::Kind::Script:
            if (windows) {
                return {L::Refused,
                        trf("{} is a script (#!{}), which Windows does not run: choose the Windows build of {}.",
                            engine, f.shebang, engine)};
            }
            return {};
        case ProgramFormat::Kind::Windows:
            if (!windows) {
                return {L::Refused, trf("{} is a Windows program; {} runs {}: choose the {} build of {}.", engine,
                                        machine, systemName(os), systemName(os), engine)};
            }
            break;
        case ProgramFormat::Kind::MachO:
            if (os != "Darwin") {
                return {L::Refused, trf("{} is a macOS program; {} runs {}: choose the {} build of {}.", engine,
                                        machine, systemName(os), systemName(os), engine)};
            }
            break;
        case ProgramFormat::Kind::Elf:
            if (windows || os == "Darwin") {
                return {L::Refused, trf("{} is a {} program; {} runs {}: choose the {} build of {}.", engine,
                                        systemName(f.os), machine, systemName(os), systemName(os), engine)};
            }
            if (f.os != os) {
                return {L::Warning, trf("{} is a {} program; {} runs {}: it runs there only with a compatibility "
                                        "layer.",
                                        engine, systemName(f.os), machine, systemName(os))};
            }
            break;
    }
    if (f.archs.empty() || arch.empty() || has(f.archs, arch)) return {};
    if (arch == "arm64" && os == "Darwin" && has(f.archs, "x86_64")) {
        return {L::Warning, trf("{} is built for x86_64; {} is an arm64 Mac: it runs there under Rosetta 2 only, if "
                                "it is installed (softwareupdate --install-rosetta).",
                                engine, machine)};
    }
    if (arch == "arm64" && windows && (has(f.archs, "x86_64") || has(f.archs, "x86"))) {
        return {L::Warning,
                trf("{} is built for {}; {} runs Windows on arm64: Windows 11 emulates it, more slowly than an arm64 "
                    "build.",
                    engine, joined(f.archs), machine)};
    }
    if (arch == "x86_64" && windows && has(f.archs, "x86")) return {};  // 32-bit programs run on 64-bit Windows
    if ((arch == "x86_64" && has(f.archs, "x86")) || (arch == "arm64" && has(f.archs, "arm"))) {
        return {L::Warning,
                trf("{} is a 32-bit program: {} runs it only with the 32-bit libraries installed.", engine, machine)};
    }
    return {L::Refused, trf("{} is built for {}; {} runs {}: choose the {} build of {}.", engine, joined(f.archs),
                            machine, there, there, engine)};
}

std::string windowsNameProblem(std::string_view name) {
    if (name.empty()) return TR("A file has no name.");
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 32 || WINDOWS_FORBIDDEN.find(c) != std::string_view::npos) {
            return u < 32 ? trf("{} has a control character, which Windows does not take in a file name.", name)
                          : trf("{} has a {}, which Windows does not take in a file name.", name, std::string(1, c));
        }
    }
    if (name.back() == '.' || name.back() == ' ') {
        return trf("{} ends with a dot or a space, which Windows leaves out of a file name.", name);
    }
    if (reservedWindowsName(name)) return trf("{} is a name Windows keeps for a device (CON, NUL, COM1…).", name);
    return {};
}

std::string safeFolderName(std::string_view name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        out += u < 32 || WINDOWS_FORBIDDEN.find(c) != std::string_view::npos ? '_' : c;
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.empty()) out = "engine";
    if (reservedWindowsName(out)) out += "_";
    return out;
}

}  // namespace fcg
