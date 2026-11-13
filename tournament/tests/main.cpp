#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.hpp>

#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#    include <csignal>
#endif

#include <tour/book_adapter.hpp>

namespace fastchess {
const char* version = "tournament-tests ";
}

int main(int argc, char** argv) {
#ifndef _WIN32
    // As in fastchess-gui: a write to an engine that just died (one that never starts, one that crashes) must not
    // end the tests.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // The book adapter: the tests start the engines that have a book of their own through this program, as
    // fastchess-gui does.
    if (argc >= 2 && std::string_view(argv[1]) == "--book-adapter") {
        return fcg::tour::runBookAdapter(std::vector<std::string>(argv + 2, argv + argc));
    }
    return doctest::Context(argc, argv).run();
}
