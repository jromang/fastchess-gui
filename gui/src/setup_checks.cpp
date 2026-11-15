#include "setup_checks.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <string_view>

#include <fmt/include/fmt/format.h>

#include "engines.hpp"
#include "i18n.hpp"
#include "run.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

const EngineEntry* findEngine(const std::vector<EngineEntry>& engines, const std::string& name) {
    for (const auto& e : engines) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

const UciOptionInfo* uciOption(const EngineEntry& entry, const std::string& name) {
    for (const auto& o : entry.uci_options) {
        if (o.name == name) return &o;
    }
    return nullptr;
}

int concurrencyOf(const MatchSetup& setup, const CpuTopology& topology) {
    const int value = setup.concurrency_auto ? topology.defaultConcurrency(clockFree(setup.limit)) : setup.concurrency;
    return std::clamp(value, 1, std::max(1, topology.logical));
}

// "Alpha", "Alpha and Beta", "Alpha, Beta and Gamma".
std::string names(const std::vector<std::string>& list) {
    if (list.empty()) return {};
    std::string out = list[0];
    for (std::size_t i = 1; i + 1 < list.size(); ++i) out += ", " + list[i];
    return list.size() == 1 ? out : trf("{} and {}", out, list.back());
}

// The first files of a list, then "…" when there are more: "KRPPvKR.rtbw, KRPPvKB.rtbw…".
std::string firstFiles(const std::vector<std::string>& files, const char* extension, std::size_t shown = 3) {
    std::string out;
    for (std::size_t i = 0; i < files.size() && i < shown; ++i) out += (i ? ", " : "") + files[i] + extension;
    if (files.size() > shown) out += "…";
    return out;
}

// The whole list, for the tooltip: a long one is cut.
std::string allFiles(const std::vector<std::string>& files, const char* extension) {
    constexpr std::size_t MOST = 60;
    std::string out;
    for (std::size_t i = 0; i < files.size() && i < MOST; ++i) out += (i ? ", " : "") + files[i] + extension;
    if (files.size() > MOST) out += ", " + trf("and {} more", files.size() - MOST);
    return out;
}

// Why a value is refused by an option, empty when it is not: fastchess sends only the values it takes (see
// UCIOption::isValid).
std::string refusal(const UciOptionInfo& option, const std::string& value) {
    if (option.type == "spin") {
        if (option.min.empty() || option.max.empty()) return {};  // no range given: not judged
        try {
            const double v = std::stod(value), min = std::stod(option.min), max = std::stod(option.max);
            if (v >= min && v <= max) return {};
        } catch (const std::exception&) {
        }
        return trf("its range is {} to {}", option.min, option.max);
    }
    if (option.type == "combo") {
        if (std::find(option.vars.begin(), option.vars.end(), value) != option.vars.end()) return {};
        std::string choices;
        for (const auto& v : option.vars) choices += (choices.empty() ? "" : ", ") + v;
        return trf("its choices are {}", choices);
    }
    if (option.type == "check" && value != "true" && value != "false") return TR("it takes true or false");
    return {};
}

// The folders of a tablebase path as a line of the summary shows them: a long one by its last two folders,
// "…/syzygy/6-men"; the tooltip has them whole.
std::string shortPaths(const std::string& paths) {
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    constexpr std::size_t LONG = 36;
    std::string out;
    for (const auto& dir : splitTablebasePath(paths)) {
        std::string shown = dir;
        auto p            = fs::path(dir).lexically_normal();
        if (!p.has_filename()) p = p.parent_path();
        if (dir.size() > LONG && p.has_parent_path() && !p.parent_path().filename().empty())
            shown = (fs::path("…") / p.parent_path().filename() / p.filename()).string();
        out += (out.empty() ? "" : std::string(1, separator)) + shown;
    }
    return out;
}

// A tablebase path with its relative folders taken from `base`.
std::string absolutePaths(const std::string& paths, const std::string& base) {
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::string out;
    for (const auto& dir : splitTablebasePath(paths)) {
        fs::path p(dir);
        if (p.is_relative() && !base.empty()) p = fs::path(base) / p;
        out += (out.empty() ? "" : std::string(1, separator)) + p.lexically_normal().string();
    }
    return out;
}

}  // namespace

tour::GameCost setupGameCost(const MatchSetup& setup, const std::vector<EngineEntry>& engines) {
    const auto people = setupParticipants(setup);
    const auto hashes = setupHashes(setup, engines);
    std::vector<uint64_t> process;
    tour::GameCost cost;
    int gpus = 0;
    for (std::size_t i = 0; i < people.size(); ++i) {
        const auto* engine = findEngine(engines, people[i].name);
        if (engine && !engine->machine.empty()) {
            process.push_back(tour::PROCESS_MB);  // on its machine: only its relay here
            continue;
        }
        const int threads   = engine ? participantThreads(setup, people[i], *engine).threads : 1;
        const uint64_t hash = i < hashes.size() && hashes[i] > 0 ? hashes[i] : tour::DEFAULT_HASH_MB;
        cost.threads        = std::max(cost.threads, threads);
        process.push_back(hash + tour::PROCESS_MB + tour::THREAD_MB * static_cast<uint64_t>(threads));
        gpus += engine && engine->gpu ? 1 : 0;
    }
    std::sort(process.rbegin(), process.rend());
    for (std::size_t i = 0; i < 2 && i < process.size(); ++i) cost.memory_mb += process[i];
    cost.gpus    = std::min(gpus, 2);
    cost.threads = std::max(1, cost.threads);
    return cost;
}

BudgetLimit budgetLimit(const MatchSetup& setup, const std::vector<EngineEntry>& engines, const tour::Budget& budget,
                        int concurrency) {
    BudgetLimit limit;
    const auto cost = setupGameCost(setup, engines);
    tour::Budget memory;
    memory.threads   = 0;
    memory.memory_mb = budget.memory_mb;
    limit.games      = tour::gamesAllowed(memory, cost, concurrency);
    limit.gpu_games  = concurrency;
    if (cost.gpus > 0) {
        if (budget.gpus == 0) {
            limit.warn = true;
            limit.why  = TR("An engine of it uses a GPU, and no GPU is counted (Settings): games may share one.");
            return limit;
        }
        if (cost.gpus > budget.gpus) {
            limit.warn      = true;
            limit.gpu_games = 1;
            limit.why =
                trn("A game between two engines that use a GPU needs two, and this machine has {} "
                    "(Settings): such a game plays alone.",
                    "A game between two engines that use a GPU needs two, and this machine has {} "
                    "(Settings): such a game plays alone.",
                    budget.gpus, budget.gpus);
            return limit;
        }
        limit.gpu_games = std::min(concurrency, budget.gpus / cost.gpus);
    }
    if (limit.games < concurrency) {
        limit.why =
            trf("The memory budget, {}, lets {} of the {} games play at a time: each takes {} (the Hash of "
                "its engines and their processes). The others wait for it.",
                formatMemory(budget.memory_mb), limit.games, concurrency, formatMemory(cost.memory_mb));
    } else if (limit.gpu_games < concurrency) {
        limit.why =
            trn("The {} GPU lets {} game with an engine that uses a GPU play at a time; the other games play "
                "beside it.",
                "The {} GPUs let {} games with an engine that uses a GPU play at a time; the other games play "
                "beside them.",
                budget.gpus, budget.gpus, limit.gpu_games);
    }
    return limit;
}

int threadsPerGame(const MatchSetup& setup, const CpuTopology& topology) {
    const int hardware = clockFree(setup.limit) ? topology.logical : topology.physical;
    return std::max(1, hardware / concurrencyOf(setup, topology));
}

ParticipantThreads participantThreads(const MatchSetup& setup, const ParticipantSetup& participant,
                                      const EngineEntry& engine) {
    // As buildRun() gives them: only to an engine that has the option, or whose options are not known yet.
    const bool has = engine.uci_options.empty() || uciOption(engine, "Threads");
    if (participant.threads > 0 && has) return {participant.threads, ParticipantThreads::From::Participant};
    if (setup.common_threads > 0 && has) return {setup.common_threads, ParticipantThreads::From::Common};
    return {engine.threads(), ParticipantThreads::From::Engine};
}

std::optional<Notice> threadNotice(const MatchSetup& setup, std::size_t index, const std::vector<EngineEntry>& engines,
                                   const CpuTopology& topology) {
    // Asked for each row of a list of participants at every frame: the list is not copied.
    const bool match  = setup.format == TournamentFormat::Match;
    const auto people = match ? setupParticipants(setup) : std::vector<ParticipantSetup>();
    if (index >= (match ? people : setup.participants).size()) return std::nullopt;
    const auto& participant = match ? people[index] : setup.participants[index];
    const auto* engine      = findEngine(engines, participant.name);
    if (!engine) return std::nullopt;

    const auto [threads, from] = participantThreads(setup, participant, *engine);
    const int per_game         = threadsPerGame(setup, topology);
    const int half             = std::max(1, topology.logical / 2);
    const int games            = concurrencyOf(setup, topology);
    const bool clock           = !clockFree(setup.limit);
    const int lower            = std::min(per_game, half);
    const auto& name           = engine->name;

    Notice n;
    if (from == ParticipantThreads::From::Engine) {
        if (threads <= per_game && threads <= half) return std::nullopt;
        n.text = threads > per_game
                     ? trn("{} plays with {} threads (its Threads option): with {} games at a time, this machine has "
                           "{} thread for each game.",
                           "{} plays with {} threads (its Threads option): with {} games at a time, this machine has "
                           "{} threads for each game.",
                           per_game, name, threads, games, per_game)
                     : trf("{} plays with {} threads (its Threads option), more than half of the {} hardware threads.",
                           name, threads, topology.logical);
        n.detail =
            TR("Some engines take every core by default. Games played at the same time then slow each other "
               "down, and the engines lose on time or play weaker than they are.");
        n.fixes = {lowerEngineThreadsFix(name, lower), openEngineFix(name)};
        return n;
    }

    if (threads <= per_game) return std::nullopt;
    const int hardware  = clock ? topology.physical : topology.logical;
    const auto resource = clock ? trn("{} physical core", "{} physical cores", hardware, hardware)
                                : trn("{} hardware thread", "{} hardware threads", hardware, hardware);
    if (from == ParticipantThreads::From::Common) {
        n.text = trf("Threads {} of the common settings: {} games × {} threads exceed the {}.", threads, games, threads,
                     resource);
        n.fixes = {lowerCommonThreadsFix(per_game)};
    } else {
        n.text = trf("{} plays with {} threads (given by this tournament): {} games × {} threads exceed the {}.", name,
                     threads, games, threads, resource);
        // The Threads cell of a participant of the list; a match has none.
        if (setup.format != TournamentFormat::Match)
            n.fixes = {lowerParticipantThreadsFix(name, static_cast<int>(index), per_game)};
    }
    return n;
}

std::vector<Notice> threadNotices(const MatchSetup& setup, const std::vector<EngineEntry>& engines,
                                  const CpuTopology& topology) {
    std::vector<Notice> out;
    std::set<std::string> seen;  // an engine's own Threads, or the common ones, said once
    bool common       = false;
    const auto people = setupParticipants(setup);
    for (std::size_t i = 0; i < people.size(); ++i) {
        auto n = threadNotice(setup, i, engines, topology);
        if (!n) continue;
        const auto& fix = n->fixes.empty() ? Fix{} : n->fixes.front();
        if (fix.kind == Fix::Kind::LowerEngineThreads && !seen.insert(fix.engine).second) continue;
        if (fix.kind == Fix::Kind::LowerCommonThreads && std::exchange(common, true)) continue;
        out.push_back(std::move(*n));
    }
    return out;
}

std::vector<Notice> programNotices(const MatchSetup& setup, const std::vector<EngineEntry>& engines) {
    std::vector<Notice> out;
    std::set<std::string> seen;  // an engine that plays twice, with two books
    for (const auto& name : enginesOfSetup(setup)) {
        const auto* e = findEngine(engines, name);
        if (!e || !seen.insert(name).second) continue;
        if (e->program_state == ProgramState::Missing) {
            out.push_back({Notice::Level::Error,
                           trf("The program of {} is not found: {}", e->name, e->cmd),
                           {},
                           {openEngineFix(e->name), locateProgramFix(e->name)}});
        } else if (e->program_state == ProgramState::MissingDir) {
            out.push_back({Notice::Level::Error,
                           trf("The working directory of {} is not found: {}", e->name, e->dir),
                           {},
                           {openEngineFix(e->name), locateDirFix(e->name)}});
        }
    }
    return out;
}

std::vector<Notice> optionNotices(const MatchSetup& setup, const std::vector<EngineEntry>& engines) {
    std::vector<Notice> out;
    std::set<std::string> said;  // engine and option
    const auto add = [&](const EngineEntry& e, const std::string& option, std::string text) {
        if (!said.insert(e.name + '\n' + option).second) return;
        out.push_back({Notice::Level::Warning, std::move(text), {}, {openEngineFix(e.name)}});
    };
    for (const auto& p : setupParticipants(setup)) {
        const auto* e = findEngine(engines, p.name);
        if (!e || e->uci_options.empty()) continue;
        // The values the setup gives: they replace the engine's own.
        const int hash    = p.hash > 0 ? p.hash : setup.common_hash;
        const int threads = p.threads > 0 ? p.threads : setup.common_threads;
        std::set<std::string> given;
        for (const auto& [key, value] : {std::pair{"Hash", hash}, std::pair{"Threads", threads}}) {
            const auto* option = uciOption(*e, key);
            if (value <= 0 || !option) continue;
            given.insert(key);
            if (const auto why = refusal(*option, std::to_string(value)); !why.empty()) {
                add(*e, key,
                    trf("{} refuses {} = {}, given by this tournament: {}. It is not sent: the engine plays with its "
                        "default.",
                        e->name, key, value, why));
            }
        }
        for (const auto& [key, value] : e->options) {
            if (given.count(key)) continue;
            const auto* option = uciOption(*e, key);
            if (!option) {
                add(*e, key, trf("{} has no option {} (any more): its value {} is not sent.", e->name, key, value));
            } else if (const auto why = refusal(*option, value); !why.empty()) {
                add(*e, key,
                    trf("{} refuses its option {} = {}: {}. It is not sent: the engine plays with its default.",
                        e->name, key, value, why));
            }
        }
    }
    return out;
}

Notice memoryNotice(const MatchSetup& setup, const std::vector<EngineEntry>& engines, const MemoryNeed& need) {
    Notice n;
    n.level = need.aboveInstalled()   ? Notice::Level::Error
              : need.aboveAvailable() ? Notice::Level::Warning
                                      : Notice::Level::Ok;
    n.text  = need.text();
    if (n.level == Notice::Level::Ok) return n;

    // The participant with the largest Hash, as setupHashes() reads it, and whether it is the engine's own.
    std::string largest;
    uint64_t most = 0;
    bool own      = false;
    for (const auto& p : setupParticipants(setup)) {
        const auto* e = findEngine(engines, p.name);
        if (!e) continue;
        const auto* option = uciOption(*e, "Hash");
        const int forced   = p.hash > 0 ? p.hash : setup.common_hash;
        uint64_t hash      = 0;
        bool engine_own    = false;
        if (forced > 0 && (e->uci_options.empty() || option)) {
            hash = static_cast<uint64_t>(forced);
        } else {
            const auto* value = e->option("Hash");
            if (!value && option) value = &option->default_value;
            try {
                hash = value ? static_cast<uint64_t>(std::max(0LL, std::stoll(*value))) : 0;
            } catch (const std::exception&) {
            }
            engine_own = true;
        }
        if (hash > most) largest = e->name, most = hash, own = engine_own;
    }
    if (largest.empty()) return n;
    n.text += " " + trf("The largest Hash: {}, {} per process.", largest, formatMemory(most));
    if (own) n.fixes = {openEngineFix(largest)};
    return n;
}

std::optional<Notice> bookNotice(const MatchSetup& setup) {
    std::error_code ec;
    if (setup.book.empty() || fs::is_regular_file(setup.book, ec)) return std::nullopt;
    return Notice{Notice::Level::Error, trf("Opening book not found: {}", setup.book), {}, {locateBookFix()}};
}

std::optional<StartupFailure> startupFailure(const std::vector<std::string>& lines) {
    // As BaseTournament::playGame() says it, once the last try failed.
    static constexpr std::string_view FATAL = "Fatal; ", FAILURE = " engine startup failure: \"";
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        const std::string_view line(*it);
        if (line.substr(0, FATAL.size()) != FATAL) continue;
        const auto at = line.find(FAILURE);
        if (at == std::string_view::npos || at <= FATAL.size()) continue;
        auto reason = line.substr(at + FAILURE.size());
        if (!reason.empty() && reason.back() == '"') reason.remove_suffix(1);
        return StartupFailure{std::string(line.substr(FATAL.size(), at - FATAL.size())), std::string(reason)};
    }
    return std::nullopt;
}

std::string libraryEngine(const std::vector<EngineEntry>& engines, const std::string& participant) {
    if (findEngine(engines, participant)) return participant;
    for (auto paren = participant.find(" ("); paren != std::string::npos; paren = participant.find(" (", paren + 1)) {
        if (const auto* e = findEngine(engines, participant.substr(0, paren))) return e->name;
    }
    return {};
}

std::vector<TablebaseUse> tablebaseUses(const MatchSetup& setup, const std::vector<EngineEntry>& engines) {
    std::vector<TablebaseUse> out;
    const auto use = [&](const std::string& paths) -> TablebaseUse& {
        for (auto& u : out) {
            if (u.paths == paths) return u;
        }
        out.push_back({paths, false, 0, {}});
        return out.back();
    };
    if (setup.tb_enabled && !setup.tb_paths.empty()) {
        auto& u        = use(absolutePaths(setup.tb_paths, {}));
        u.adjudication = true;
        u.pieces       = std::max(0, setup.tb_pieces);
    }
    for (const auto& name : enginesOfSetup(setup)) {
        const auto* e = findEngine(engines, name);
        if (!e) continue;
        const auto* option = uciOption(*e, "SyzygyPath");
        std::string paths;
        if (setup.engine_syzygy && option) {
            paths = setup.tb_paths;
        } else if (const auto* own = e->option("SyzygyPath")) {
            paths = *own;
        } else if (option) {
            paths = option->default_value;
        }
        if (paths.empty() || paths == "<empty>") continue;
        auto& u = use(absolutePaths(paths, engineWorkingDir(*e)));
        if (std::find(u.engines.begin(), u.engines.end(), e->name) == u.engines.end()) u.engines.push_back(e->name);
    }
    return out;
}

std::optional<Notice> adjudicationFolderNotice(const MatchSetup& setup) {
    if (!setup.tb_enabled || !splitTablebasePath(setup.tb_paths).empty()) return std::nullopt;
    return Notice{Notice::Level::Error,
                  TR("Tablebase adjudication: no folder given. The tournament cannot start without one."),
                  {},
                  {locateTablebasesFix()}};
}

std::vector<Notice> tablebaseNotices(const TablebaseUse& use, const TablebaseCheck& check) {
    std::vector<Notice> out;
    const auto dirs = splitTablebasePath(use.paths);
    std::string present;  // the first folder found, for Open the folder
    for (const auto& d : dirs) {
        if (std::find(check.missing_dirs.begin(), check.missing_dirs.end(), d) == check.missing_dirs.end()) {
            present = d;
            break;
        }
    }
    const auto who = use.adjudication && !use.engines.empty()
                         ? trf("Tablebase adjudication and SyzygyPath of {}", names(use.engines))
                     : use.adjudication ? std::string(TR("Tablebase adjudication"))
                                        : trf("SyzygyPath of {}", names(use.engines));
    // The text names the folders, shortened; the tooltip says who uses them, and where they are in full.
    const auto shown  = shortPaths(use.paths);
    const auto detail = [&](const std::string& more = {}) {
        return who + "\n" + use.paths + (more.empty() ? "" : "\n" + more);
    };
    // The fixes of the folder: open it; choose another for the adjudication, see the engine otherwise.
    const auto fixes = [&](bool folder) {
        std::vector<Fix> f;
        if (folder && !present.empty()) f.push_back(openFolderFix(present));
        if (use.adjudication && (present.empty() || check.wdl_files == 0)) f.push_back(locateTablebasesFix());
        if (!use.adjudication && !use.engines.empty()) f.push_back(openEngineFix(use.engines.front()));
        return f;
    };
    // fastchess does not start a tournament whose adjudication finds no table.
    const auto level = use.adjudication ? Notice::Level::Error : Notice::Level::Warning;
    const auto cannot =
        use.adjudication ? " " + std::string(TR("The tournament cannot start with this adjudication.")) : std::string();

    if (present.empty()) {
        out.push_back({level, trf("{}: folder not found: {}", who, shown) + cannot, detail(), fixes(false)});
        return out;
    }
    for (const auto& d : check.missing_dirs) {
        out.push_back(
            {Notice::Level::Warning, trf("{}: folder not found: {}", who, shortPaths(d)), detail(), fixes(false)});
    }
    if (check.wdl_files == 0) {
        out.push_back({level, trf("{} points to a folder with no tablebase files: {}", who, shown) + cannot, detail(),
                       fixes(true)});
        return out;
    }

    // The pieces used: the adjudication's limit, the tables found otherwise (the engines probe all they find); and
    // the most pieces with every table there, from 3 up.
    const int used     = use.adjudication && use.pieces > 0 ? use.pieces : check.largest;
    const int complete = check.missing.empty() ? check.largest : check.missing.begin()->first - 1;
    if (use.adjudication && use.pieces > check.largest) {
        auto f = fixes(true);
        if (check.largest >= 3) f.insert(f.begin(), lowerTablebasePiecesFix(check.largest));
        out.push_back({Notice::Level::Warning,
                       trn("Adjudication up to {} piece, but only {}-piece files found in {}.",
                           "Adjudication up to {} pieces, but only {}-piece files found in {}.", use.pieces, use.pieces,
                           check.largest, shown),
                       detail(), f});
    }
    for (const auto& [pieces, missing] : check.missing) {
        if (pieces > used) break;
        auto f = fixes(true);
        // The adjudication can stop where the tables are complete.
        if (use.adjudication && pieces == complete + 1 && complete >= 3)
            f.insert(f.begin(), lowerTablebasePiecesFix(complete));
        out.push_back(
            {Notice::Level::Warning,
             trn("Syzygy {} pieces: {} file missing in {} ({}).", "Syzygy {} pieces: {} files missing in {} ({}).",
                 missing.size(), pieces, missing.size(), shown, firstFiles(missing, ".rtbw")),
             detail(allFiles(missing, ".rtbw")), f});
    }
    if (!check.damaged.empty()) {
        out.push_back({Notice::Level::Warning,
                       trn("Syzygy: {} damaged file in {} ({}): its size is not a tablebase file's.",
                           "Syzygy: {} damaged files in {} ({}): their size is not a tablebase file's.",
                           check.damaged.size(), check.damaged.size(), shown, firstFiles(check.damaged, "")),
                       detail(std::string(TR("Cut short, by a download that did not end for example. Engines leave "
                                             "such a file out, and some stop at once when they meet it.")) +
                              "\n" + allFiles(check.damaged, "")),
                       fixes(true)});
    }
    if (out.empty() && use.adjudication) {
        out.push_back({Notice::Level::Ok,
                       trn("Tablebase adjudication up to {} piece: every file there",
                           "Tablebase adjudication up to {} pieces: every file there", used, used),
                       detail(),
                       {}});
    }
    return out;
}

}  // namespace fcg

namespace fcg {

int marginFor(double round_trip_ms) {
    const auto twice = static_cast<int>(std::ceil(round_trip_ms * 2 / 10.0)) * 10;
    return std::max(10, twice);
}

std::vector<Notice> remoteNotices(const MatchSetup& setup, const std::vector<EngineEntry>& engines,
                                  const std::vector<Machine>& machines) {
    std::vector<Notice> out;
    std::vector<std::string> seen;
    const auto machineOf = [&](const std::string& id) -> const Machine* {
        for (const auto& m : machines) {
            if (m.id == id) return &m;
        }
        return nullptr;
    };
    const auto nameOf = [](const Machine& m) { return m.name.empty() ? m.host : m.name; };
    // The time a move has: the increment, else the time of a period over its moves (40 for a whole game).
    double per_move_ms = 0;
    if (setup.limit == LimitKind::Time) {
        per_move_ms = setup.tc_inc_s > 0 ? setup.tc_inc_s * 1000.0
                                         : setup.tc_base_s * 1000.0 / (setup.tc_moves > 0 ? setup.tc_moves : 40);
    } else if (setup.limit == LimitKind::MoveTime) {
        per_move_ms = setup.move_s * 1000.0;
    }
    for (const auto& p : setupParticipants(setup)) {
        const auto* e = findEngine(engines, p.name);
        if (!e || e->machine.empty() || std::find(seen.begin(), seen.end(), e->name) != seen.end()) continue;
        seen.push_back(e->name);
        const auto* m = machineOf(e->machine);
        if (!m) {
            out.push_back({Notice::Level::Error,
                           trf("{} runs on a machine that is no longer in Settings > Machines.", e->name),
                           {},
                           {openEngineFix(e->name)}});
            continue;
        }
        const auto where = nameOf(*m);
        if (!setup.machine.empty() && setup.machine != m->id) {
            const auto* run_on = machineOf(setup.machine);
            out.push_back({Notice::Level::Error,
                           trf("{} runs on {}, and the tournament on {}: an engine on a machine plays from this "
                               "computer, or on its machine.",
                               e->name, where, run_on ? nameOf(*run_on) : setup.machine),
                           {},
                           {setRunOnFix({}, {}), setRunOnFix(m->id, where)}});
            continue;
        }
        if (setup.machine == m->id) {
            out.push_back({Notice::Level::Ok, trf("{} plays on {}, where the tournament runs", e->name, where)});
            continue;
        }
        if (m->system.checked.empty()) {
            out.push_back({Notice::Level::Error,
                           trf("{} has never been tested: its host key is unknown, and nothing is sent to it before.",
                               where),
                           {},
                           {testMachineFix(m->id, where)}});
            continue;
        }
        const double rtt = e->round_trip_ms;
        if (rtt > 0 && per_move_ms > 0 && rtt > per_move_ms * 0.05 && setup.tc_margin_ms < marginFor(rtt)) {
            const auto share = static_cast<int>(std::lround(100.0 * rtt / per_move_ms));
            out.push_back({Notice::Level::Warning,
                           setup.limit == LimitKind::Time && setup.tc_inc_s > 0
                               ? trf("{} plays on {}: round trip {:.0f} ms, {} % of the {} s increment, counted on "
                                     "its clock.",
                                     e->name, where, rtt, share, setup.tc_inc_s)
                               : trf("{} plays on {}: round trip {:.0f} ms, {} % of the time a move has, counted on "
                                     "its clock.",
                                     e->name, where, rtt, share),
                           {},
                           {setMarginFix(marginFor(rtt))}});
            continue;
        }
        out.push_back({Notice::Level::Ok, rtt > 0 ? trf("{} plays on {} · round trip {:.1f} ms", e->name, where, rtt)
                                                  : trf("{} plays on {} (Check measures the round trip)", e->name,
                                                        where)});
    }
    return out;
}

}  // namespace fcg
