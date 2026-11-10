#include "engines.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <sstream>

#include <engine/compliance.hpp>
#include <engine/uci_engine.hpp>

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

bool refreshProgram(EngineEntry& entry) {
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
    if (entry.program.size == 0) {
        entry.program       = tour::fingerprint(entry.cmd);
        entry.program_state = ProgramState::Ok;
        return true;
    }
    if (tour::changedSince(entry.cmd, entry.program)) {
        entry.program_state = ProgramState::Changed;
        return false;
    }
    entry.program_state = ProgramState::Ok;
    // Same contents under a new date (copied, touched): remember the date, so the CRC is not read every time.
    const auto mtime = fs::last_write_time(entry.cmd, ec).time_since_epoch().count();
    if (ec || mtime == entry.program.mtime) return false;
    entry.program.mtime = mtime;
    return true;
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

ProbeResult probeEngine(const EngineEntry& entry) {
    ProbeResult result;

    if (!std::filesystem::is_regular_file(entry.cmd)) {
        result.error = trf("File not found: {}", entry.cmd);
        return result;
    }

    result.program  = tour::fingerprint(entry.cmd);
    result.protocol = entry.protocol.empty() ? detectProtocol(entry) : entry.protocol;

    fastchess::EngineConfiguration config;
    config.name     = entry.name.empty() ? std::filesystem::path(entry.cmd).stem().string() : entry.name;
    config.cmd      = entry.cmd;
    config.args     = entry.args;
    config.dir      = engineWorkingDir(entry);
    config.protocol = result.protocol;

    try {
        fastchess::engine::UciEngine engine(config, false);

        if (auto started = engine.start(std::nullopt); !started) {
            result.error = started.error();
            // The usual reason: a WinBoard engine taken for a UCI one.
            if (result.error.find("uciok") != std::string::npos && result.protocol != "xboard") {
                result.error += TR(" — not a UCI engine? For a WinBoard engine, choose the WinBoard protocol.");
            }
            return result;
        }

        result.id_name   = engine.idName().value_or("");
        result.id_author = engine.idAuthor().value_or("");

        for (const auto* line : engine.getStdoutLines()) {
            if (auto option = parseUciOption(line->line)) result.options.push_back(std::move(*option));
        }

        result.ok = true;
    } catch (const std::exception& e) {
        result.error = e.what();
    }

    return result;
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
    constexpr std::size_t OUTPUT_LINES = 40;
    fastchess::engine::UciEngine engine(config, false);
    Compliance result;
    auto& report = result.report;
    for (const auto& [description, action] : fastchess::engine::complianceSteps(engine)) {
        if (action()) {
            report += fmt::format("✓ {}\n", description);
            result.steps++;
            continue;
        }
        result.passed = false;
        report += fmt::format("✗ {}\n", description);
        for (const auto standard :
             {fastchess::engine::process::Standard::OUTPUT, fastchess::engine::process::Standard::ERR}) {
            const auto lines = standard == fastchess::engine::process::Standard::OUTPUT ? engine.getStdoutLines()
                                                                                        : engine.getStderrLines();
            if (lines.empty()) continue;
            report += standard == fastchess::engine::process::Standard::OUTPUT ? TR("The engine wrote:")
                                                                               : TR("On its error output:");
            report += "\n";
            for (std::size_t i = lines.size() > OUTPUT_LINES ? lines.size() - OUTPUT_LINES : 0; i < lines.size(); ++i)
                report += "    " + lines[i]->line + "\n";
        }
        break;
    }
    engine.quit();
    return result;
}

}  // namespace

CheckResult checkEngine(const EngineEntry& entry) {
    using namespace std::chrono;
    CheckResult result;

    fastchess::EngineConfiguration config;
    config.name     = entry.name.empty() ? std::filesystem::path(entry.cmd).stem().string() : entry.name;
    config.cmd      = entry.cmd;
    config.args     = entry.args;
    config.dir      = engineWorkingDir(entry);
    config.options  = entry.options;
    config.protocol = entry.protocol;

    if (!std::filesystem::is_regular_file(entry.cmd)) {
        result.message = trf("File not found: {}", entry.cmd);
        return result;
    }

    try {
        fastchess::engine::UciEngine engine(config, false);

        const auto t0 = steady_clock::now();
        if (auto started = engine.start(std::nullopt); !started) {
            result.message = started.error();
            return result;
        }
        const auto start_ms = duration_cast<milliseconds>(steady_clock::now() - t0).count();

        const auto t1 = steady_clock::now();
        if (engine.isready().code != fastchess::engine::process::Status::OK) {
            result.message = TR("The engine does not answer isready.");
            return result;
        }
        const auto ready_ms = duration_cast<microseconds>(steady_clock::now() - t1).count() / 1000.0;

        // A short search from the initial position: the move must be legal.
        chess::Board board;
        (void)engine.ucinewgame();
        if (!engine.position({}, std::string(chess::constants::STARTPOS)) || !engine.writeEngine("go movetime 300")) {
            result.message = TR("The engine stopped while it was asked to think.");
            return result;
        }
        engine.readEngine("bestmove", seconds(10));
        const auto best = engine.bestmove(false);
        if (!best) {
            result.message = TR("No move after 10 seconds of a 0.3 s search.");
            return result;
        }
        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        const auto move = chess::uci::uciToMove(board, *best);
        if (std::find(legal.begin(), legal.end(), move) == legal.end()) {
            result.message = trf("Illegal move from the initial position: {}", *best);
            return result;
        }

        engine.quit();
        result.ok      = true;
        result.message = trf("Starts in {} ms · isready {:.1f} ms · first move {}", start_ms, ready_ms, *best);

        if (config.protocol != "xboard") {
            const auto uci = compliance(config);
            result.report  = uci.report;
            result.ok      = uci.passed;
            result.message = uci.passed ? result.message + trf(" · UCI: {} steps passed", uci.steps)
                                        : trf("UCI compliance: step {} failed, see the report", uci.steps + 1);
        }
    } catch (const std::exception& e) {
        result.message = e.what();
    }
    return result;
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
