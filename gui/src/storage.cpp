#include "storage.hpp"

#include <fstream>
#include <system_error>

#include <tour/files.hpp>

#include "console.hpp"
#include "i18n.hpp"
#include "portable.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

// A JSON file made a T by `convert`, nullopt when it is missing or unreadable.
template <typename T, typename Convert>
std::optional<T> readJsonWith(const fs::path& file, Convert convert) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;

    try {
        return convert(nlohmann::json::parse(in));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

template <typename T>
T plain(const nlohmann::json& j) {
    return j.get<T>();
}

// A setup the program saved in the profile, upgraded (see upgradeSavedSetup()), its paths made absolute again (in the
// portable mode, those in the program's folder are kept relative to it).
MatchSetup savedSetup(const nlohmann::json& j) {
    auto setup = j.get<MatchSetup>();
    upgradeSavedSetup(j, setup);
    portable::resolve(setup);
    return setup;
}

std::vector<QueuedTournament> savedQueue(const nlohmann::json& j) {
    auto queue = j.get<std::vector<QueuedTournament>>();
    for (std::size_t i = 0; i < queue.size(); ++i) {
        if (const auto& item = j[i]; item.contains("setup")) upgradeSavedSetup(item["setup"], queue[i].setup);
        portable::resolve(queue[i].setup);
    }
    return queue;
}

// The engines and books of the profile, their paths made absolute again.
template <typename T>
std::vector<T> resolvedList(const nlohmann::json& j) {
    auto items = j.get<std::vector<T>>();
    portable::resolve(items);
    return items;
}

}  // namespace

template <typename T>
std::optional<T> readJson(const fs::path& file) {
    return readJsonWith<T>(file, plain<T>);
}

template std::optional<std::vector<EngineEntry>> readJson(const fs::path&);
template std::optional<MatchSetup> readJson(const fs::path&);
template std::optional<std::vector<QueuedTournament>> readJson(const fs::path&);
template std::optional<nlohmann::json> readJson(const fs::path&);
template std::optional<RunningMarker> readJson(const fs::path&);
template std::optional<std::vector<layout::Layout>> readJson(const fs::path&);

std::optional<MatchSetup> readSavedSetup(const fs::path& file) { return readJsonWith<MatchSetup>(file, savedSetup); }

bool writeJson(const fs::path& file, const nlohmann::json& value, std::string* error, bool backup, bool owner_only) {
    try {
        // Text that is not UTF-8 (an engine's output...) is written with U+FFFD rather than failing.
        tour::replaceFile(file, value.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + '\n', backup,
                          owner_only);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

namespace {

// A profile file written, or the reason it could not be on the Console.
void save(const fs::path& file, const nlohmann::json& value, bool backup = false, bool owner_only = false) {
    std::string error;
    if (!writeJson(file, value, &error, backup, owner_only))
        Console::instance().add(trf("Cannot save {}: {}", file.string(), error), true);
}

// A profile file that is there but cannot be read is set aside, so that the defaults saved in its place do not
// replace it: a typo in a hand edit, or a file left empty by a power cut, must not cost the list of engines.
template <typename T, typename Convert = T (*)(const nlohmann::json&)>
std::optional<T> readProfileFile(const fs::path& file, Convert convert = plain<T>) {
    auto value = readJsonWith<T>(file, convert);
    std::error_code ec;
    if (value || !fs::exists(file, ec)) return value;
    for (int n = 1; n < 100; ++n) {
        const auto aside = fs::path(file).concat(n == 1 ? std::string(".unreadable") : fmt::format(".unreadable{}", n));
        if (fs::exists(aside, ec)) continue;
        fs::rename(file, aside, ec);
        if (!ec) Console::instance().add(trf("{} could not be read: kept as {}", file.string(), aside.string()), true);
        break;
    }
    return value;
}

// A setting of settings.json, the fallback when the file is not an object or the value has another type.
template <typename T>
T setting(const nlohmann::json& settings, const char* key, T fallback) {
    try {
        return settings.value(key, fallback);
    } catch (const nlohmann::json::exception&) {
        return fallback;
    }
}

}  // namespace

nlohmann::json Storage::settings() const {
    auto settings = readProfileFile<nlohmann::json>(root_ / "settings.json");
    return settings && settings->is_object() ? *settings : nlohmann::json::object();
}

Storage::Storage(fs::path root) : root_(std::move(root)) {
    std::error_code ec;
    fs::create_directories(root_, ec);
    fs::remove(root_ / "imgui.ini", ec);

    const auto settings = this->settings();
    runs_dir_           = fs::path(portable::resolved(setting(settings, "tournaments_dir", std::string{})));
    software_rendering_ = setting(settings, "software_rendering", false);
}

void Storage::setRunsDir(const fs::path& dir) {
    runs_dir_                   = dir == defaultRunsDir() ? fs::path{} : dir;
    auto settings               = this->settings();
    settings["tournaments_dir"] = portable::stored(runs_dir_.string());
    save(root_ / "settings.json", settings);
}

void Storage::setSoftwareRendering(bool on) {
    software_rendering_            = on;
    auto settings                  = this->settings();
    settings["software_rendering"] = on;
    save(root_ / "settings.json", settings);
}

std::vector<EngineEntry> Storage::loadEngines() const {
    const auto file = root_ / "engines.json";
    if (auto engines = readProfileFile<std::vector<EngineEntry>>(file, resolvedList<EngineEntry>)) return *engines;

    // The list as it was before its last change, when the file is gone or could not be read.
    const auto backup = fs::path(file).concat(".bak");
    if (auto engines = readJson<std::vector<EngineEntry>>(backup)) {
        Console::instance().add(trf("The engines are those of {}, kept before the last change.", backup.string()),
                                true);
        portable::resolve(*engines);
        return *engines;
    }
    return {};
}

void Storage::saveEngines(const std::vector<EngineEntry>& engines) const {
    // With the list before this change as engines.json.bak: hundreds of engines and their settings are not lost
    // to one bad write.
    save(root_ / "engines.json", portable::stored(engines), true);
}

std::vector<fs::path> Storage::ratingPgns() const {
    std::vector<fs::path> out;
    for (const auto& f : setting(settings(), "rating_pgns", std::vector<std::string>{}))
        out.emplace_back(portable::resolved(f));
    return out;
}

void Storage::setRatingPgns(const std::vector<fs::path>& files) const {
    auto settings = this->settings();
    std::vector<std::string> list;
    for (const auto& f : files) list.push_back(portable::stored(f.string()));
    settings["rating_pgns"] = list;
    save(root_ / "settings.json", settings);
}

std::vector<std::string> Storage::separatePlayers() const {
    return setting(settings(), "separate_players", std::vector<std::string>{});
}

void Storage::setSeparatePlayers(const std::vector<std::string>& players) const {
    auto settings                = this->settings();
    settings["separate_players"] = players;
    save(root_ / "settings.json", settings);
}

fs::path Storage::engineFolder() const {
    return fs::path(portable::resolved(setting(settings(), "engine_folder", std::string{})));
}

void Storage::setEngineFolder(const fs::path& folder) const {
    auto settings             = this->settings();
    settings["engine_folder"] = portable::stored(folder.string());
    save(root_ / "settings.json", settings);
}

std::string Storage::lichessToken() const { return setting(settings(), "lichess_token", std::string{}); }

void Storage::setLichessToken(const std::string& token) const {
    auto settings = this->settings();
    if (token.empty()) {
        settings.erase("lichess_token");
    } else {
        settings["lichess_token"] = token;
    }
    // Readable by its owner only, as it holds a token, from before the token is in it; later rewrites keep that.
    save(root_ / "settings.json", settings, false, true);
}

std::pair<std::string, std::string> Storage::submissionTester() const {
    const auto s = settings();
    return {setting(s, "submission_tester", std::string{}), setting(s, "submission_site", std::string{})};
}

void Storage::setSubmissionTester(const std::string& name, const std::string& site) const {
    auto settings                 = this->settings();
    settings["submission_tester"] = name;
    settings["submission_site"]   = site;
    save(root_ / "settings.json", settings);
}

std::optional<WindowPlace> Storage::windowPlace() const {
    const auto s = settings();
    if (!s.contains("window") || !s["window"].is_object()) return std::nullopt;
    try {
        return s["window"].get<WindowPlace>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

void Storage::setWindowPlace(const WindowPlace& place) const {
    auto settings      = this->settings();
    settings["window"] = place;
    save(root_ / "settings.json", settings);
}

MachineBudget Storage::machineBudget() const {
    const auto s = settings();
    MachineBudget b;
    b.cores           = std::max(0, setting(s, "budget_cores", 0));
    b.memory_mb       = std::max(0, setting(s, "budget_memory_mb", 0));
    b.gpus            = std::max(0, setting(s, "budget_gpus", 0));
    b.queue_when_free = setting(s, "queue_when_free", false);
    return b;
}

void Storage::setMachineBudget(const MachineBudget& budget) const {
    auto settings                = this->settings();
    settings["budget_cores"]     = budget.cores;
    settings["budget_memory_mb"] = budget.memory_mb;
    settings["budget_gpus"]      = budget.gpus;
    settings["queue_when_free"]  = budget.queue_when_free;
    save(root_ / "settings.json", settings);
}

update::Settings Storage::updateSettings() const {
    const auto s = settings();
    update::Settings out;
    out.enabled   = setting(s, "check_new_version", false);
    out.checked   = setting(s, "new_version_checked", int64_t{0});
    out.dismissed = setting(s, "new_version_dismissed", std::string{});
    if (s.contains("latest_release") && s["latest_release"].is_object()) {
        try {
            out.latest = s["latest_release"].get<update::Release>();
        } catch (const nlohmann::json::exception&) {
        }
    }
    return out;
}

void Storage::setUpdateSettings(const update::Settings& updates) const {
    auto settings                     = this->settings();
    settings["check_new_version"]     = updates.enabled;
    settings["new_version_checked"]   = updates.checked;
    settings["new_version_dismissed"] = updates.dismissed;
    if (updates.latest) {
        settings["latest_release"] = *updates.latest;
    } else {
        settings.erase("latest_release");
    }
    save(root_ / "settings.json", settings);
}

appearance::Settings Storage::appearance() const {
    const auto s = settings();
    appearance::Settings out;
    if (const auto it = s.find("appearance"); it != s.end()) appearance::from_json(*it, out);
    out.piece_folder = portable::resolved(out.piece_folder);
    return out;
}

void Storage::setAppearance(const appearance::Settings& appearance) const {
    auto kept         = appearance;
    kept.piece_folder = portable::stored(kept.piece_folder);
    auto settings     = this->settings();
    appearance::to_json(settings["appearance"], kept);
    save(root_ / "settings.json", settings);
}

std::vector<BookEntry> Storage::loadBooks() const {
    return readProfileFile<std::vector<BookEntry>>(root_ / "books.json", resolvedList<BookEntry>)
        .value_or(std::vector<BookEntry>{});
}

void Storage::saveBooks(const std::vector<BookEntry>& books) const {
    save(root_ / "books.json", portable::stored(books));
}

MatchSetup Storage::loadSetup() const {
    return readProfileFile<MatchSetup>(root_ / "setup.json", savedSetup).value_or(MatchSetup{});
}

void Storage::saveSetup(const MatchSetup& setup) const { save(root_ / "setup.json", portable::stored(setup)); }

nlohmann::json Storage::loadProfiles() const {
    auto profiles = readProfileFile<nlohmann::json>(root_ / "profiles.json");
    return profiles && profiles->is_object() ? *profiles : nlohmann::json::object();
}

void Storage::saveProfiles(const nlohmann::json& profiles) const { save(root_ / "profiles.json", profiles); }

std::vector<layout::Layout> Storage::loadLayouts() const {
    return readProfileFile<std::vector<layout::Layout>>(root_ / "layouts.json").value_or(std::vector<layout::Layout>{});
}

void Storage::saveLayouts(const std::vector<layout::Layout>& layouts) const { save(root_ / "layouts.json", layouts); }

std::vector<QueuedTournament> Storage::loadQueue() const {
    return readProfileFile<std::vector<QueuedTournament>>(root_ / "queue.json", savedQueue)
        .value_or(std::vector<QueuedTournament>{});
}

void Storage::saveQueue(const std::vector<QueuedTournament>& queue) const {
    auto kept = queue;
    for (auto& item : kept) item.setup = portable::stored(std::move(item.setup));
    save(root_ / "queue.json", kept);
}

std::vector<RunningMarker> Storage::runningMarkers() const {
    std::vector<RunningMarker> out;
    const auto j = readJson<nlohmann::json>(root_ / "running.json");
    if (!j) return out;
    try {
        if (j->is_array()) {
            for (const auto& m : *j) out.push_back(m.get<RunningMarker>());
        } else if (j->is_object()) {
            out.push_back(j->get<RunningMarker>());
        }
    } catch (const std::exception&) {
        out.clear();
    }
    std::erase_if(out, [](const RunningMarker& m) { return m.dir.empty(); });
    return out;
}

std::optional<RunningMarker> Storage::runningMarker(const std::string& dir) const {
    for (const auto& m : runningMarkers()) {
        if (m.dir == dir) return m;
    }
    return std::nullopt;
}

namespace {

void saveMarkers(const fs::path& file, const std::vector<RunningMarker>& markers) {
    if (markers.empty()) {
        std::error_code ec;
        fs::remove(file, ec);
        return;
    }
    (void)writeJson(file, nlohmann::json(markers));
}

}  // namespace

void Storage::setRunningMarker(const RunningMarker& marker) const {
    auto markers = runningMarkers();
    const auto it =
        std::find_if(markers.begin(), markers.end(), [&](const RunningMarker& m) { return m.dir == marker.dir; });
    if (it != markers.end()) {
        *it = marker;
    } else {
        markers.push_back(marker);
    }
    saveMarkers(root_ / "running.json", markers);
}

void Storage::clearRunningMarker(const std::string& dir) const {
    auto markers     = runningMarkers();
    const auto count = markers.size();
    std::erase_if(markers, [&](const RunningMarker& m) { return m.dir == dir; });
    if (markers.size() != count) saveMarkers(root_ / "running.json", markers);
}

}  // namespace fcg
