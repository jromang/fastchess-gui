#include "storage.hpp"

#include <fstream>
#include <system_error>

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
        out << value.dump(2) << '\n';
        if (!out) return false;
    }

    fs::rename(tmp, file, ec);
    return !ec;
}

Storage::Storage(fs::path root) : root_(std::move(root)) {
    std::error_code ec;
    fs::create_directories(root_, ec);
    fs::remove(root_ / "imgui.ini", ec);

    if (const auto settings = readJson<nlohmann::json>(root_ / "settings.json")) {
        runs_dir_ = fs::path(settings->value("tournaments_dir", std::string{}));
    }
}

void Storage::setRunsDir(const fs::path& dir) {
    runs_dir_                   = dir == defaultRunsDir() ? fs::path{} : dir;
    auto settings               = readJson<nlohmann::json>(root_ / "settings.json").value_or(nlohmann::json::object());
    settings["tournaments_dir"] = runs_dir_.string();
    writeJson(root_ / "settings.json", settings);
}

std::vector<EngineEntry> Storage::loadEngines() const {
    return readJson<std::vector<EngineEntry>>(root_ / "engines.json").value_or(std::vector<EngineEntry>{});
}

void Storage::saveEngines(const std::vector<EngineEntry>& engines) const { writeJson(root_ / "engines.json", engines); }

std::vector<fs::path> Storage::ratingPgns() const {
    std::vector<fs::path> out;
    if (const auto settings = readJson<nlohmann::json>(root_ / "settings.json")) {
        for (const auto& f : settings->value("rating_pgns", std::vector<std::string>{})) out.emplace_back(f);
    }
    return out;
}

void Storage::setRatingPgns(const std::vector<fs::path>& files) const {
    auto settings = readJson<nlohmann::json>(root_ / "settings.json").value_or(nlohmann::json::object());
    std::vector<std::string> list;
    for (const auto& f : files) list.push_back(f.string());
    settings["rating_pgns"] = list;
    writeJson(root_ / "settings.json", settings);
}

std::vector<BookEntry> Storage::loadBooks() const {
    return readJson<std::vector<BookEntry>>(root_ / "books.json").value_or(std::vector<BookEntry>{});
}

void Storage::saveBooks(const std::vector<BookEntry>& books) const { writeJson(root_ / "books.json", books); }

MatchSetup Storage::loadSetup() const { return readJson<MatchSetup>(root_ / "setup.json").value_or(MatchSetup{}); }

void Storage::saveSetup(const MatchSetup& setup) const { writeJson(root_ / "setup.json", setup); }

std::vector<QueuedTournament> Storage::loadQueue() const {
    return readJson<std::vector<QueuedTournament>>(root_ / "queue.json").value_or(std::vector<QueuedTournament>{});
}

void Storage::saveQueue(const std::vector<QueuedTournament>& queue) const { writeJson(root_ / "queue.json", queue); }

}  // namespace fcg
