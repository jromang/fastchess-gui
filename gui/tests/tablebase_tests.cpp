// The Syzygy tablebases a tournament uses: the tables of each piece count, and what a folder holds.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>

#include "tablebases.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

bool has(const std::vector<std::string>& v, const std::string& name) {
    return std::find(v.begin(), v.end(), name) != v.end();
}

// A Syzygy file: 16 bytes more than a multiple of 64 long, or a damaged one when `size` says so.
void table(const fs::path& path, std::size_t size = 64 + 16) { std::ofstream(path) << std::string(size, 'x'); }

std::string joined(const std::vector<fs::path>& dirs) {
#ifdef _WIN32
    const char* separator = ";";
#else
    const char* separator = ":";
#endif
    std::string out;
    for (const auto& d : dirs) out += (out.empty() ? "" : separator) + d.string();
    return out;
}

}  // namespace

TEST_CASE("Syzygy tables: every material of each piece count, named as Syzygy names its files") {
    CHECK(syzygyTables(2).empty());
    CHECK(syzygyTables(8).empty());
    CHECK(syzygyTables(3) == std::vector<std::string>{"KQvK", "KRvK", "KBvK", "KNvK", "KPvK"});
    CHECK(syzygyTables(4).size() == 30);
    CHECK(syzygyTables(5).size() == 110);
    CHECK(syzygyTables(6).size() == 365);
    CHECK(syzygyTables(7).size() == 1001);

    // The side with more pieces first; with as many, the stronger one, each side's pieces in the order KQRBNP.
    const auto four = syzygyTables(4), five = syzygyTables(5), six = syzygyTables(6);
    CHECK(has(four, "KQvKQ"));
    CHECK(has(four, "KRvKN"));
    CHECK_FALSE(has(four, "KNvKR"));
    CHECK(has(four, "KBNvK"));
    CHECK(has(four, "KPvKP"));
    CHECK(has(five, "KBNvKR"));
    CHECK_FALSE(has(five, "KRvKBN"));
    CHECK(has(five, "KQRvKQ"));
    CHECK(has(six, "KRPPvKR"));
    CHECK(has(six, "KQPvKRP"));
    CHECK_FALSE(has(six, "KRPvKQP"));
    CHECK(has(six, "KPPPPvK"));

    // Each table once.
    for (int pieces = 3; pieces <= 7; ++pieces) {
        const auto tables = syzygyTables(pieces);
        CHECK(std::set<std::string>(tables.begin(), tables.end()).size() == tables.size());
        for (const auto& t : tables) CHECK(static_cast<int>(t.size()) == pieces + 1);
    }
}

TEST_CASE("Syzygy path: its folders, separated as the engines read them") {
#ifdef _WIN32
    CHECK(splitTablebasePath("C:\\TB\\wdl;D:\\TB6") == std::vector<std::string>{"C:\\TB\\wdl", "D:\\TB6"});
#else
    CHECK(splitTablebasePath("/tb/345:/tb/6") == std::vector<std::string>{"/tb/345", "/tb/6"});
    CHECK(splitTablebasePath("/tb/345::/tb/6:") == std::vector<std::string>{"/tb/345", "/tb/6"});
#endif
    CHECK(splitTablebasePath("").empty());
}

TEST_CASE("Syzygy check: the tables missing up to the largest found, and the files cut short") {
    const auto root = fs::temp_directory_path() / "fcg-gui-tb-check";
    fs::remove_all(root);
    const auto a = root / "345", b = root / "6";
    fs::create_directories(a);
    fs::create_directories(b);

    // Every table of 3 and 4 pieces, the 5-piece ones but two, one of them cut short.
    for (int pieces = 3; pieces <= 5; ++pieces) {
        for (const auto& t : syzygyTables(pieces)) {
            if (t == "KQRvKQ" || t == "KBNvKR") continue;
            table(a / (t + ".rtbw"));
            table(a / (t + ".rtbz"), 128 + 16);
        }
    }
    table(a / "KBNvKR.rtbw", 1000);
    table(a / "KQvK.rtbz", 0);
    std::ofstream(a / "readme.txt") << "not a table";

    auto check = checkTablebases(a.string());
    CHECK(check.missing_dirs.empty());
    CHECK(check.largest == 5);
    CHECK(check.wdl_files == 5 + 30 + 108);
    REQUIRE(check.missing.size() == 1);
    CHECK(check.missing[5] == std::vector<std::string>{"KQRvKQ", "KBNvKR"});
    CHECK(check.damaged == std::vector<std::string>{"KBNvKR.rtbw", "KQvK.rtbz"});

    // A second folder with a few 6-piece tables: the others are missing.
    table(b / "KRPPvKR.rtbw");
    table(b / "KQRvKQ.rtbw");
    check = checkTablebases(joined({a, b, root / "gone"}));
    CHECK(check.missing_dirs == std::vector<std::string>{(root / "gone").string()});
    CHECK(check.largest == 6);
    CHECK(check.missing[5] == std::vector<std::string>{"KBNvKR"});
    CHECK(check.missing[6].size() == 364);
    CHECK_FALSE(has(check.missing[6], "KRPPvKR"));

    // A folder without a table.
    check = checkTablebases(root.string());
    CHECK(check.wdl_files == 0);
    CHECK(check.largest == 0);
    CHECK(check.missing.empty());

    fs::remove_all(root);
}

TEST_CASE("Syzygy checks: off the UI thread, the last result kept by path") {
    const auto root = fs::temp_directory_path() / "fcg-gui-tb-checks";
    fs::remove_all(root);
    fs::create_directories(root);
    table(root / "KQvK.rtbw");

    TablebaseChecks checks;
    const TablebaseCheck* found = nullptr;
    for (int i = 0; i < 500 && !found; ++i) {
        found = checks.get(root.string());
        if (!found) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(found);
    CHECK(found->largest == 3);
    CHECK(found->missing.at(3).size() == 4);
    fs::remove_all(root);
}
