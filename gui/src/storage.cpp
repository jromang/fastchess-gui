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
}

std::vector<EngineEntry> Storage::loadEngines() const {
    return readJson<std::vector<EngineEntry>>(root_ / "engines.json").value_or(std::vector<EngineEntry>{});
}

void Storage::saveEngines(const std::vector<EngineEntry>& engines) const { writeJson(root_ / "engines.json", engines); }

MatchSetup Storage::loadSetup() const { return readJson<MatchSetup>(root_ / "setup.json").value_or(MatchSetup{}); }

void Storage::saveSetup(const MatchSetup& setup) const { writeJson(root_ / "setup.json", setup); }

}  // namespace fcg
