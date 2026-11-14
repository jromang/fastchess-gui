#include "engine_changes.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>

namespace fcg {

namespace {

const UciOptionInfo* findOption(const std::vector<UciOptionInfo>& options, const std::string& name) {
    const auto it = std::find_if(options.begin(), options.end(), [&](const auto& o) { return o.name == name; });
    return it == options.end() ? nullptr : &*it;
}

// The same description: a button has no value, so its default does not count.
bool sameOption(const UciOptionInfo& a, const UciOptionInfo& b) {
    if (a.type != b.type) return false;
    if (a.type == "button") return true;
    return a.default_value == b.default_value && a.min == b.min && a.max == b.max && a.vars == b.vars;
}

std::optional<long long> wholeNumber(const std::string& text) {
    long long v       = 0;
    const auto* end   = text.data() + text.size();
    const auto [p, e] = std::from_chars(text.data(), end, v);
    if (e != std::errc() || p != end || text.empty()) return std::nullopt;
    return v;
}

bool equalNoCase(const std::string& a, const std::string& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

bool lessNoCase(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

// The order the card lists the options in: those waiting for a choice, those described otherwise, new, gone.
int rank(const OptionChange& c) {
    if (needsChoice(c)) return 0;
    if (c.before.name.empty()) return 2;
    if (c.after.name.empty()) return 3;
    return 1;
}

// Closes the notice once nothing is left in it to decide or to read.
void closeIfDone(EngineEntry& e) {
    if (e.changes && choicesLeft(*e.changes) == 0 && !tellsMore(*e.changes)) e.changes.reset();
}

bool choose(EngineEntry& e, OptionChange& row, ValueChoice choice) {
    if (!needsChoice(row) || row.choice != 0) return false;
    if (choice == ValueChoice::Keep) {
        const auto& played = row.has_value ? row.value : row.before.default_value;
        if (played == row.after.default_value)
            e.clearOption(row.name);
        else
            e.setOption(row.name, played);
    } else {
        e.clearOption(row.name);
    }
    row.choice = static_cast<int>(choice);
    return true;
}

}  // namespace

EngineRead lastRead(const EngineEntry& e) { return {e.id_name, e.id_author, e.uci_options}; }

bool otherProgram(const tour::FileFingerprint& a, const tour::FileFingerprint& b) {
    if (a.size == 0 || b.size == 0) return false;
    return a.size != b.size || a.crc32 != b.crc32;
}

bool fitsOption(const UciOptionInfo& option, const std::string& value) {
    if (option.type == "spin") {
        const auto v = wholeNumber(value);
        if (!v) return false;
        if (const auto lo = wholeNumber(option.min); lo && *v < *lo) return false;
        if (const auto hi = wholeNumber(option.max); hi && *v > *hi) return false;
        return true;
    }
    if (option.type == "check") return value == "true" || value == "false";
    if (option.type == "combo") {
        return std::any_of(option.vars.begin(), option.vars.end(),
                           [&](const std::string& var) { return equalNoCase(var, value); });
    }
    return true;
}

ProgramChanges compareReads(const EngineRead& before, const EngineRead& after,
                            const std::vector<std::pair<std::string, std::string>>& values) {
    ProgramChanges changes;
    changes.id_name_before   = before.id_name;
    changes.id_name_after    = after.id_name;
    changes.id_author_before = before.id_author;
    changes.id_author_after  = after.id_author;

    const auto row = [&](const std::string& name) {
        OptionChange c;
        c.name        = name;
        const auto it = std::find_if(values.begin(), values.end(), [&](const auto& kv) { return kv.first == name; });
        if (it != values.end()) {
            c.value     = it->second;
            c.has_value = true;
        }
        return c;
    };
    for (const auto& now : after.options) {
        const auto* was = findOption(before.options, now.name);
        if (was && sameOption(*was, now)) continue;
        auto c = row(now.name);
        if (was) c.before = *was;
        c.after = now;
        changes.options.push_back(std::move(c));
    }
    for (const auto& was : before.options) {
        if (findOption(after.options, was.name)) continue;
        auto c   = row(was.name);
        c.before = was;
        changes.options.push_back(std::move(c));
    }
    std::stable_sort(changes.options.begin(), changes.options.end(), [](const auto& a, const auto& b) {
        const int ra = rank(a), rb = rank(b);
        return ra != rb ? ra < rb : lessNoCase(a.name, b.name);
    });
    return changes;
}

EngineRead readBefore(const ProgramChanges& changes, const EngineRead& now) {
    EngineRead before = now;
    before.id_name    = changes.id_name_before;
    before.id_author  = changes.id_author_before;
    auto& options     = before.options;
    for (const auto& c : changes.options) {
        if (c.choice != 0) continue;
        const auto it = std::find_if(options.begin(), options.end(), [&](const auto& o) { return o.name == c.name; });
        if (c.before.name.empty()) {
            if (it != options.end()) options.erase(it);
        } else if (it == options.end()) {
            options.push_back(c.before);
        } else {
            *it = c.before;
        }
    }
    return before;
}

bool noteChanges(EngineEntry& e, const EngineRead& after, const std::string& when) {
    auto before = lastRead(e);
    auto values = e.options;
    if (e.changes) {
        before = readBefore(*e.changes, before);
        // The values of the options gone with an earlier build, dropped from the library since: still shown.
        for (const auto& c : e.changes->options) {
            if (c.after.name.empty() && c.has_value && !e.option(c.name)) values.emplace_back(c.name, c.value);
        }
    }
    auto changes = compareReads(before, after, values);
    if (changes.empty()) {
        e.changes.reset();
        return false;
    }
    changes.when = when;
    e.changes    = std::move(changes);
    return true;
}

bool needsChoice(const OptionChange& c) {
    if (c.before.name.empty() || c.after.name.empty() || c.after.type == "button") return false;
    const auto& played = c.has_value ? c.value : c.before.default_value;
    if (played == c.after.default_value) return false;
    return c.before.default_value != c.after.default_value || (c.has_value && !fitsOption(c.after, c.value));
}

int choicesLeft(const ProgramChanges& changes) {
    return static_cast<int>(std::count_if(changes.options.begin(), changes.options.end(),
                                          [](const auto& c) { return c.choice == 0 && needsChoice(c); }));
}

bool tellsMore(const ProgramChanges& changes) {
    if (changes.nameChanged() || changes.authorChanged()) return true;
    return std::any_of(changes.options.begin(), changes.options.end(), [](const auto& c) { return !needsChoice(c); });
}

bool chooseValue(EngineEntry& e, const std::string& option, ValueChoice choice) {
    if (!e.changes) return false;
    bool changed = false;
    for (auto& row : e.changes->options) {
        if (row.name == option) changed |= choose(e, row, choice);
    }
    closeIfDone(e);
    return changed;
}

bool chooseAllValues(EngineEntry& e, ValueChoice choice) {
    if (!e.changes) return false;
    bool changed = false;
    for (auto& row : e.changes->options) changed |= choose(e, row, choice);
    closeIfDone(e);
    return changed;
}

std::string programKey(const std::string& cmd, const std::string& args) {
    auto key = cmd;
#ifdef _WIN32
    for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    return key + '\n' + args;
}

std::map<std::string, PlayedProgram> lastPlayedPrograms(const std::vector<tour::Summary>& tournaments) {
    std::map<std::string, PlayedProgram> out;
    for (const auto& t : tournaments) {
        for (const auto& p : t.spec.participants) {
            if (p.program.size == 0) continue;
            out.try_emplace(programKey(p.engine.cmd, p.engine.args), PlayedProgram{p.program, t.spec.name, t.dir});
        }
    }
    return out;
}

const PlayedProgram* rebuiltSincePlayed(const EngineEntry& e, const std::map<std::string, PlayedProgram>& played) {
    if (e.program_state == ProgramState::Missing || e.program_state == ProgramState::MissingDir) return nullptr;
    const auto it = played.find(programKey(e.cmd, e.args));
    if (it == played.end()) return nullptr;
    if (e.program_state == ProgramState::Changed || otherProgram(e.program, it->second.program)) return &it->second;
    return nullptr;
}

std::vector<ParticipantProgram> changedPrograms(const tour::TournamentSpec& spec) {
    std::vector<ParticipantProgram> out;
    for (const auto& p : spec.participants) {
        if (!p.active || p.program.size == 0 || !tour::changedSince(p.engine.cmd, p.program)) continue;
        std::error_code ec;
        out.push_back({p.id, p.engine.name, p.engine.cmd, !std::filesystem::is_regular_file(p.engine.cmd, ec)});
    }
    return out;
}

}  // namespace fcg
