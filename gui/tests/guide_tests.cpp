// The user guide built into the program (docs/design/help.md): its anchors, its translations kept up to date with the
// English text, the topics the program opens, the Markdown the guide panel draws, its line breaking and its search.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "guide.hpp"
#include "markdown.hpp"
#include "zip.hpp"

using namespace fcg;

namespace {

// Each byte one unit wide, a space half a unit: what the layout tests measure with.
float units(std::string_view text, uint8_t) {
    float w = 0;
    for (const char c : text) w += c == ' ' ? 0.5f : (static_cast<unsigned char>(c) & 0xC0) == 0x80 ? 0.0f : 1.0f;
    return w;
}

std::vector<std::string> lineTexts(const std::vector<md::Run>& runs, const std::vector<md::Line>& lines) {
    std::vector<std::string> out;
    for (const auto& l : lines) {
        std::string s;
        for (const auto& p : l.pieces) {
            if (!s.empty() && p.x > 0 && s.back() != ' ') {
                // Pieces placed apart on the line: a space between them in the text.
                const auto& prev = l.pieces[&p - &l.pieces[0] - 1];
                if (p.x > prev.x + prev.width + 0.01f) s += ' ';
            }
            s += runs[p.run].text.substr(p.begin, p.end - p.begin);
        }
        out.push_back(s);
    }
    return out;
}

std::string sectionBody(const guide::Doc& d, const guide::Heading& h) { return d.text.substr(h.body, h.end - h.body); }

// Text between < and > outside code (a placeholder such as <engine>): GitHub and the panel take it for HTML and show
// nothing of it. <kbd> and comments are meant; \\<engine\\> is escaped.
std::vector<std::string> rawTags(const std::string& text) {
    static const std::regex span("`+[^`]*`+");
    static const std::regex tag(R"((^|[^\\])(<(?!/?kbd>|!--)[A-Za-z/][^>]*>))");
    std::vector<std::string> out;
    bool fenced    = false;
    std::size_t at = 0;
    while (at < text.size()) {
        auto end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const auto line = text.substr(at, end - at);
        at              = end + 1;
        if (line.rfind("```", 0) == 0) {
            fenced = !fenced;
            continue;
        }
        if (fenced) continue;
        const auto bare = std::regex_replace(line, span, "");
        for (std::sregex_iterator it(bare.begin(), bare.end(), tag), stop; it != stop; ++it) out.push_back((*it)[2]);
    }
    return out;
}

}  // namespace

TEST_CASE("guide: anchors made from headings as GitHub makes them") {
    CHECK(guide::slug("The window") == "the-window");
    CHECK(guide::slug("Changing a running tournament") == "changing-a-running-tournament");
    CHECK(guide::slug("What's new? (0.4.13)") == "whats-new-0413");
    CHECK(guide::slug("SPRT & Elo: the basics") == "sprt--elo-the-basics");
    CHECK(guide::slug("`settings.json` file_name") == "settingsjson-file_name");
    // Letters outside ASCII lowered too, the usual punctuation outside ASCII left out.
    CHECK(guide::slug("Équipe Über Œuvre") == "équipe-über-œuvre");
    CHECK(guide::slug("Новый турнир") == "новый-турнир");
    CHECK(guide::slug("«Démarrage» — rapide…") == "démarrage--rapide");
    CHECK(guide::slug("新建锦标赛：设置") == "新建锦标赛设置");

    const auto anchors = guide::anchors(
        "# Guide\n\nText # not a heading\n## The window ##\n```\n# code\n```\n"
        "### Files\n#### Files\n#NoSpace\n## Files\r\n");
    CHECK(anchors == std::vector<std::string>{"guide", "the-window", "files", "files-1", "files-2"});
}

TEST_CASE("guide: a translation's sections, tied to the English ones by their stamps") {
    const std::string fr =
        "# Guide\n\nIntro.\n\n## Fenêtre\n<!-- en: the-window 0123abcd -->\n\nTexte.\n\n### Sans tampon\n\nRien.\n";
    const auto hs = guide::headings(fr, true);
    REQUIRE(hs.size() == 3);
    CHECK(hs[0].id.empty());
    CHECK(hs[1].anchor == "fenêtre");
    CHECK(hs[1].id == "the-window");
    CHECK(hs[1].stamp == "0123abcd");
    CHECK(fr.substr(hs[1].body, hs[1].end - hs[1].body) == "\nTexte.\n\n");
    CHECK(hs[2].id.empty());

    // The hash of a section: its heading and its text, trailing spaces and blank lines at both ends left out.
    const std::string en = "## The window\n\n  Text.  \n\n\n## Next\n";
    const auto eh        = guide::headings(en, false);
    CHECK(guide::sectionText(en, eh[0]) == "## The window\n  Text.");
    CHECK(guide::hex8(guide::fnv1a("")) == "811c9dc5");
    CHECK(guide::hex8(guide::fnv1a("a")) == "e40c292c");
}

TEST_CASE("guide: the embedded guides, gzipped, read back whole") {
    const auto* en = guide::doc("en");
    REQUIRE(en);
    CHECK(en->text.size() > 100000);
    CHECK(en->text.substr(0, 26) == "# fastchess-gui user guide");
    CHECK(guide::doc("") == en);
    CHECK(guide::doc("xx") == nullptr);
    CHECK(guide::picture("screenshots/appearance.png").has_value());
    CHECK_FALSE(guide::picture("guide.md").has_value());

    const std::string text = "the guide, gzipped and back";
    CHECK(gunzipped(gzipped(text)) == text);
    auto broken = gzipped(text);
    broken[broken.size() - 6] ^= 1;  // the CRC
    CHECK_FALSE(gunzipped(broken).has_value());
    CHECK_FALSE(gunzipped("not gzip at all").has_value());
}

TEST_CASE("guide: English headings unique, every topic of the program one of them") {
    const auto* en = guide::doc("en");
    REQUIRE(en);
    std::set<std::string> ids;
    for (const auto& h : en->headings) {
        CAPTURE(h.title);
        CHECK(h.anchor == guide::slug(h.title));  // not made unique by a suffix: titles are unique
        ids.insert(h.id);
    }
    for (const char* topic : guide::allTopics()) {
        CAPTURE(topic);
        CHECK(ids.count(topic) == 1);
    }
    // Every link to a section reaches one, and no text between < and > outside code would vanish.
    for (const auto& h : en->headings) {
        const auto body = sectionBody(*en, h);
        CAPTURE(h.id);
        for (const auto& target : guide::sectionLinks(body)) {
            CAPTURE(target);
            CHECK(ids.count(target) == 1);
        }
        CHECK(rawTags(body).empty());
    }
}

TEST_CASE("guide: every translation is complete and up to date with the English text") {
    const auto* en = guide::doc("en");
    REQUIRE(en);
    const auto codes = guide::translations();
    // One for each language of the interface.
    for (const char* code : {"fr", "de", "es", "it", "pt_BR", "ru", "zh_CN"}) {
        CAPTURE(code);
        CHECK(std::find(codes.begin(), codes.end(), code) != codes.end());
    }
    for (const auto& code : codes) {
        CAPTURE(code);
        const auto* tr = guide::doc(code);
        REQUIRE(tr);
        std::set<std::string> anchors;
        for (const auto& h : tr->headings) anchors.insert(h.anchor);
        int last = -1;
        for (std::size_t i = 1; i < en->headings.size(); ++i) {
            const auto& e = en->headings[i];
            CAPTURE(e.id);
            const int t = tr->byId(e.id);
            CHECK(t >= 0);
            if (t < 0) continue;
            const auto& h = tr->headings[static_cast<std::size_t>(t)];
            CHECK(t > last);  // in the English order
            last = t;
            CHECK(h.level == e.level);
            // Made from the English text as it is now.
            CHECK(h.stamp == guide::hex8(guide::fnv1a(guide::sectionText(en->text, e))));
            CHECK(guide::shown(code, i).fallback == guide::Fallback::None);
            // Commands, files and options as in English.
            const auto en_body = sectionBody(*en, e);
            const auto tr_body = sectionBody(*tr, h);
            CHECK(guide::codeSpans(tr_body) == guide::codeSpans(en_body));
            CHECK(guide::codeBlocks(tr_body) == guide::codeBlocks(en_body));
            // Links to sections of its own file, and nothing that would vanish.
            for (const auto& target : guide::sectionLinks(tr_body)) {
                CAPTURE(target);
                CHECK(anchors.count(target) == 1);
            }
            CHECK(rawTags(tr_body).empty());
        }
        // No section that is no English one.
        for (std::size_t t = 1; t < tr->headings.size(); ++t) {
            CAPTURE(tr->headings[t].title);
            CHECK(en->byId(tr->headings[t].id) >= 0);
        }
    }
}

TEST_CASE("guide: a section missing or stale in a translation is shown in English") {
    const auto* en = guide::doc("en");
    REQUIRE(en);
    const int i = en->byId(guide::topics::Format);
    REQUIRE(i >= 0);
    const auto english = guide::shown("en", static_cast<std::size_t>(i));
    CHECK(english.doc == en);
    CHECK(english.heading->id == "format");
    CHECK(english.fallback == guide::Fallback::None);
    // A language without a guide: every section in English, said missing.
    const auto none = guide::shown("xx", static_cast<std::size_t>(i));
    CHECK(none.doc == en);
    CHECK(none.fallback == guide::Fallback::Missing);

    // Its page: the ## section and what is under it.
    const auto [first, last] = guide::page(static_cast<std::size_t>(i));
    CHECK(en->headings[first].id == "new-tournament");
    CHECK(static_cast<int>(first) < i);
    CHECK(static_cast<int>(last) > i);
    CHECK(en->headings[last].level <= 2);

    CHECK(guide::url("en", "format") ==
          std::string("https://github.com/jromang/fastchess-gui/blob/v") + FCG_VERSION + "/docs/guide.md#format");
}

TEST_CASE("markdown: blocks and runs of a guide's text") {
    const auto blocks = md::parse(
        "A **bold** and *italic* `code` [link](#the-window) and <kbd>Ctrl</kbd>+<kbd>K</kbd>.\n\n"
        "- one\n- two\n  - nested\n\n"
        "1. first\n\n   more of it\n\n"
        "| Key | What |\n|---|:-:|\n| <kbd>F1</kbd> | The *guide* |\n\n"
        "```\nfastchess-gui --run FOLDER\n```\n\n"
        "![A picture](screenshots/appearance.png)\n\n"
        "<!-- en: the-window 0123abcd -->\n\nAT&amp;T &#233;\n");
    REQUIRE(blocks.size() == 10);

    const auto& p = blocks[0];
    CHECK(p.kind == md::BlockKind::Paragraph);
    CHECK(md::plain(p.runs) == "A bold and italic code link and Ctrl+K.");
    const auto style_of = [&](std::string_view text) {
        for (const auto& r : p.runs)
            if (r.text == text) return static_cast<int>(r.style);
        return -1;
    };
    CHECK(style_of("bold") == md::Bold);
    CHECK(style_of("italic") == md::Italic);
    CHECK(style_of("code") == md::Code);
    CHECK(style_of("link") == md::Link);
    CHECK(style_of("Ctrl") == md::Key);
    for (const auto& r : p.runs)
        if (r.style & md::Link) CHECK(r.link == "#the-window");

    CHECK(blocks[1].kind == md::BlockKind::ListItem);
    CHECK(blocks[1].marker == "\xE2\x80\xA2");
    CHECK(blocks[1].level == 1);
    CHECK(md::plain(blocks[2].runs) == "two");
    CHECK(blocks[3].level == 2);
    CHECK(md::plain(blocks[3].runs) == "nested");
    CHECK(blocks[4].marker == "1.");
    CHECK(blocks[5].kind == md::BlockKind::ListItem);  // the item's second paragraph
    CHECK(blocks[5].marker.empty());
    CHECK(md::plain(blocks[5].runs) == "more of it");

    const auto& table = blocks[6];
    CHECK(table.kind == md::BlockKind::Table);
    REQUIRE(table.rows.size() == 2);
    CHECK(table.align == std::vector<int>{0, 1});
    CHECK(md::plain(table.rows[1][1]) == "The guide");
    CHECK(table.rows[1][0][0].style == md::Key);

    CHECK(blocks[7].kind == md::BlockKind::Code);
    CHECK(blocks[7].text == "fastchess-gui --run FOLDER");
    CHECK(blocks[8].kind == md::BlockKind::Image);
    CHECK(blocks[8].src == "screenshots/appearance.png");
    CHECK(blocks[8].text == "A picture");
    // The stamp comment left out, entities read.
    CHECK(md::plain(blocks[9].runs) == "AT&T \xC3\xA9");
}

TEST_CASE("markdown: lines wrapped between words, Chinese between characters") {
    const std::vector<md::Run> runs{{"one two three", md::Plain, {}}, {"four", md::Bold, {}}, {", five", md::Plain, {}}};
    // "one two three" is 13 bytes and two spaces of a half: 12 units.
    auto lines = md::wrap(runs, 8.0f, units);
    CHECK(lineTexts(runs, lines) == std::vector<std::string>{"one two", "threefour,", "five"});
    // No break between "four" and the comma that follows it, though they are two runs.
    lines = md::wrap(runs, 100.0f, units);
    CHECK(lines.size() == 1);

    // A word longer than the line is cut between characters.
    const std::vector<md::Run> long_word{{"abcdefghij", md::Plain, {}}};
    CHECK(lineTexts(long_word, md::wrap(long_word, 4.0f, units)) ==
          std::vector<std::string>{"abcd", "efgh", "ij"});

    // Chinese: between any two characters, but not before a closing mark. Each character is 3 bytes, 1 unit.
    const std::vector<md::Run> zh{{"新建锦标赛，然后开始。", md::Plain, {}}};
    CHECK(lineTexts(zh, md::wrap(zh, 3.0f, units)) ==
          std::vector<std::string>{"新建锦", "标赛，", "然后开", "始。"});

    // A hard break starts a new line.
    const std::vector<md::Run> hard{{"one\ntwo", md::Plain, {}}};
    CHECK(lineTexts(hard, md::wrap(hard, 100.0f, units)) == std::vector<std::string>{"one", "two"});
}

TEST_CASE("markdown: the search folds letter case and accents") {
    CHECK(md::queryWords("  Événement  ÜBER straße ") ==
          std::vector<std::u32string>{U"evenement", U"uber", U"strasse"});
    CHECK(md::queryWords("Ёлка") == std::vector<std::u32string>{U"елка"});
    // Ranges of the original text, in bytes: "É" is two bytes.
    const auto m = md::matches("L'Événement, un événement", md::queryWords("evenement"));
    REQUIRE(m.size() == 2);
    CHECK(m[0] == std::pair<std::size_t, std::size_t>{2, 13});
    CHECK(m[1] == std::pair<std::size_t, std::size_t>{18, 29});
    // Overlapping matches of two words merged.
    CHECK(md::matches("abcdef", md::queryWords("abc cde")) ==
          std::vector<std::pair<std::size_t, std::size_t>>{{0, 5}});
    CHECK(md::matches("锦标赛设置", md::queryWords("标赛")) ==
          std::vector<std::pair<std::size_t, std::size_t>>{{3, 9}});
}
