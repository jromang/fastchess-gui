#pragma once

// New versions of the program: when the user asks for it (Settings, off by default), the latest release on GitHub
// is asked for at launch, at most once a day, in the background; a newer one is shown as a notice with its notes and
// the link to its page. Nothing is downloaded nor installed.

#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <string_view>

#include <json.hpp>

namespace fcg::update {

// GitHub's API for the latest release (drafts and pre-releases left out), and the page of the releases.
inline constexpr const char* LATEST_API    = "https://api.github.com/repos/jromang/fastchess-gui/releases/latest";
inline constexpr const char* RELEASES_PAGE = "https://github.com/jromang/fastchess-gui/releases";

struct Release {
    std::string version;  // as tagged, without the "v": "0.6.0"
    std::string notes;    // Markdown, shown as it is
    std::string url;      // its page on GitHub, with the files to download
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Release, version, notes, url)

// What settings.json keeps of it.
struct Settings {
    bool enabled    = false;        // asked for at launch
    int64_t checked = 0;            // when it was last asked for, seconds since 1970; 0 never
    std::optional<Release> latest;  // the latest release then
    std::string dismissed;          // the version whose notice was closed: shown again for a newer one only
};

// Less than 0, 0 or more than 0 as `a` is older than, the same as or newer than `b`: numbers separated by dots, a
// "v" in front ignored, missing numbers taken as 0 ("0.6" is "0.6.0"); a version with a suffix ("0.6.0-rc1") comes
// before the same without one.
[[nodiscard]] int compareVersions(std::string_view a, std::string_view b);

// The release in an answer of LATEST_API; nullopt when it is not one, a draft or a pre-release. A page that is not
// one of the program's on GitHub is replaced with RELEASES_PAGE.
[[nodiscard]] std::optional<Release> parseRelease(std::string_view json);

// Whether to ask again: a day has passed since `last` (seconds since 1970, 0 never), or the clock went back.
[[nodiscard]] bool checkDue(int64_t last, int64_t now);

// The release to tell the user about: the latest known, newer than `current`, and not dismissed.
[[nodiscard]] const Release* notice(const Settings& settings, std::string_view current);

// The question to GitHub, in a thread of its own.
class Checker {
   public:
    Checker()                          = default;
    Checker(const Checker&)            = delete;
    Checker& operator=(const Checker&) = delete;
    ~Checker();  // waits for a question in flight: it gives up within its timeout

    void start();
    [[nodiscard]] bool running() const { return future_.valid(); }

    struct Answer {
        std::optional<Release> release;
        std::string error;  // why there is none
    };
    // The answer, once, when it came; nullopt meanwhile, or when nothing was asked.
    [[nodiscard]] std::optional<Answer> poll();

   private:
    std::future<Answer> future_;
};

}  // namespace fcg::update
