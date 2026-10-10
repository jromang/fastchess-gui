#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace fcg {

// Native file dialogs (SDL3: portal or zenity on Linux, the shell dialogs on Windows).
// SDL may answer on another thread, so answers are queued and picked up with take().
class Dialogs {
   public:
    enum class Purpose { AddEngine, EngineCommand, EngineDir, Book, Tablebases, ExportGames, ExportAll };

    struct Answer {
        Purpose purpose;
        std::string path;
    };

    explicit Dialogs(SDL_Window* window) : window_(window) {}

    void openFile(Purpose purpose, const std::vector<std::pair<std::string, std::string>>& filters = {},
                  const std::string& start = {});
    void openFolder(Purpose purpose, const std::string& start = {});
    void saveFile(Purpose purpose, const std::vector<std::pair<std::string, std::string>>& filters,
                  const std::string& start = {});

    [[nodiscard]] std::optional<Answer> take();

    [[nodiscard]] bool busy() const;

   private:
    struct Request;

    static void callback(void* userdata, const char* const* files, int filter);

    SDL_Window* window_;

    mutable std::mutex mutex_;
    std::vector<Answer> answers_;
    int pending_ = 0;
};

}  // namespace fcg
