#include "notify.hpp"

#include <SDL3/SDL_video.h>

#ifndef _WIN32
#    include <spawn.h>
#    include <sys/wait.h>

#    include <thread>
extern char** environ;
#endif

namespace fcg {

void notifyRunFinished(SDL_Window* window, const std::string& title, const std::string& body) {
    if (window) SDL_FlashWindow(window, SDL_FLASH_UNTIL_FOCUSED);

#ifndef _WIN32
    // notify-send may be missing or slow (it talks to D-Bus): never block the GUI on it.
    std::thread([title, body] {
        const char* argv[] = {"notify-send", "-a", "fastchess-gui", title.c_str(), body.c_str(), nullptr};
        pid_t pid          = 0;
        if (posix_spawnp(&pid, "notify-send", nullptr, nullptr, const_cast<char* const*>(argv), environ) == 0) {
            int status = 0;
            waitpid(pid, &status, 0);
        }
    }).detach();
#endif
}

}  // namespace fcg
