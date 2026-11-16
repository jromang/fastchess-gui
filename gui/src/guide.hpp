#pragma once

// The user guide (docs/guide.md) and its translations (docs/guide.<code>.md), embedded in the program: their sections,
// the anchors GitHub gives their headings, which translated section is up to date, and the topics the program opens.
// docs/design/help.md describes the rules; tools/guide_sync.py checks them on the files with the same slug and hash.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fcg::guide {

// The ids of the English sections the program opens (F1, the `?` beside a title, the messages). A test checks each
// is a section of docs/guide.md.
namespace topics {
inline constexpr const char* Intro              = "fastchess-gui-user-guide";
inline constexpr const char* QuickStart         = "quick-start";
inline constexpr const char* Window             = "the-window";
inline constexpr const char* Help               = "help-in-the-program";
inline constexpr const char* Palette            = "the-command-palette";
inline constexpr const char* Language           = "language-theme-and-size";
inline constexpr const char* BoardButtons       = "boards-and-their-buttons";
inline constexpr const char* Version            = "version-and-new-versions";
inline constexpr const char* Report             = "reporting-a-problem";
inline constexpr const char* Drawing            = "drawing-the-window";
inline constexpr const char* Appearance         = "appearance";
inline constexpr const char* Board3d            = "the-3d-board";
inline constexpr const char* Shortcuts          = "keyboard-shortcuts";
inline constexpr const char* Home               = "the-home-page";
inline constexpr const char* Engines            = "engines";
inline constexpr const char* EngineChecks       = "adding-and-checking-engines";
inline constexpr const char* Library            = "a-large-library";
inline constexpr const char* StartingEngines    = "starting-engines";
inline constexpr const char* Rebuilt            = "rebuilt-programs";
inline constexpr const char* Import             = "importing-engines-and-ratings";
inline constexpr const char* WinBoard           = "winboard-engines";
inline constexpr const char* Books              = "books";
inline constexpr const char* BookChecks         = "checking-a-book";
inline constexpr const char* CreatingBooks      = "creating-books";
inline constexpr const char* Board              = "board";
inline constexpr const char* BoardOpen          = "opening-pasting-and-setting-up-a-position";
inline constexpr const char* Analysis           = "the-analysis";
inline constexpr const char* GameAnalysis       = "analysing-a-whole-game";
inline constexpr const char* BoardPlay          = "playing-a-game-from-a-position";
inline constexpr const char* Suites             = "test-suites";
inline constexpr const char* NewTournament      = "new-tournament";
inline constexpr const char* Summary            = "the-summary-and-its-checks";
inline constexpr const char* Profiles           = "profiles-and-templates";
inline constexpr const char* Format             = "format";
inline constexpr const char* Participants       = "participants-and-the-library";
inline constexpr const char* OwnBooks           = "participants-own-books";
inline constexpr const char* EngineSettings     = "engine-settings";
inline constexpr const char* TimeControl        = "time-control";
inline constexpr const char* Concurrency        = "concurrency";
inline constexpr const char* Openings           = "openings";
inline constexpr const char* Chess960           = "chess960";
inline constexpr const char* Adjudication       = "adjudication";
inline constexpr const char* PgnMisc            = "pgn-and-misc";
inline constexpr const char* Tips               = "tips-for-the-settings";
inline constexpr const char* Running            = "running-a-tournament";
inline constexpr const char* Overview           = "overview";
inline constexpr const char* Standings          = "standings";
inline constexpr const char* Teams              = "teams";
inline constexpr const char* Schedule           = "bracket-and-schedule";
inline constexpr const char* Live               = "live";
inline constexpr const char* BigScreen          = "big-screen";
inline constexpr const char* Games              = "games";
inline constexpr const char* OpeningsTab        = "the-openings-tab";
inline constexpr const char* Panels             = "panels-and-layouts";
inline constexpr const char* Processor          = "processor-use";
inline constexpr const char* PauseStop          = "pause-stop-and-resume";
inline constexpr const char* Several            = "several-tournaments-at-once";
inline constexpr const char* CannotStart        = "an-engine-that-cannot-start";
inline constexpr const char* Changing           = "changing-a-running-tournament";
inline constexpr const char* Queue              = "queue-and-relaunch";
inline constexpr const char* Ratings            = "ratings";
inline constexpr const char* RatingList         = "rating-list";
inline constexpr const char* EngineCard         = "engine-card";
inline constexpr const char* HeadToHead         = "head-to-head";
inline constexpr const char* Progress           = "progress";
inline constexpr const char* Broadcast          = "broadcast";
inline constexpr const char* LiveFiles          = "live-files";
inline constexpr const char* Tlcs               = "tlcs-server";
inline constexpr const char* WebPage            = "web-page";
inline constexpr const char* Lichess            = "lichess";
inline constexpr const char* Tunnel             = "putting-the-web-page-on-the-internet";
inline constexpr const char* Chat               = "where-the-viewers-talk";
inline constexpr const char* Headless           = "headless-mode";
inline constexpr const char* Machines           = "machines";
inline constexpr const char* RemoteEngines      = "engines-played-from-this-computer";
inline constexpr const char* Files              = "files";
inline constexpr const char* Portable           = "portable-mode";
inline constexpr const char* CommandLine        = "command-line";
inline constexpr const char* Troubleshooting    = "troubleshooting";
inline constexpr const char* TroubleStart       = "the-program-does-not-start";
inline constexpr const char* TroubleEngine      = "an-engine-does-not-start";
inline constexpr const char* TroubleGames       = "games-are-lost-or-look-wrong";
inline constexpr const char* TroubleBroadcast   = "broadcasting";
inline constexpr const char* Glossary           = "glossary";
}  // namespace topics

// Every topic above, for the test.
[[nodiscard]] const std::vector<const char*>& allTopics();

// The anchor GitHub gives a heading: lower case (ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic letters),
// spaces become dashes, punctuation and symbols go away (ASCII ones but - and _, and the usual ones outside ASCII:
// « » ¿ — ’ “ ” …, the CJK and full-width ones); other letters, digits and characters stay.
[[nodiscard]] std::string slug(std::string_view heading);

// FNV-1a, 32 bits: the hash of an English section a translation was made from.
[[nodiscard]] uint32_t fnv1a(std::string_view text);
[[nodiscard]] std::string hex8(uint32_t value);

// A heading of a guide and the section it starts: its text up to the next heading, outside fenced code blocks.
struct Heading {
    int level = 0;
    std::string title;
    std::string anchor;  // in its own file, "-1", "-2"... added to a heading met again, as on GitHub
    std::string id;      // the English section's: the anchor in English, the stamp's in a translation ("" without)
    std::string stamp;   // a translation's: the hash of the English text it was made from
    std::size_t line  = 0;  // where its heading line starts in the text
    std::size_t body  = 0;  // where its text starts (after the heading line and the stamp)
    std::size_t end   = 0;  // where the next heading line starts, or the end of the text
};

// The headings of a guide, in order. `translation`: a `<!-- en: <id> <hash> -->` line right after a heading gives
// its id and stamp (English headings are their own id).
[[nodiscard]] std::vector<Heading> headings(std::string_view markdown, bool translation);

// The anchors of every heading of a Markdown text, in order.
[[nodiscard]] std::vector<std::string> anchors(std::string_view markdown);

// What an English section's hash is made from: its heading line and its text, each line without trailing spaces,
// without the blank lines at both ends, joined by "\n".
[[nodiscard]] std::string sectionText(std::string_view markdown, const Heading& heading);

// The inline code spans of a section's text, sorted, and its fenced code blocks, in order: what a translation keeps.
[[nodiscard]] std::vector<std::string> codeSpans(std::string_view text);
[[nodiscard]] std::vector<std::string> codeBlocks(std::string_view text);
// The targets of its links to a section: "[text](#target)".
[[nodiscard]] std::vector<std::string> sectionLinks(std::string_view text);

// An embedded guide.
struct Doc {
    std::string code;  // "en", "fr", "pt_BR"…
    std::string text;
    std::vector<Heading> headings;
    // The heading of an English id, or of an anchor of this file; -1 when none.
    [[nodiscard]] int byId(std::string_view id) const;
    [[nodiscard]] int byAnchor(std::string_view anchor) const;
};

// The guide of a language ("en" or "" for English), read and parsed at its first use; nullptr when the program has
// none in that language.
[[nodiscard]] const Doc* doc(std::string_view code);
// The codes of the embedded translations.
[[nodiscard]] std::vector<std::string> translations();
// The bytes of an embedded picture ("screenshots/appearance.png"), nullopt when there is none of that name.
[[nodiscard]] std::optional<std::string_view> picture(std::string_view name);

// Why an English section is shown in English in another language.
enum class Fallback { None, Missing, Stale };

// What the guide of a language shows of an English section: its translation when it has one made from the current
// English text, else the English section.
struct Shown {
    const Doc* doc         = nullptr;
    const Heading* heading = nullptr;
    Fallback fallback      = Fallback::None;
};
[[nodiscard]] Shown shown(std::string_view code, std::size_t english_index);

// The English sections of the page an English section is on: the `#` or `##` heading at or before it and the
// headings under it, as [first, last).
[[nodiscard]] std::pair<std::size_t, std::size_t> page(std::size_t english_index);

// The guide on GitHub, for the version that runs, in a language, at the section of an English id:
// https://github.com/jromang/fastchess-gui/blob/v0.6.0/docs/guide.fr.md#nouveau-tournoi
[[nodiscard]] std::string url(std::string_view code, std::string_view id);

// The interface language's code for the guide: "fr", "pt_BR"…, "en".
[[nodiscard]] std::string currentCode();

}  // namespace fcg::guide
