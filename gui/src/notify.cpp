#include "notify.hpp"

#include <SDL3/SDL_video.h>

#ifndef _WIN32
#    include <spawn.h>
#    include <sys/wait.h>

#    include <thread>
extern char** environ;
#endif

namespace fcg {

void notifyRunFinished(SDL_Window* window, [[maybe_unused]] const std::string& title,
                       [[maybe_unused]] const std::string& body) {  // the flash only, on Windows
    if (window) SDL_FlashWindow(window, SDL_FLASH_UNTIL_FOCUSED);

#ifndef _WIN32
    // notify-send may be missing or slow (it talks to D-Bus): never block the GUI on it.
    std::thread([title, body] {
#    ifdef __APPLE__
        // osascript instead, the title and the text given as arguments, not written into the script.
        const char* argv[] = {"osascript",
                              "-e",
                              "on run argv",
                              "-e",
                              "display notification (item 2 of argv) with title (item 1 of argv)",
                              "-e",
                              "end run",
                              title.c_str(),
                              body.c_str(),
                              nullptr};
#    else
        const char* argv[] = {"notify-send", "-a", "fastchess-gui", title.c_str(), body.c_str(), nullptr};
#    endif
        pid_t pid = 0;
        if (posix_spawnp(&pid, argv[0], nullptr, nullptr, const_cast<char* const*>(argv), environ) == 0) {
            int status = 0;
            waitpid(pid, &status, 0);
        }
    }).detach();
#endif
}

}  // namespace fcg
