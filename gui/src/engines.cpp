#include "engines.hpp"
#include "i18n.hpp"
#include "wb2uci.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <set>
#include <sstream>

#include <engine/compliance.hpp>
#include <engine/uci_engine.hpp>
#include <tour/engine_text.hpp>
#include <tour/game_db.hpp>

namespace fcg {

namespace {

bool isKeyword(std::string_view token) {
    return token == "default" || token == "min" || token == "max" || token == "var";
}

std::vector<std::string_view> split(std::string_view line) {
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        const auto start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        if (i > start) tokens.push_back(line.substr(start, i - start));
    }
    return tokens;
}

std::string join(const std::vector<std::string_view>& tokens, std::size_t from, std::size_t to) {
    std::string out;
    for (auto i = from; i < to; ++i) {
        if (!out.empty()) out += ' ';
        out += tokens[i];
    }
    return out;
}

}  // namespace

std::optional<UciOptionInfo> parseUciOption(std::string_view line) {
    const auto tokens = split(line);
    if (tokens.size() < 5 || tokens[0] != "option" || tokens[1] != "name") return std::nullopt;

    std::size_t type_at = 2;
    while (type_at < tokens.size() && tokens[type_at] != "type") ++type_at;
    if (type_at + 1 >= tokens.size() || type_at == 2) return std::nullopt;

    UciOptionInfo option;
    option.name = join(tokens, 2, type_at);
    option.type = std::string(tokens[type_at + 1]);

    for (auto i = type_at + 2; i < tokens.size();) {
        const auto key = tokens[i];
        auto end       = i + 1;

        // A string default may contain spaces, so it runs to the end of the line.
        if (key == "default" && option.type == "string") {
            end = tokens.size();
        } else {
            while (end < tokens.size() && !isKeyword(tokens[end])) ++end;
        }

        auto value = join(tokens, i + 1, end);
        if (value == "<empty>") value.clear();

        if (key == "default")
            option.default_value = value;
        else if (key == "min")
            option.min = value;
        else if (key == "max")
            option.max = value;
        else if (key == "var")
            option.vars.push_back(value);

        i = end;
    }

    return option;
}

bool programNeedsCrc(EngineEntry& entry) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (entry.cmd.empty()) {
        entry.program_state = ProgramState::Unknown;
        return false;
    }
    if (!fs::is_regular_file(entry.cmd, ec)) {
        entry.program_state = ProgramState::Missing;
        return false;
    }
    if (!entry.dir.empty() && !fs::is_directory(entry.dir, ec)) {
        entry.program_state = ProgramState::MissingDir;
        return false;
    }
    if (entry.program.size == 0) return true;
    const auto size  = fs::file_size(entry.cmd, ec);
    const auto mtime = fs::last_write_time(entry.cmd, ec).time_since_epoch().count();
    if (ec || size != entry.program.size) {
        entry.program_state = ProgramState::Changed;
        return false;
    }
    if (entry.program.mtime != 0 && mtime == entry.program.mtime) {
        entry.program_state = ProgramState::Ok;
        return false;
    }
    return true;
}

bool applyFingerprint(EngineEntry& entry, const tour::FileFingerprint& now) {
    if (now.size == 0) {
        entry.program_state = ProgramState::Missing;  // gone, or unreadable, meanwhile
        return false;
    }
    if (entry.program.size == 0) {
        entry.program       = now;
        entry.program_state = ProgramState::Ok;
        return true;
    }
    if (now.size != entry.program.size || now.crc32 != entry.program.crc32) {
        entry.program_state = ProgramState::Changed;
        return false;
    }
    entry.program_state = ProgramState::Ok;
    // Same contents under a new date (copied, touched): remember the date, so the CRC is not read every time.
    if (now.mtime == entry.program.mtime) return false;
    entry.program.mtime = now.mtime;
    return true;
}

bool refreshProgram(EngineEntry& entry) {
    if (!programNeedsCrc(entry)) return false;
    return applyFingerprint(entry, tour::fingerprint(entry.cmd));
}

namespace {

// Well-formed UTF-8 (RFC 3629): no overlong forms, no surrogates, nothing above U+10FFFF.
bool isUtf8(std::string_view text) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    for (std::size_t i = 0; i < text.size();) {
        const auto c = byte(i);
        int extra    = 0;
        unsigned lo = 0x80, hi = 0xBF;  // the second byte's range
        if (c < 0x80) {
            ++i;
            continue;
        } else if (c >= 0xC2 && c <= 0xDF) {
            extra = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            extra = 2;
            if (c == 0xE0) lo = 0xA0;
            if (c == 0xED) hi = 0x9F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            extra = 3;
            if (c == 0xF0) lo = 0x90;
            if (c == 0xF4) hi = 0x8F;
        } else {
            return false;
        }
        if (i + extra >= text.size() || byte(i + 1) < lo || byte(i + 1) > hi) return false;
        for (int k = 2; k <= extra; ++k) {
            if ((byte(i + k) & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

}  // namespace

std::string toUtf8(std::string text) {
    if (isUtf8(text)) return text;

    // 0x80 to 0x9F in Windows-1252; the other bytes are Latin-1, the same code points.
    static constexpr char16_t CP1252[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                            0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                            0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                            0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
    std::string out;
    out.reserve(text.size() + text.size() / 4);
    for (const char ch : text) {
        const auto c      = static_cast<unsigned char>(ch);
        const char16_t cp = c >= 0x80 && c < 0xA0 ? CP1252[c - 0x80] : c;
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

std::string engineWorkingDir(const EngineEntry& entry) {
    if (!entry.dir.empty()) return entry.dir;
    return std::filesystem::path(entry.cmd).parent_path().string();
}

std::string detectProtocol(const EngineEntry& entry) {
    using namespace std::chrono;
    using fastchess::engine::process::Status;

    fastchess::engine::process::Process process;
    process.setRealtimeLogging(false);
    if (!process.init(engineWorkingDir(entry), entry.cmd, entry.args, "detect")) return "uci";

    // A UCI engine answers uciok, possibly after loading a network. A WinBoard engine answers protover 2 with its
    // features; one of protocol 1 says nothing, but it usually complains about "uci" first.
    (void)process.writeInput("uci\n");
    std::string protocol;
    bool complained   = false;
    auto xboard_since = steady_clock::time_point{};
    const auto start  = steady_clock::now();
    std::vector<fastchess::engine::process::Line> lines;
    while (protocol.empty()) {
        const auto now = steady_clock::now();
        if (xboard_since != steady_clock::time_point{} && now - xboard_since > seconds(3)) {
            protocol = complained ? "xboard" : "uci";
            break;
        }
        if (xboard_since == steady_clock::time_point{} && (complained || now - start > seconds(10))) {
            (void)process.writeInput("xboard\nprotover 2\n");
            xboard_since = now;
        }
        const auto r = process.readOutput(lines, "uciok", milliseconds(200));
        for (const auto& l : lines) {
            if (l.line.rfind("uciok", 0) == 0) protocol = "uci";
            if (l.line.rfind("feature ", 0) == 0) protocol = "xboard";
            const auto lower = [&] {
                std::string s = l.line;
                for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return s;
            }();
            if (lower.find("illegal") != std::string::npos || lower.find("error") != std::string::npos ||
                lower.find("unknown") != std::string::npos || lower.find("invalid") != std::string::npos) {
                complained = true;
            }
        }
        if (r.code == Status::ERR) break;  // it stopped: let the probe tell why
    }
    (void)process.writeInput("quit\n");
    return protocol.empty() ? "uci" : protocol;
}

namespace {

// The last lines an engine wrote on its output, kept as they come: the engine keeps only those of its last read, and
// not those of isready.
class Transcript {
   public:
    explicit Transcript(fastchess::engine::UciEngine& engine) : engine_(engine) {
        engine.setLineCallback([this](const std::string& line) {
            lines_.push_back(line);
            if (lines_.size() > LINES) lines_.pop_front();
        });
    }
    ~Transcript() { engine_.setLineCallback({}); }
    Transcript(const Transcript&)            = delete;
    Transcript& operator=(const Transcript&) = delete;

    // For a report, then what the engine wrote last on its error output; empty when it wrote nothing. What is still
    // to read is read first: an engine that ended before it was asked anything (a DLL missing) failed the "uci" sent
    // to it, so its last words were never read.
    [[nodiscard]] std::string report() {
        (void)engine_.readEngine("", std::chrono::milliseconds(200));
        std::string report;
        if (!lines_.empty()) report += std::string(TR("The engine wrote:")) + "\n";
        for (const auto& line : lines_) report += "    " + toUtf8(line) + "\n";
        const auto errors = engine_.getStderrLines();
        if (!errors.empty()) report += std::string(TR("On its error output:")) + "\n";
        for (std::size_t i = errors.size() > LINES ? errors.size() - LINES : 0; i < errors.size(); ++i)
            report += "    " + toUtf8(errors[i]->line) + "\n";
        return report;
    }

   private:
    static constexpr std::size_t LINES = 40;
    fastchess::engine::UciEngine& engine_;
    std::deque<std::string> lines_;
};

}  // namespace

ProbeResult probeEngine(const EngineEntry& entry, const std::atomic_bool* interrupted) {
    ProbeResult result;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(entry.cmd, ec)) {
        result.error = trf("File not found: {}", entry.cmd);
        return result;
    }

    result.program  = tour::fingerprint(entry.cmd);
    result.protocol = entry.protocol.empty() ? detectProtocol(entry) : entry.protocol;
    // Interrupted while the protocol was found out: what it found is not the engine's.
    if (interrupted && *interrupted) {
        result.error = "interrupted";
        return result;
    }

    fastchess::EngineConfiguration config;
    config.name     = entry.name.empty() ? std::filesystem::path(entry.cmd).stem().string() : entry.name;
    config.cmd      = entry.cmd;
    config.args     = entry.args;
    config.dir      = engineWorkingDir(entry);
    config.protocol = result.protocol;

    config.start_timeout_ms = entry.start_timeout * 1000LL;

    try {
        fastchess::engine::UciEngine engine(config, false);
        Transcript transcript(engine);

        if (auto started = engine.start(std::nullopt); !started) {
            result.error  = toUtf8(started.error());
            result.output = transcript.report();
            // The usual reason: a WinBoard engine taken for a UCI one.
            if (result.error.find("uciok") != std::string::npos && result.protocol != "xboard") {
                result.error += TR(" — not a UCI engine? For a WinBoard engine, choose the WinBoard protocol.");
            }
            return result;
        }

        result.id_name   = toUtf8(engine.idName().value_or(""));
        result.id_author = toUtf8(engine.idAuthor().value_or(""));

        for (const auto* line : engine.getStdoutLines()) {
            if (auto option = parseUciOption(toUtf8(line->line))) result.options.push_back(std::move(*option));
            if (line->line == wb2uci::NO_REUSE_INFO) result.restart = true;
        }

        result.ok = true;
    } catch (const std::exception& e) {
        result.error = toUtf8(e.what());
    }

    return result;
}

bool sameEngine(const EngineEntry& a, const EngineEntry& b) {
#ifdef _WIN32
    const bool same_program = std::equal(a.cmd.begin(), a.cmd.end(), b.cmd.begin(), b.cmd.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
#else
    const bool same_program = a.cmd == b.cmd;
#endif
    return same_program && a.args == b.args;
}

std::string engineSearchText(const EngineEntry& entry) {
    return entry.id_name + '\n' + entry.id_author + '\n' + entry.cmd + '\n' + entry.notes;
}

bool engineMatches(const EngineEntry& entry, const std::string& needle) {
    if (needle.empty()) return true;
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const auto n = lower(needle);
    return lower(entry.name).find(n) != std::string::npos || lower(engineSearchText(entry)).find(n) != std::string::npos;
}

bool engineWorks(const EngineEntry& entry) {
    const auto state = entry.program_state;
    return entry.check_ok == 1 && state != ProgramState::Missing && state != ProgramState::MissingDir &&
           state != ProgramState::Changed;
}

std::vector<std::string> suggestOpponents(const std::vector<EngineEntry>& engines, int elo, int range, int count,
                                          const std::vector<std::string>& exclude) {
    std::vector<const EngineEntry*> candidates;
    for (const auto& e : engines) {
        if (e.inactive || e.elo <= 0 || !engineWorks(e) || std::abs(e.elo - elo) > range) continue;
        if (std::find(exclude.begin(), exclude.end(), e.name) != exclude.end()) continue;
        candidates.push_back(&e);
    }
    std::sort(candidates.begin(), candidates.end(), [&](const EngineEntry* a, const EngineEntry* b) {
        const int da = std::abs(a->elo - elo), db = std::abs(b->elo - elo);
        return da != db ? da < db : a->name < b->name;
    });
    std::vector<std::string> out;
    for (const auto* e : candidates) {
        if (static_cast<int>(out.size()) >= count) break;
        out.push_back(e->name);
    }
    return out;
}

std::map<std::string, std::string> libraryAliases(const std::vector<EngineEntry>& engines) {
    std::map<std::string, std::string> out;
    std::set<std::string> taken;  // folded
    for (const auto& e : engines) {
        for (const auto& alias : e.aliases) {
            if (taken.insert(tour::foldedName(alias)).second) out.emplace(alias, e.name);
        }
    }
    return out;
}

std::string libraryAlias(const std::vector<EngineEntry>& engines, const std::string& player) {
    const auto key = tour::foldedName(player);
    for (const auto& e : engines) {
        for (const auto& alias : e.aliases) {
            if (tour::foldedName(alias) == key) return e.name;
        }
    }
    return {};
}

bool setLibraryAlias(std::vector<EngineEntry>& engines, const std::string& player, const std::string& engine) {
    const auto key = tour::foldedName(player);
    bool changed   = false;
    for (auto& e : engines) {
        if (e.name == engine) continue;
        changed |= std::erase_if(e.aliases, [&](const std::string& a) { return tour::foldedName(a) == key; }) > 0;
    }
    for (auto& e : engines) {
        if (e.name != engine) continue;
        const bool has = std::any_of(e.aliases.begin(), e.aliases.end(),
                                     [&](const std::string& a) { return tour::foldedName(a) == key; });
        if (!has) {
            e.aliases.push_back(player);
            changed = true;
        }
    }
    return changed;
}

bool isWinboard(const EngineEntry& entry) {
    if (entry.protocol == "xboard") return true;
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return lower(entry.cmd + " " + entry.args).find("wb2uci") != std::string::npos;
}

namespace {

struct Compliance {
    bool passed = true;
    int steps   = 0;  // passed
    std::string report;
};

// fastchess' compliance steps on a new process of the engine, up to the first that fails, one line each; then what the
// engine wrote last.
Compliance compliance(const fastchess::EngineConfiguration& config) {
    fastchess::engine::UciEngine engine(config, false);
    Transcript transcript(engine);
    auto steps = fastchess::engine::complianceSteps(engine);
    // Its options, sent as a game sends them, once it started.
    if (!config.options.empty())
        steps.insert(steps.begin() + 1, {TR("Send its options"), [&engine] { return engine.refreshUci(); }});
    Compliance result;
    auto& report = result.report;
    for (const auto& [description, action] : steps) {
        if (action()) {
            report += fmt::format("✓ {}\n", description);
            result.steps++;
            continue;
        }
        result.passed = false;
        report += fmt::format("✗ {}\n", description);
        report += transcript.report();
        break;
    }
    engine.quit();
    return result;
}

}  // namespace

CheckResult checkEngine(const EngineEntry& entry, const std::atomic_bool* interrupted) {
    using namespace std::chrono;
    CheckResult result;

    fastchess::EngineConfiguration config;
    config.name     = entry.name.empty() ? std::filesystem::path(entry.cmd).stem().string() : entry.name;
    config.cmd      = entry.cmd;
    config.args     = entry.args;
    config.dir      = engineWorkingDir(entry);
    config.options  = tour::forEngine(entry.options);
    config.protocol = entry.protocol;

    config.start_timeout_ms = entry.start_timeout * 1000LL;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(entry.cmd, ec)) {
        result.message = trf("File not found: {}", entry.cmd);
        return result;
    }

    try {
        fastchess::engine::UciEngine engine(config, false);
        Transcript transcript(engine);
        // A failure comes with what the engine wrote last.
        const auto fail = [&](std::string message) {
            result.message = std::move(message);
            result.report  = transcript.report();
            return result;
        };

        const auto t0 = steady_clock::now();
        if (auto started = engine.start(std::nullopt); !started) return fail(toUtf8(started.error()));
        const auto start_ms = duration_cast<milliseconds>(steady_clock::now() - t0).count();

        // Its options, as games send them: fastchess only warns, then plays without, about an option the engine does
        // not have or a value it refuses. A wrong path (EvalFile, SyzygyPath) often makes it stop.
        std::string refused;
        for (const auto& [name, value] : config.options) {
            const auto option = engine.uciOptions().getOption(name);
            if (!option) {
                refused += (refused.empty() ? "" : "; ") + trf("{}: unknown to the engine", name);
            } else if (!(*option)->isValid(value)) {
                refused += (refused.empty() ? "" : "; ") + trf("{} = {}: a value it refuses", name, value);
            }
        }
        if (!refused.empty()) return fail(trf("Options not sent: {}", refused));
        if (!config.options.empty() && !engine.refreshUci())
            return fail(TR("The engine stopped answering once its options were sent."));

        const auto t1 = steady_clock::now();
        if (engine.isready().code != fastchess::engine::process::Status::OK)
            return fail(TR("The engine does not answer isready."));
        const auto ready_ms = duration_cast<microseconds>(steady_clock::now() - t1).count() / 1000.0;

        // A short search from the initial position: the move must be legal.
        chess::Board board;
        (void)engine.ucinewgame();
        if (!engine.position({}, std::string(chess::constants::STARTPOS)) || !engine.writeEngine("go movetime 300"))
            return fail(TR("The engine stopped while it was asked to think."));
        engine.readEngine("bestmove", seconds(10));
        const auto best = engine.bestmove(false);
        if (!best) return fail(TR("No move after 10 seconds of a 0.3 s search."));
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        const auto move = chess::uci::uciToMove(board, *best);
        if (std::find(legal.begin(), legal.end(), move) == legal.end())
            return fail(trf("Illegal move from the initial position: {}", *best));

        engine.quit();
        result.ok      = true;
        result.message = trf("Starts in {} ms · isready {:.1f} ms · first move {}", start_ms, ready_ms, *best);

        if (interrupted && *interrupted) return fail("interrupted");
        if (config.protocol != "xboard") {
            const auto uci = compliance(config);
            result.report  = uci.report;
            result.ok      = uci.passed;
            result.message = uci.passed ? result.message + trf(" · UCI: {} steps passed", uci.steps)
                                        : trf("UCI compliance: step {} failed, see the report", uci.steps + 1);
        }
    } catch (const std::exception& e) {
        result.message = toUtf8(e.what());
    }
    return result;
}

OptionPath optionPath(const UciOptionInfo& option) {
    if (option.type != "string") return OptionPath::None;
    auto name = option.name;
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto has = [&](const char* word) { return name.find(word) != std::string::npos; };
    if (has("path") || has("dir") || has("folder")) return OptionPath::Folder;
    if (has("file") || has("book") || has("network") || has("weights")) return OptionPath::File;
    return OptionPath::None;
}

void showWdlByDefault(EngineEntry& entry) {
    const bool has = std::any_of(entry.uci_options.begin(), entry.uci_options.end(),
                                 [](const UciOptionInfo& o) { return o.name == "UCI_ShowWDL" && o.type == "check"; });
    const bool set = std::any_of(entry.options.begin(), entry.options.end(),
                                 [](const auto& kv) { return kv.first == "UCI_ShowWDL"; });
    if (has && !set) entry.options.emplace_back("UCI_ShowWDL", "true");
}

void pruneOptions(EngineEntry& entry) {
    if (entry.uci_options.empty()) return;

    std::erase_if(entry.options, [&](const auto& kv) {
        for (const auto& option : entry.uci_options) {
            if (option.name != kv.first) continue;
            if (option.type == "button") return kv.second != "true";
            return option.default_value == kv.second;
        }
        return true;
    });
}

}  // namespace fcg
