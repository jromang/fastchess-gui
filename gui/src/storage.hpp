#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "layout.hpp"
#include "model.hpp"

namespace fcg {

// Where the GUI keeps its files: engines.json, books.json, setup.json, layout.ini, layouts.json, settings.json and the
// tournaments (runs/ unless settings.json names another folder). Defaults to SDL_GetPrefPath(); --profile <dir>
// overrides it.
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

    // PGN files whose games count in the ratings too (settings.json).
    [[nodiscard]] std::vector<std::filesystem::path> ratingPgns() const;
    void setRatingPgns(const std::vector<std::filesystem::path>& files) const;

    // The folder engines were last imported from, which Refresh looks in again for new ones (settings.json); empty
    // when none.
    [[nodiscard]] std::filesystem::path engineFolder() const;
    void setEngineFolder(const std::filesystem::path& folder) const;

    // The lichess API token, in settings.json: never in a tournament's files or in the log.
    [[nodiscard]] std::string lichessToken() const;
    void setLichessToken(const std::string& token) const;

    // The tester's name and site written in the exports for a rating list, in settings.json.
    [[nodiscard]] std::pair<std::string, std::string> submissionTester() const;
    void setSubmissionTester(const std::string& name, const std::string& site) const;

    [[nodiscard]] std::vector<BookEntry> loadBooks() const;
    void saveBooks(const std::vector<BookEntry>& books) const;

    [[nodiscard]] MatchSetup loadSetup() const;
    void saveSetup(const MatchSetup& setup) const;

    // The user's own layouts of the tournament workspace (layouts.json).
    [[nodiscard]] std::vector<layout::Layout> loadLayouts() const;
    void saveLayouts(const std::vector<layout::Layout>& layouts) const;

    // Tournaments waiting to be started one after the other (queue.json).
    [[nodiscard]] std::vector<QueuedTournament> loadQueue() const;
    void saveQueue(const std::vector<QueuedTournament>& queue) const;

    // The tournament running now (running.json, see RunningMarker); nullopt removes it.
    [[nodiscard]] std::optional<RunningMarker> runningMarker() const;
    void setRunningMarker(const std::optional<RunningMarker>& marker) const;
    // Removes it if it names the tournament in `dir`, not one another window of the profile wrote since.
    void clearRunningMarker(const std::string& dir) const;

   private:
    // settings.json, an empty object when it is missing, unreadable or not an object.
    [[nodiscard]] nlohmann::json settings() const;

    std::filesystem::path root_;
    std::filesystem::path runs_dir_;
};

// Reads a JSON file into T, nullopt when it is missing or unreadable.
template <typename T>
std::optional<T> readJson(const std::filesystem::path& file);
// A setup the program saved in the profile (a template), as readJson() reads it, then upgradeSavedSetup(); a setup
// file given to --run is read by readJson(), as it is.
std::optional<MatchSetup> readSavedSetup(const std::filesystem::path& file);

// Writes through a temporary file synced to the disk, so a crash or a power cut never leaves half a file behind
// (see tour::replaceFile: the file keeps its permissions). False on failure, with the reason in `error`. With
// `backup`, the previous content is kept as <file>.bak.
bool writeJson(const std::filesystem::path& file, const nlohmann::json& value, std::string* error = nullptr,
               bool backup = false);

}  // namespace fcg
