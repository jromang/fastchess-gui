#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.hpp>

#ifndef _WIN32
#    include <csignal>
#endif

namespace fastchess {
const char* version = "tournament-tests ";
}

int main(int argc, char** argv) {
#ifndef _WIN32
    // As in fastchess-gui: a write to an engine that just died (one that never starts, one that crashes) must not
    // end the tests.
    std::signal(SIGPIPE, SIG_IGN);
#endif
    return doctest::Context(argc, argv).run();
}
