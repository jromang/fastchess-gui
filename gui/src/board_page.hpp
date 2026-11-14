#pragma once

// The state of the Board page (view_board.cpp) and of the engine console of the Engines page
// (view_engine_console.cpp): what they show, apart from the App that draws them.

#include <string>
#include <vector>

#include "engine_client.hpp"

namespace fcg {

// The console of an engine of the library: the lines exchanged with it, typed by the user.
struct EngineConsole {
    std::string engine;  // the library engine, by name
    bool raw = false;    // a WinBoard engine spoken to in its own protocol, without the adapter
    EngineProcess process;
    std::vector<EngineProcess::Line> log;
    std::string input;
    std::vector<std::string> history;
    int history_pos = -1;  // browsing the history with Up and Down; -1: the line being typed
    bool follow     = true;
    bool focus      = true;  // the input line takes the keyboard on the next frame
    bool shown      = true;  // in the engine's card, instead of its settings
    // UCI: the library's option values are sent once the engine answers uciok.
    std::vector<std::pair<std::string, std::string>> options;
    bool options_due = false;
};

// The quick commands of the console, as typed: UCI's, or WinBoard's when spoken to in its own protocol.
[[nodiscard]] const std::vector<const char*>& consoleCommands(bool winboard);

}  // namespace fcg
