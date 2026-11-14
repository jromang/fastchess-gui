#include "update_check.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <vector>

#include "https.hpp"

namespace fcg::update {

namespace {

// "v0.6.0-rc1" -> numbers {0, 6, 0}, suffix "-rc1".
struct Parsed {
    std::vector<int64_t> numbers;
    std::string_view suffix;
};

Parsed parse(std::string_view v) {
    Parsed p;
    if (!v.empty() && (v.front() == 'v' || v.front() == 'V')) v.remove_prefix(1);
    while (!v.empty() && std::isdigit(static_cast<unsigned char>(v.front()))) {
        int64_t n = 0;
        while (!v.empty() && std::isdigit(static_cast<unsigned char>(v.front()))) {
            n = std::min<int64_t>(n * 10 + (v.front() - '0'), 1'000'000'000);
            v.remove_prefix(1);
        }
        p.numbers.push_back(n);
        if (v.size() < 2 || v.front() != '.' || !std::isdigit(static_cast<unsigned char>(v[1]))) break;
        v.remove_prefix(1);
    }
    p.suffix = v;
    return p;
}

}  // namespace

int compareVersions(std::string_view a, std::string_view b) {
    const auto pa = parse(a), pb = parse(b);
    for (std::size_t i = 0; i < std::max(pa.numbers.size(), pb.numbers.size()); ++i) {
        const int64_t x = i < pa.numbers.size() ? pa.numbers[i] : 0;
        const int64_t y = i < pb.numbers.size() ? pb.numbers[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    if (pa.suffix.empty() != pb.suffix.empty()) return pa.suffix.empty() ? 1 : -1;
    return pa.suffix.compare(pb.suffix) < 0 ? -1 : pa.suffix == pb.suffix ? 0 : 1;
}

std::optional<Release> parseRelease(std::string_view json) {
    try {
        const auto j = nlohmann::json::parse(json);
        if (!j.is_object() || j.value("draft", false) || j.value("prerelease", false)) return std::nullopt;
        const auto tag = j.value("tag_name", std::string{});
        if (parse(tag).numbers.empty()) return std::nullopt;
        Release r;
        r.version = tag.front() == 'v' || tag.front() == 'V' ? tag.substr(1) : tag;
        if (j.contains("body") && j["body"].is_string()) r.notes = j["body"].get<std::string>();
        // Shown as it is: a page of notes is plenty.
        if (r.notes.size() > 20000) {
            std::size_t cut = 20000;
            while (cut > 0 && (static_cast<unsigned char>(r.notes[cut]) & 0xC0) == 0x80) --cut;  // not inside a letter
            r.notes = r.notes.substr(0, cut) + "\n…";
        }
        // Only a page of the program's own on GitHub is opened.
        const auto url                = j.value("html_url", std::string{});
        const std::string_view prefix = "https://github.com/jromang/fastchess-gui/";
        r.url =
            url.rfind(prefix, 0) == 0 && url.find_first_of(" \t\r\n\"<>") == std::string::npos ? url : RELEASES_PAGE;
        return r;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

bool checkDue(int64_t last, int64_t now) { return last <= 0 || now < last || now - last >= 24 * 3600; }

const Release* notice(const Settings& settings, std::string_view current) {
    if (!settings.enabled || !settings.latest || settings.latest->version.empty()) return nullptr;
    if (compareVersions(settings.latest->version, current) <= 0) return nullptr;
    if (!settings.dismissed.empty() && compareVersions(settings.latest->version, settings.dismissed) <= 0) {
        return nullptr;
    }
    return &*settings.latest;
}

Checker::~Checker() {
    if (future_.valid()) future_.wait();
}

void Checker::start() {
    if (future_.valid()) return;
    future_ = std::async(std::launch::async, [] {
        Answer answer;
        HttpRequest request;
        request.method    = "GET";
        request.url       = LATEST_API;
        request.timeout_s = 15;
        request.headers   = {{"Accept", "application/vnd.github+json"}, {"X-GitHub-Api-Version", "2022-11-28"}};
        const auto result = httpRequest(request);
        if (!result.error.empty()) {
            answer.error = result.error;
        } else if (result.status != 200) {
            answer.error = "GitHub answered " + std::to_string(result.status);
        } else if (!(answer.release = parseRelease(result.body))) {
            answer.error = "GitHub's answer has no release";
        }
        return answer;
    });
}

std::optional<Checker::Answer> Checker::poll() {
    if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
    return future_.get();
}

}  // namespace fcg::update
