#include "palette.hpp"

#include <algorithm>
#include <utility>

namespace fcg::palette {

namespace {

// The scores of matches where what is typed is found as it is (or word by word), rather than its letters apart.
constexpr int CONTIGUOUS = 10000;

// ASCII letters only: the bytes of other letters (UTF-8) are compared as they are.
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool alnum(char c) {
    const auto u = static_cast<unsigned char>(c);
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || u >= 0x80;
}

// At the start of the text or after a space, a dash, a parenthesis...
bool wordStart(std::string_view text, std::size_t i) { return i == 0 || !alnum(text[i - 1]); }

}  // namespace

int matchScore(std::string_view query, std::string_view text) {
    if (query.empty()) return 0;
    if (query.size() > text.size()) return -1;

    std::string q(query), t(text);
    std::transform(q.begin(), q.end(), q.begin(), lower);
    std::transform(t.begin(), t.end(), t.begin(), lower);

    // Shorter texts first among those that match as well: "Engines" before "Engine options".
    const int shorter = 1000 - static_cast<int>(std::min<std::size_t>(t.size(), 999));
    if (t.compare(0, q.size(), q) == 0) return 30000 + shorter;
    bool inside = false;
    for (auto at = t.find(q); at != std::string::npos; at = t.find(q, at + 1)) {
        if (wordStart(t, at)) return 20000 + shorter;
        inside = true;
    }
    if (inside) return 10000 + shorter;

    // Several words typed, each in the text, whatever their order: "stock 17", "copy standings".
    std::vector<std::string_view> words;
    for (std::size_t from = 0; from < q.size();) {
        const auto end = std::min(q.find(' ', from), q.size());
        if (end > from) words.push_back(std::string_view(q).substr(from, end - from));
        from = end + 1;
    }
    if (words.size() >= 2) {
        bool all = true, at_starts = true;
        for (const auto word : words) {
            bool found = false, start = false;
            for (auto at = t.find(word); at != std::string::npos && !start; at = t.find(word, at + 1)) {
                found = true;
                start = wordStart(t, at);
            }
            all       = all && found;
            at_starts = at_starts && start;
        }
        if (all) return (at_starts ? 15000 : 11000) + shorter;
    }

    // The letters apart: each one at its first place after the previous one.
    int starts = 0, together = 0, gaps = 0;
    std::size_t at = 0, previous = std::string::npos;
    for (const char c : q) {
        while (at < t.size() && t[at] != c) ++at;
        if (at == t.size()) return -1;
        if (wordStart(t, at)) ++starts;
        if (previous != std::string::npos && at == previous + 1) ++together;
        if (previous != std::string::npos) gaps += static_cast<int>(at - previous - 1);
        previous = at++;
    }
    return std::clamp(5000 + 100 * starts + 50 * together - gaps - static_cast<int>(t.size()) / 10, 0, 9999);
}

std::vector<int> rank(std::string_view query, const std::vector<std::string>& texts,
                      const std::vector<std::string>* extra) {
    // (kind of match, score) for each index matched: 2 what is typed in the text itself, 1 in the text and its extra,
    // 0 its letters apart in the text. Letters apart over the text and its extra are too loose to count.
    std::vector<std::pair<std::pair<int, int>, int>> found;
    for (std::size_t i = 0; i < texts.size(); ++i) {
        const int score = matchScore(query, texts[i]);
        if (score >= CONTIGUOUS) {
            found.push_back({{2, score}, static_cast<int>(i)});
            continue;
        }
        if (extra && i < extra->size() && !(*extra)[i].empty()) {
            if (const int more = matchScore(query, texts[i] + " " + (*extra)[i]); more >= CONTIGUOUS) {
                found.push_back({{1, more}, static_cast<int>(i)});
                continue;
            }
        }
        if (score >= 0) found.push_back({{0, score}, static_cast<int>(i)});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<int> out;
    out.reserve(found.size());
    for (const auto& f : found) out.push_back(f.second);
    return out;
}

}  // namespace fcg::palette
