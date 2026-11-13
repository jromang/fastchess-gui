#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.hpp>

#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#    include <fstream>

#    include <windows.h>
#else
#    include <csignal>
#endif

#include "wb2uci.hpp"

namespace fastchess {
const char* version = "gui-tests ";
}

int main(int argc, char** argv) {
#ifndef _WIN32
    // As in fastchess-gui: a write to an engine that just died must not end the tests.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // The WinBoard adapter: the tests start their WinBoard engines through this program, as fastchess-gui does.
    if (argc >= 2 && std::string_view(argv[1]) == "--wb2uci") {
        return fcg::wb2uci::run(std::vector<std::string>(argv + 2, argv + argc));
    }
#ifdef _WIN32
    // What an engine starts, for the test of the engine's job (run_tests.cpp): it reads nothing and leaves the console
    // it was given, so that only its job can end it. Once it runs, it writes its id to the file given (whole, by a
    // rename), and it ends on its own after a minute.
    if (argc >= 3 && std::string_view(argv[1]) == "--job-child") {
        FreeConsole();
        const std::string file = argv[2];
        std::ofstream(file + ".tmp") << GetCurrentProcessId();
        MoveFileExA((file + ".tmp").c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING);
        Sleep(60000);
        return 0;
    }
#endif
    return doctest::Context(argc, argv).run();
}
