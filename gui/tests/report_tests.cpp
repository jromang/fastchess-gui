#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>

#include <fmt/include/fmt/format.h>

#include "report.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

// The value of a query parameter, decoded.
std::string param(const std::string& url, const std::string& name) {
    const auto at = url.find("&" + name + "=");
    if (at == std::string::npos) return "<none>";
    const auto start = at + name.size() + 2;
    const auto end   = url.find('&', start);
    const auto value = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            out += static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += value[i];
        }
    }
    return out;
}

}  // namespace

TEST_CASE("report: the issue's address, its fields filled") {
    report::Facts facts;
    facts.version  = "fastchess-gui 0.5.9";
    facts.system   = "System: Ubuntu 24.04 LTS, Linux 6.8.0 (x86_64)\nGraphics: Mesa Intel(R) UHD 620 · OpenGL 4.6";
    facts.log      = {"fastchess-gui 0.5.9 — 4 physical cores, 8 threads", "! Cannot start the match: x & y = 100%"};
    const auto url = report::issueUrl(facts);

    CHECK(url.rfind("https://github.com/jromang/fastchess-gui/issues/new?template=bug_report.yml&", 0) == 0);
    CHECK(param(url, "version") == "fastchess-gui 0.5.9");
    CHECK(param(url, "system") == facts.system);
    CHECK(param(url, "log") == facts.log[0] + "\n" + facts.log[1] + "\n");
    CHECK(param(url, "seen") == "<none>");  // the user says what went wrong
    // Nothing but safe characters in the address: no space, no line break, no raw & or = inside a value.
    CHECK(std::regex_match(url, std::regex(R"([A-Za-z0-9:/?=&%._~-]+)")));

    facts.seen = "At startup: OpenGL 3.2 is not available";
    CHECK(param(report::issueUrl(facts), "seen") == facts.seen);
}

TEST_CASE("report: a long log is cut to fit, its oldest lines first") {
    report::Facts facts;
    facts.version = "fastchess-gui 0.5.9";
    facts.system  = "System: Windows 10.0 build 22631 (x86_64)";
    for (int i = 0; i < 500; ++i) facts.log.push_back(fmt::format("line {} of the log, with some text: é ✓", i));
    const auto url = report::issueUrl(facts, 7000);
    CHECK(url.size() <= 7000);
    CHECK(url.size() > 6000);  // the room is used
    const auto log = param(url, "log");
    CHECK(log.find("line 499 of") != std::string::npos);                     // the newest kept
    CHECK(log.find("line 0 of") == std::string::npos);                       // the oldest left out
    CHECK(log.rfind("line 499", std::string::npos) > log.find("line 498"));  // in order
    CHECK(param(url, "version") == "fastchess-gui 0.5.9");

    // A huge line is cut, and a huge system description too: the version always goes.
    facts.log      = {std::string(100000, 'x')};
    facts.system   = std::string(50000, 's');
    const auto big = report::issueUrl(facts, 7000);
    CHECK(big.size() <= 7000);
    CHECK(param(big, "version") == "fastchess-gui 0.5.9");
    CHECK(param(big, "log").size() < 1000);
}

TEST_CASE("report: the last lines of the log file") {
    const auto dir = fs::temp_directory_path() /
                     fmt::format("fcg-tests-report-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(dir);
    const auto file = dir / "fastchess-gui.log";
    CHECK(report::lastLines(file, 10).empty());  // none yet

    {
        std::ofstream out(file, std::ios::binary);
        out << "first\r\nsecond\nthird\n";
    }
    CHECK(report::lastLines(file, 10) == std::vector<std::string>{"first", "second", "third"});
    CHECK(report::lastLines(file, 2) == std::vector<std::string>{"second", "third"});

    // A long session: only the end is read, starting at a whole line.
    {
        std::ofstream out(file, std::ios::binary);
        for (int i = 0; i < 20000; ++i) out << "line number " << i << "\n";
    }
    const auto last = report::lastLines(file, 3);
    CHECK(last == std::vector<std::string>{"line number 19997", "line number 19998", "line number 19999"});
    const auto many = report::lastLines(file, 100000);
    REQUIRE_FALSE(many.empty());
    CHECK(many.front().rfind("line number ", 0) == 0);
    CHECK(many.size() < 20000);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("report: the system named") {
    const auto name = report::systemName();
    MESSAGE(name);
    CHECK_FALSE(name.empty());
#ifdef _WIN32
    CHECK(name.rfind("Windows", 0) == 0);
#elif defined(__APPLE__)
    CHECK(name.rfind("macOS", 0) == 0);
#else
    CHECK(name.find("Linux") != std::string::npos);
#endif
}

TEST_CASE("report: the form's field ids are those the address fills") {
#ifdef FCG_SOURCE_DIR
    std::ifstream in(fs::path(FCG_SOURCE_DIR) / ".github" / "ISSUE_TEMPLATE" / "bug_report.yml");
    REQUIRE(in);
    const std::string form((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (const char* id : {"version", "system", "log", "seen"}) {
        CHECK_MESSAGE(form.find(fmt::format("id: {}\n", id)) != std::string::npos, id);
    }
#endif
}
