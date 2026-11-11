#include "storage.hpp"

#include <fstream>
#include <system_error>

#include "console.hpp"
#include "i18n.hpp"

namespace fcg {

namespace fs = std::filesystem;

template <typename T>
std::optional<T> readJson(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;

    try {
        return nlohmann::json::parse(in).get<T>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

template std::optional<std::vector<EngineEntry>> readJson(const fs::path&);
template std::optional<MatchSetup> readJson(const fs::path&);
template std::optional<std::vector<QueuedTournament>> readJson(const fs::path&);
template std::optional<nlohmann::json> readJson(const fs::path&);

bool writeJson(const fs::path& file, const nlohmann::json& value) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);

    const auto tmp = fs::path(file).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        // Text that is not UTF-8 (an engine's output...) is written with U+FFFD rather than failing.
        out << value.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
        if (!out) return false;
    }

    fs::rename(tmp, file, ec);
    return !ec;
}

namespace {

// A profile file that is there but cannot be read is set aside, so that the defaults saved in its place do not
// replace it: a typo in a hand edit, or a file left empty by a power cut, must not cost the list of engines.
template <typename T>
std::optional<T> readProfileFile(const fs::path& file) {
    auto value = readJson<T>(file);
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

    runs_dir_ = fs::path(setting(settings(), "tournaments_dir", std::string{}));
}

void Storage::setRunsDir(const fs::path& dir) {
    runs_dir_                   = dir == defaultRunsDir() ? fs::path{} : dir;
    auto settings               = this->settings();
    settings["tournaments_dir"] = runs_dir_.string();
    writeJson(root_ / "settings.json", settings);
}

std::vector<EngineEntry> Storage::loadEngines() const {
    return readProfileFile<std::vector<EngineEntry>>(root_ / "engines.json").value_or(std::vector<EngineEntry>{});
}

void Storage::saveEngines(const std::vector<EngineEntry>& engines) const { writeJson(root_ / "engines.json", engines); }

std::vector<fs::path> Storage::ratingPgns() const {
    std::vector<fs::path> out;
    for (const auto& f : setting(settings(), "rating_pgns", std::vector<std::string>{})) out.emplace_back(f);
    return out;
}

void Storage::setRatingPgns(const std::vector<fs::path>& files) const {
    auto settings = this->settings();
    std::vector<std::string> list;
    for (const auto& f : files) list.push_back(f.string());
    settings["rating_pgns"] = list;
    writeJson(root_ / "settings.json", settings);
}

std::string Storage::lichessToken() const { return setting(settings(), "lichess_token", std::string{}); }

void Storage::setLichessToken(const std::string& token) const {
    auto settings = this->settings();
    if (token.empty()) {
        settings.erase("lichess_token");
    } else {
        settings["lichess_token"] = token;
    }
    const auto file = root_ / "settings.json";
    writeJson(file, settings);
    // Readable by its owner only, as it holds a token.
    std::error_code ec;
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
}

std::vector<BookEntry> Storage::loadBooks() const {
    return readProfileFile<std::vector<BookEntry>>(root_ / "books.json").value_or(std::vector<BookEntry>{});
}

void Storage::saveBooks(const std::vector<BookEntry>& books) const { writeJson(root_ / "books.json", books); }

MatchSetup Storage::loadSetup() const {
    return readProfileFile<MatchSetup>(root_ / "setup.json").value_or(MatchSetup{});
}

void Storage::saveSetup(const MatchSetup& setup) const { writeJson(root_ / "setup.json", setup); }

std::vector<QueuedTournament> Storage::loadQueue() const {
    return readProfileFile<std::vector<QueuedTournament>>(root_ / "queue.json")
        .value_or(std::vector<QueuedTournament>{});
}

void Storage::saveQueue(const std::vector<QueuedTournament>& queue) const { writeJson(root_ / "queue.json", queue); }

}  // namespace fcg
