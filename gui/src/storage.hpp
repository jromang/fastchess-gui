#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "model.hpp"

namespace fcg {

// Where the GUI keeps its files: engines.json, setup.json, layout.ini, settings.json and the tournaments (runs/
// unless settings.json names another folder). Defaults to SDL_GetPrefPath(); --profile <dir> overrides it.
class Storage {
   public:
    explicit Storage(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] std::filesystem::path runsDir() const { return runs_dir_.empty() ? defaultRunsDir() : runs_dir_; }
    [[nodiscard]] std::filesystem::path defaultRunsDir() const { return root_ / "runs"; }
    // Another folder for new tournaments and the lists, empty for the default; kept in settings.json.
    void setRunsDir(const std::filesystem::path& dir);
    // Window layout. Named layout.ini since the windows got language independent ids;
    // the imgui.ini of earlier versions is removed at startup.
    [[nodiscard]] std::filesystem::path iniFile() const { return root_ / "layout.ini"; }

    [[nodiscard]] std::vector<EngineEntry> loadEngines() const;
    void saveEngines(const std::vector<EngineEntry>& engines) const;

    [[nodiscard]] MatchSetup loadSetup() const;
    void saveSetup(const MatchSetup& setup) const;

    // Tournaments waiting to be started one after the other (queue.json).
    [[nodiscard]] std::vector<QueuedTournament> loadQueue() const;
    void saveQueue(const std::vector<QueuedTournament>& queue) const;

   private:
    std::filesystem::path root_;
    std::filesystem::path runs_dir_;
};

// Reads a JSON file into T, nullopt when it is missing or unreadable.
template <typename T>
std::optional<T> readJson(const std::filesystem::path& file);

// Writes through a temporary file so a crash never leaves half a file behind.
bool writeJson(const std::filesystem::path& file, const nlohmann::json& value);

}  // namespace fcg
