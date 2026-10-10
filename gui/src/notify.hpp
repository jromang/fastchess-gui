#pragma once

#include <string>

struct SDL_Window;

namespace fcg {

// Tells the user a run is over: flashes the window and, on Linux, sends a desktop notification.
void notifyRunFinished(SDL_Window* window, const std::string& title, const std::string& body);

}  // namespace fcg
