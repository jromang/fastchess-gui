#include "engine_import.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

#include <json.hpp>

#include "engines.hpp"
#include "i18n.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n\"");
    if (a == std::string::npos) return "";
    const auto b = s.find_last_not_of(" \t\r\n\"");
    return s.substr(a, b - a + 1);
}

// Lists written by Windows programs are often in Windows-1252: names and paths with accents become UTF-8.
std::string readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return toUtf8({std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()});
}

// Windows paths in lists written on Windows, read anywhere.
fs::path resolve(const fs::path& base, std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    fs::path p(path);
    return p.is_absolute() || (path.size() > 1 && path[1] == ':') ? p : base / p;
}

bool isWinboard(const std::string& protocol) {
    const auto p = lower(protocol);
    return p.find("xboard") != std::string::npos || p.find("winboard") != std::string::npos ||
           p.find("cecp") != std::string::npos || p == "wb" || p == "wb1" || p == "wb2";
}

std::string jsonText(const nlohmann::json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number()) return std::to_string(v.get<double>());
    return "";
}

// Any object with a name and a command, wherever it is in the document (cutechess, Banksia, others).
void collectJson(const nlohmann::json& node, const fs::path& base, ImportResult& out) {
    if (node.is_array()) {
        for (const auto& item : node) collectJson(item, base, out);
        return;
    }
    if (!node.is_object()) return;

    std::string command;
    for (const char* key : {"command", "cmd", "path", "app", "file"}) {
        if (node.contains(key) && node[key].is_string()) {
            command = node[key].get<std::string>();
            break;
        }
    }
    if (!command.empty() && node.contains("name")) {
        EngineEntry e;
        e.name              = jsonText(node["name"]);
        const auto protocol = node.contains("protocol") ? jsonText(node["protocol"]) : "uci";
        e.protocol          = isWinboard(protocol) ? "xboard" : "uci";
        // cutechess puts arguments after the program in the command; a quoted program may contain spaces.
        std::string program = command, args;
        if (!command.empty() && command.front() == '"') {
            const auto close = command.find('"', 1);
            program          = command.substr(1, close == std::string::npos ? std::string::npos : close - 1);
            if (close != std::string::npos && close + 1 < command.size()) args = trim(command.substr(close + 1));
        } else if (const auto space = command.find(' ');
                   space != std::string::npos && !fs::exists(resolve(base, command))) {
            program = command.substr(0, space);
            args    = trim(command.substr(space + 1));
        }
        for (const char* key : {"workingDirectory", "working directory", "workingFolder", "dir"}) {
            if (node.contains(key) && node[key].is_string())
                e.dir = resolve(base, node[key].get<std::string>()).string();
        }
        const auto from = e.dir.empty() ? base : fs::path(e.dir);
        e.cmd           = resolve(from, program).lexically_normal().string();
        e.args          = args;
        if (node.contains("arguments") && node["arguments"].is_array()) {
            for (const auto& a : node["arguments"]) e.args += (e.args.empty() ? "" : " ") + jsonText(a);
        }
        if (node.contains("options") && node["options"].is_array()) {
            for (const auto& o : node["options"]) {
                if (!o.is_object() || !o.contains("name")) continue;
                const auto& value = o.contains("value") ? o["value"] : o.contains("default") ? o["default"] : o["name"];
                if (o.contains("value")) e.options.emplace_back(jsonText(o["name"]), jsonText(value));
            }
        }
        out.engines.push_back(std::move(e));
        return;
    }
    for (const auto& [key, value] : node.items()) collectJson(value, base, out);
}

// The keys of a section in the order of the file, the order of the engines in Arena's list.
using Keys = std::vector<std::pair<std::string, std::string>>;
using Ini  = std::vector<std::pair<std::string, Keys>>;

Ini parseIni(const std::string& text) {
    Ini ini;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            ini.emplace_back(line.substr(1, line.size() - 2), Keys{});
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos || ini.empty()) continue;
        auto key   = trim(line.substr(0, eq));
        auto value = trim(line.substr(eq + 1));
        // A key given twice keeps its last value.
        auto& keys    = ini.back().second;
        const auto it = std::find_if(keys.begin(), keys.end(), [&](const auto& kv) { return kv.first == key; });
        if (it != keys.end())
            it->second = std::move(value);
        else
            keys.emplace_back(std::move(key), std::move(value));
    }
    return ini;
}

const std::string* find(const Keys& keys, std::initializer_list<const char*> names) {
    for (const char* n : names) {
        for (const auto& [k, v] : keys) {
            if (lower(k) == lower(n) && !v.empty()) return &v;
        }
    }
    return nullptr;
}

// Arena's %ArenaPath% (its folder, where ArenaENG.cfg is) and %ArenaDrive% (the drive of that folder), in any case.
std::string arenaPath(std::string text, const fs::path& base) {
    const std::pair<std::string, std::string> names[] = {{"%arenapath%", base.string()},
                                                         {"%arenadrive%", base.root_name().string()}};
    for (const auto& [name, value] : names) {
        for (auto at = lower(text).find(name); at != std::string::npos; at = lower(text).find(name, at + value.size()))
            text.replace(at, name.size(), value);
    }
    return text;
}

// Arena's ArenaENG.cfg: an [Engines] section with a line per engine, Name=program|arguments|number|author|type|
// folder|country|..., the type 1 for UCI, 2 for WinBoard, 3 for the old WinBoard 1 and 0 when Arena finds out. The
// section named after a UCI engine holds the values of its options, <empty> for an empty text. False when the list
// is not Arena's.
bool collectArena(const Ini& ini, const fs::path& base, ImportResult& out) {
    const auto engines = std::find_if(ini.begin(), ini.end(), [](const auto& section) {
        return lower(section.first) == "engines" &&
               std::any_of(section.second.begin(), section.second.end(),
                           [](const auto& kv) { return kv.second.find('|') != std::string::npos; });
    });
    if (engines == ini.end()) return false;

    std::error_code ec;
    const auto folder = fs::absolute(base, ec);
    for (const auto& [name, line] : engines->second) {
        std::vector<std::string> fields;
        std::istringstream in(line);
        for (std::string field; std::getline(in, field, '|');) fields.push_back(trim(field));
        if (fields.empty() || fields[0].empty()) continue;
        const auto field = [&](std::size_t i) { return i < fields.size() ? arenaPath(fields[i], folder) : ""; };

        EngineEntry e;
        e.name = name;
        e.cmd  = resolve(folder, field(0)).lexically_normal().string();
        e.args = field(1);
        if (const auto type = field(4); type == "1")
            e.protocol = "uci";
        else if (type == "2" || type == "3")
            e.protocol = "xboard";
        // The folder Arena starts it in, when it is set and still there.
        if (const auto dir = field(5); !dir.empty()) {
            auto path = resolve(folder, dir).lexically_normal();
            if (!path.has_filename() && path.has_relative_path()) path = path.parent_path();
            if (fs::is_directory(path, ec)) e.dir = path.string();
        }
        // The section of a WinBoard engine holds Arena's settings for it, not options.
        if (e.protocol == "uci") {
            for (const auto& [section, keys] : ini) {
                if (section != name) continue;
                for (const auto& [k, v] : keys) e.options.emplace_back(k, v == "<empty>" ? "" : arenaPath(v, folder));
            }
        }
        out.engines.push_back(std::move(e));
    }
    return true;
}

// Sections with a program path: ChessGUI's ChsGUI_Eng.TXT (EngineFile), and others (Command, Filename...).
void collectIni(const Ini& ini, const fs::path& base, ImportResult& out) {
    // ChessGUI keeps the UCI options of its engines in ChsGUI_Cfg.ini, by engine name.
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> chessgui_options;
    for (const auto& name : {"ChsGUI_Cfg.ini", "ChsGUI_Cfg.INI", "GUI_DATA/ChsGUI_Cfg.ini"}) {
        if (fs::exists(base / name)) {
            for (const auto& [section, keys] : parseIni(readFile(base / name))) {
                for (const auto& [k, v] : keys) chessgui_options[section].emplace_back(k, v);
            }
            break;
        }
    }

    for (const auto& [section, keys] : ini) {
        const auto* file = find(keys, {"EngineFile", "Command", "Filename", "File", "Path", "Executable", "Exe"});
        if (!file) continue;
        const auto* proto = find(keys, {"Protocol", "Type"});
        const auto* name  = find(keys, {"MyName", "Name"});

        EngineEntry e;
        e.name = name ? *name : section;
        // ChessGUI's lists do not say: the probe after the import finds out.
        if (proto) e.protocol = isWinboard(*proto) ? "xboard" : "uci";
        e.cmd = resolve(base, *file).lexically_normal().string();
        if (const auto* args = find(keys, {"CommandLine", "Arguments", "Parameters"})) e.args = *args;
        if (const auto* dir = find(keys, {"WorkingDirectory", "Directory", "Dir"}))
            e.dir = resolve(base, *dir).string();
        if (const auto it = chessgui_options.find(e.name); it != chessgui_options.end()) e.options = it->second;
        if (const auto it = chessgui_options.find(section); it != chessgui_options.end() && e.options.empty()) {
            e.options = it->second;
        }
        out.engines.push_back(std::move(e));
    }
}

bool executable(const fs::directory_entry& entry) {
    std::error_code ec;
    if (!entry.is_regular_file(ec)) return false;
    const auto name = lower(entry.path().filename().string());
    for (const char* skip : {"uninst", "setup", "install", "update", "readme", "polyglot"}) {
        if (name.find(skip) != std::string::npos) return false;
    }
#ifdef _WIN32
    return lower(entry.path().extension().string()) == ".exe";
#else
    const auto perms = entry.status(ec).permissions();
    const auto ext   = lower(entry.path().extension().string());
    return (perms & fs::perms::owner_exec) != fs::perms::none && ext != ".sh" && ext != ".py" && ext != ".so";
#endif
}

}  // namespace

ImportResult importEngineList(const fs::path& file) {
    ImportResult out;
    const auto text = readFile(file);
    const auto base = file.parent_path();
    if (text.empty()) {
        out.notes.push_back(trf("Cannot read {}", file.string()));
        return out;
    }

    const auto first = text.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    bool json        = false;
    if (first != std::string::npos && (text[first] == '{' || text[first] == '[') && text.compare(first, 2, "[E") != 0) {
        try {
            collectJson(nlohmann::json::parse(text.substr(first)), base, out);
            json = true;
        } catch (const std::exception&) {
            // Not JSON after all: an INI file starting with a section.
            out.engines.clear();
        }
    }
    bool arena = false;
    if (!json) {
        const auto ini = parseIni(text);
        arena          = collectArena(ini, base, out);
        if (!arena) collectIni(ini, base, out);
    }

    const auto missing = [](const EngineEntry& e) {
        std::error_code ec;
        return !fs::is_regular_file(e.cmd, ec);
    };
    for (const auto& e : out.engines) {
        if (missing(e)) out.notes.push_back(trf("{}: program not found ({})", e.name, e.cmd));
    }
    // Arena's list keeps the engines deleted or moved since, its bundled ones too: those are left out.
    if (arena) std::erase_if(out.engines, missing);
    return out;
}

ImportResult findEngines(const fs::path& folder) {
    ImportResult out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it.depth() > 2) {
            it.disable_recursion_pending();
            continue;
        }
        if (!executable(*it)) continue;
        EngineEntry e;
        e.name = it->path().stem().string();
        e.cmd  = it->path().string();
        out.engines.push_back(std::move(e));
    }
    std::sort(out.engines.begin(), out.engines.end(),
              [](const EngineEntry& a, const EngineEntry& b) { return a.cmd < b.cmd; });
    return out;
}

}  // namespace fcg
