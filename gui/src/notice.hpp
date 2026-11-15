#pragma once

// An actionable message: what is wrong, naming the engine (and its option or file) and the resource (memory, cores,
// a folder), and the fixes, one button each. The setup summary, the home page and the tournament header show them.

#include <string>
#include <vector>

namespace fcg {

// What fixes a problem. The App carries it out (App::applyFix); the views only show its button.
struct Fix {
    enum class Kind {
        OpenEngine,               // `engine` selected on the Engines page
        LocateProgram,            // the file dialog for the program of `engine`
        LocateDir,                // the folder dialog for the working directory of `engine`
        LocateBook,               // the file dialog for the setup's opening book
        LocateTablebases,         // the folder dialog for the setup's tablebases (the adjudication's)
        LowerEngineThreads,       // the Threads option of `engine`, in the library, set to `value`
        LowerParticipantThreads,  // the Threads of participant `index` of the setup set to `value`
        LowerCommonThreads,       // the Threads of the setup's common settings set to `value`
        LowerTablebasePieces,     // the tablebase adjudication up to `value` pieces
        OpenLog,                  // the Log page
        OpenFolder,               // `path` in the file manager of the system
        ChangeTournamentsFolder,  // the folder dialog for where new tournaments are written
        CopyToMachine,            // the copy of `engine` to the machine whose id is `path` offered (Settings)
    };
    Kind kind;
    std::string label;    // the button's
    std::string tooltip;  // what it does, and where it applies (the library, this tournament); may be empty
    std::string engine;
    std::string path;
    int value = 0;
    int index = -1;

    bool operator==(const Fix&) const = default;
};

// The fixes, with their labels in the language of the interface.
[[nodiscard]] Fix openEngineFix(const std::string& engine);
[[nodiscard]] Fix locateProgramFix(const std::string& engine);
[[nodiscard]] Fix locateDirFix(const std::string& engine);
[[nodiscard]] Fix locateBookFix();
[[nodiscard]] Fix locateTablebasesFix();
[[nodiscard]] Fix lowerEngineThreadsFix(const std::string& engine, int threads);
[[nodiscard]] Fix lowerParticipantThreadsFix(const std::string& engine, int index, int threads);
[[nodiscard]] Fix lowerCommonThreadsFix(int threads);
[[nodiscard]] Fix lowerTablebasePiecesFix(int pieces);
[[nodiscard]] Fix openLogFix();
[[nodiscard]] Fix openFolderFix(const std::string& path);
[[nodiscard]] Fix changeTournamentsFolderFix();
[[nodiscard]] Fix copyToMachineFix(const std::string& engine, const std::string& machine_id,
                                   const std::string& machine_name);

struct Notice {
    enum class Level { Ok, Warning, Error };
    Level level = Level::Warning;
    std::string text;
    std::string detail;  // shown when the mouse is on the text (the whole list of files missing...); may be empty
    std::vector<Fix> fixes;
};

// Draws `notice` as the lines of the setup summary are: a mark (✓, or ! orange for a warning, red for an error), the
// text wrapped, then its fixes, small buttons on the next line. `id` tells its buttons from another notice's.
// Returns the fix clicked, or nullptr.
const Fix* drawNotice(const Notice& notice, const char* id);

// drawNotice() of each, the fix clicked (if any) in `clicked`. Returns true when one was.
bool drawNotices(const std::vector<Notice>& notices, const char* id, Fix& clicked);

}  // namespace fcg
