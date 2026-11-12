// The command palette's matching, and the guide's sections the pages lead to.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "guide.hpp"
#include "palette.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> ranked(std::string_view query, const std::vector<std::string>& texts,
                                const std::vector<std::string>* extra = nullptr) {
    std::vector<std::string> out;
    for (const int i : palette::rank(query, texts, extra)) out.push_back(texts[static_cast<std::size_t>(i)]);
    return out;
}

}  // namespace

TEST_CASE("palette: the letters typed, in order, whatever their case") {
    CHECK(palette::matchScore("", "Engines") == 0);
    CHECK(palette::matchScore("eng", "Engines") > 0);
    CHECK(palette::matchScore("ENG", "engines") > 0);
    CHECK(palette::matchScore("egn", "Engines") > 0);  // e, g, n apart
    CHECK(palette::matchScore("sen", "Engines") < 0);  // not in that order
    CHECK(palette::matchScore("enginesx", "Engines") < 0);
    CHECK(palette::matchScore("x", "") < 0);
    // Letters outside ASCII are compared as they are.
    CHECK(palette::matchScore("é", "Réglages") > 0);
    CHECK(palette::matchScore("e", "Réglages") > 0);
}

TEST_CASE("palette: the start of the text first, then the start of a word, then inside a word, then apart") {
    const int prefix = palette::matchScore("sto", "Stockfish 17");
    const int word   = palette::matchScore("sto", "Finish then stop");
    const int inside = palette::matchScore("sto", "Pistol");
    const int apart  = palette::matchScore("sto", "Settings of the tournament");
    CHECK(prefix > word);
    CHECK(word > inside);
    CHECK(inside > apart);
    CHECK(apart >= 0);

    // Among the letters apart, those at the start of words, side by side, come first.
    CHECK(palette::matchScore("rl", "Rating list") > palette::matchScore("rl", "Search all"));
    // As well matched, the shorter text first.
    CHECK(palette::matchScore("eng", "Engines") > palette::matchScore("eng", "Engine options of the setup"));
}

TEST_CASE("palette: rank keeps the matches, best first, equal ones in their order") {
    const std::vector<std::string> texts = {"Settings", "Stop now (running games are lost)",   "Stockfish 17",
                                            "Books",    "Finish the running games, then stop", "Stockfish 16"};
    CHECK(ranked("", texts) == texts);
    CHECK(ranked("sto", texts) == std::vector<std::string>{"Stockfish 17", "Stockfish 16",
                                                           "Stop now (running games are lost)",
                                                           "Finish the running games, then stop"});
    CHECK(ranked("zzz", texts).empty());
    CHECK(ranked("book", texts) == std::vector<std::string>{"Books"});
}

TEST_CASE("palette: several words, each in the text, in any order") {
    CHECK(palette::matchScore("stock 17", "Stockfish 17") >= 15000);
    CHECK(palette::matchScore("17 stock", "Stockfish 17") >= 15000);
    CHECK(palette::matchScore("ock 17", "Stockfish 17") >= 10000);  // inside a word
    CHECK(palette::matchScore("stock 18", "Stockfish 17") < 0);
    // A word at the start of the text comes before words.
    CHECK(palette::matchScore("stock", "Stockfish 17") > palette::matchScore("stock 17", "Stockfish 17"));
}

TEST_CASE("palette: what an entry belongs to is searched too, after the entry itself") {
    const std::vector<std::string> texts = {"An engine list…", "Import results", "Live"};
    const std::vector<std::string> extra = {"Engines · Import…", "", "Tournament A"};
    // "Import results" matches by itself, "An engine list…" by its menu: after it.
    CHECK(ranked("import", texts, &extra) == std::vector<std::string>{"Import results", "An engine list…"});
    CHECK(ranked("live tour", texts, &extra) == std::vector<std::string>{"Live"});
    CHECK(ranked("import", texts) == std::vector<std::string>{"Import results"});
    const std::vector<std::string> standings = {"Standings"};
    CHECK(ranked("copy stand", {"Copy: As text"}, &standings).size() == 1);

    // The name of a tournament found in what its actions belong to: they come before letters apart in a text, and
    // letters apart over a text and what it belongs to do not count.
    const std::vector<std::string> actions = {"Settings of the books", "Pause", "A rating list…"};
    const std::vector<std::string> where   = {"", "Stockfish 17 vs 16", "Engines · Import…"};
    CHECK(ranked("sto", actions, &where) == std::vector<std::string>{"Pause", "Settings of the books"});
}

TEST_CASE("guide: anchors made from headings as GitHub makes them") {
    CHECK(guide::slug("The window") == "the-window");
    CHECK(guide::slug("Changing a running tournament") == "changing-a-running-tournament");
    CHECK(guide::slug("What's new? (0.4.13)") == "whats-new-0413");
    CHECK(guide::slug("SPRT & Elo: the basics") == "sprt--elo-the-basics");
    CHECK(guide::slug("`settings.json` file_name") == "settingsjson-file_name");
    CHECK(guide::slug("Équipe") == "Équipe");  // only ASCII letters are lowered here

    const auto anchors = guide::anchors(
        "# Guide\n\nText # not a heading\n## The window ##\n```\n# code\n```\n"
        "### Files\n#### Files\n#NoSpace\n## Files\r\n");
    CHECK(anchors == std::vector<std::string>{"guide", "the-window", "files", "files-1", "files-2"});
}

TEST_CASE("guide: every section the pages lead to is a heading of docs/guide.md") {
    const char* env     = std::getenv("FCG_SOURCE_DIR");
    const fs::path path = fs::path(env ? env : FCG_SOURCE_DIR) / "docs" / "guide.md";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        MESSAGE("guide not found: " << path.string());
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto anchors = guide::anchors(text);
    CHECK(anchors.size() > 10);
    for (int i = 0; i < static_cast<int>(guide::Section::Count); ++i) {
        const auto section = static_cast<guide::Section>(i);
        CAPTURE(guide::anchor(section));
        CHECK(std::string(guide::anchor(section)).size() > 0);
        CHECK(std::find(anchors.begin(), anchors.end(), guide::anchor(section)) != anchors.end());
        CHECK(guide::url(section) ==
              std::string("https://github.com/jromang/fastchess-gui/blob/gui/docs/guide.md#") + guide::anchor(section));
    }
}
