#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "wb2uci.hpp"

namespace fastchess {
const char* version = "gui-tests ";
}

int main(int argc, char** argv) {
    // The WinBoard adapter: the tests start their WinBoard engines through this program, as fastchess-gui does.
    if (argc >= 2 && std::string_view(argv[1]) == "--wb2uci") {
        return fcg::wb2uci::run(std::vector<std::string>(argv + 2, argv + argc));
    }
    return doctest::Context(argc, argv).run();
}
