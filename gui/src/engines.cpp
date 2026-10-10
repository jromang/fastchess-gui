#include "engines.hpp"
#include "i18n.hpp"

#include <filesystem>
#include <sstream>

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

std::string engineWorkingDir(const EngineEntry& entry) {
    if (!entry.dir.empty()) return entry.dir;
    return std::filesystem::path(entry.cmd).parent_path().string();
}

ProbeResult probeEngine(const EngineEntry& entry) {
    ProbeResult result;

    fastchess::EngineConfiguration config;
    config.name = entry.name.empty() ? std::filesystem::path(entry.cmd).stem().string() : entry.name;
    config.cmd  = entry.cmd;
    config.args = entry.args;
    config.dir  = engineWorkingDir(entry);

    if (!std::filesystem::is_regular_file(entry.cmd)) {
        result.error = trf("File not found: {}", entry.cmd);
        return result;
    }

    try {
        fastchess::engine::UciEngine engine(config, false);

        if (auto started = engine.start(std::nullopt); !started) {
            result.error = started.error();
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
