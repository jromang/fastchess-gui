#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "model.hpp"

namespace fcg {

// Where the GUI keeps its files: engines.json, setup.json, imgui.ini and runs/.
// Defaults to SDL_GetPrefPath(); --profile <dir> overrides it.
class Storage {
   public:
    explicit Storage(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] std::filesystem::path runsDir() const { return root_ / "runs"; }
    // Window layout. Named layout.ini since the windows got language independent ids;
    // the imgui.ini of earlier versions is removed at startup.
    [[nodiscard]] std::filesystem::path iniFile() const { return root_ / "layout.ini"; }

    [[nodiscard]] std::vector<EngineEntry> loadEngines() const;
    void saveEngines(const std::vector<EngineEntry>& engines) const;

    [[nodiscard]] MatchSetup loadSetup() const;
    void saveSetup(const MatchSetup& setup) const;

   private:
    std::filesystem::path root_;
};

// Reads a JSON file into T, nullopt when it is missing or unreadable.
template <typename T>
std::optional<T> readJson(const std::filesystem::path& file);

// Writes through a temporary file so a crash never leaves half a file behind.
bool writeJson(const std::filesystem::path& file, const nlohmann::json& value);

}  // namespace fcg
