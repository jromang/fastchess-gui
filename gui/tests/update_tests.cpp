#include <doctest/doctest.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include <fmt/include/fmt/format.h>

#include "https.hpp"
#include "storage.hpp"
#include "update_check.hpp"

using namespace fcg;
using namespace fcg::update;
namespace fs = std::filesystem;

TEST_CASE("new version: versions compared number by number") {
    CHECK(compareVersions("0.5.9", "0.5.9") == 0);
    CHECK(compareVersions("v0.5.9", "0.5.9") == 0);
    CHECK(compareVersions("0.5.10", "0.5.9") > 0);  // not as text
    CHECK(compareVersions("0.6.0", "0.5.9") > 0);
    CHECK(compareVersions("1.0", "0.99.99") > 0);
    CHECK(compareVersions("0.6", "0.6.0") == 0);
    CHECK(compareVersions("0.6.0.1", "0.6.0") > 0);
    CHECK(compareVersions("0.5.9", "0.6.0") < 0);
    // A pre-release comes before its release, after the version before it.
    CHECK(compareVersions("0.6.0-rc1", "0.6.0") < 0);
    CHECK(compareVersions("0.6.0-rc1", "0.5.9") > 0);
    CHECK(compareVersions("0.6.0-rc2", "0.6.0-rc1") > 0);
    // Nonsense is not newer than anything.
    CHECK(compareVersions("", "0.5.9") < 0);
    CHECK(compareVersions("latest", "0.0.1") < 0);
    CHECK(compareVersions("99999999999999999999.0", "1.0") > 0);  // no overflow
}

TEST_CASE("new version: GitHub's answer read") {
    const auto r = parseRelease(R"({
        "tag_name": "v0.6.0", "name": "Version 0.6.0", "draft": false, "prerelease": false,
        "html_url": "https://github.com/jromang/fastchess-gui/releases/tag/v0.6.0",
        "body": "## What changes\n\n- Several tournaments at once.\n"})");
    REQUIRE(r.has_value());
    CHECK(r->version == "0.6.0");
    CHECK(r->url == "https://github.com/jromang/fastchess-gui/releases/tag/v0.6.0");
    CHECK(r->notes.find("Several tournaments") != std::string::npos);

    // A page elsewhere is not opened: the releases' page instead.
    const auto other = parseRelease(R"({"tag_name": "0.6.0", "html_url": "https://example.com/x", "body": null})");
    REQUIRE(other.has_value());
    CHECK(other->url == RELEASES_PAGE);
    CHECK(other->notes.empty());

    CHECK_FALSE(parseRelease(R"({"tag_name": "0.6.0", "prerelease": true})").has_value());
    CHECK_FALSE(parseRelease(R"({"tag_name": "0.6.0", "draft": true})").has_value());
    CHECK_FALSE(parseRelease(R"({"message": "API rate limit exceeded"})").has_value());
    CHECK_FALSE(parseRelease("<html>").has_value());
    CHECK_FALSE(parseRelease(R"({"tag_name": 6})").has_value());

    // Very long notes are cut, not inside a letter.
    std::string body(19999, 'a');
    body += "é and more";
    const auto json = nlohmann::json{{"tag_name", "1.0"}, {"body", body}}.dump();
    const auto cut  = parseRelease(json);
    REQUIRE(cut.has_value());
    CHECK(cut->notes.size() < body.size());
    CHECK(cut->notes.rfind("a\n…") == cut->notes.size() - std::string("a\n…").size());
}

TEST_CASE("new version: asked at most once a day") {
    const int64_t now = 1'800'000'000;
    CHECK(checkDue(0, now));
    CHECK_FALSE(checkDue(now - 3600, now));
    CHECK_FALSE(checkDue(now - 24 * 3600 + 1, now));
    CHECK(checkDue(now - 24 * 3600, now));
    CHECK(checkDue(now + 3600, now));  // the clock went back
}

TEST_CASE("new version: the notice, for a newer version not dismissed") {
    Settings s;
    s.enabled = true;
    s.latest  = Release{"0.6.0", "notes", RELEASES_PAGE};
    CHECK(notice(s, "0.5.9") == &*s.latest);
    CHECK(notice(s, "0.6.0") == nullptr);
    CHECK(notice(s, "0.6.1") == nullptr);
    s.dismissed = "0.6.0";
    CHECK(notice(s, "0.5.9") == nullptr);
    s.latest->version = "0.6.1";  // a newer one shows again
    CHECK(notice(s, "0.5.9") != nullptr);
    s.enabled = false;  // the user no longer wants to know
    CHECK(notice(s, "0.5.9") == nullptr);
}

TEST_CASE("new version: what settings.json keeps") {
    const auto dir = fs::temp_directory_path() /
                     fmt::format("fcg-tests-update-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    const Storage storage(dir);
    auto s = storage.updateSettings();
    CHECK_FALSE(s.enabled);  // off by default
    CHECK(s.checked == 0);
    CHECK_FALSE(s.latest.has_value());

    s.enabled   = true;
    s.checked   = 1'800'000'000;
    s.latest    = Release{"0.6.0", "## Notes", "https://github.com/jromang/fastchess-gui/releases/tag/v0.6.0"};
    s.dismissed = "0.5.10";
    storage.setUpdateSettings(s);
    const auto read = storage.updateSettings();
    CHECK(read.enabled);
    CHECK(read.checked == 1'800'000'000);
    REQUIRE(read.latest.has_value());
    CHECK(read.latest->version == "0.6.0");
    CHECK(read.latest->notes == "## Notes");
    CHECK(read.dismissed == "0.5.10");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("new version: GitHub's API trusted, its latest release read (FCG_NET_TESTS=1 only)") {
    const char* wanted = std::getenv("FCG_NET_TESTS");
    if (!wanted || std::string(wanted) != "1") {
        MESSAGE("needs the internet: FCG_NET_TESTS=1, skipped");
        return;
    }
    Checker checker;
    checker.start();
    std::optional<Checker::Answer> answer;
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(40);
    while (!(answer = checker.poll()) && std::chrono::steady_clock::now() < give_up) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    REQUIRE(answer.has_value());
    REQUIRE_MESSAGE(answer->release.has_value(), answer->error);
    CHECK(compareVersions(answer->release->version, "0.5.0") > 0);
    CHECK(answer->release->url.rfind("https://github.com/jromang/fastchess-gui/", 0) == 0);
}
