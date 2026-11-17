#include "guide.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>

#include <fmt/include/fmt/format.h>

#include "i18n.hpp"
#include "zip.hpp"

namespace fcg::assets {
struct DocFile {
    const char* name;
    const unsigned char* data;
    std::size_t size;
    bool gzipped;
};
extern const DocFile doc_files[];
extern const std::size_t doc_files_count;
}  // namespace fcg::assets

namespace fcg::guide {

const std::vector<const char*>& allTopics() {
    using namespace topics;
    static const std::vector<const char*> all{
        Intro,        QuickStart,   Window,      Help,          Palette,       Language,     BoardButtons,
        Version,      Report,       Drawing,     Appearance,    Shortcuts,     Home,         Engines,
        EngineChecks, Library,      StartingEngines, Rebuilt,   Import,        WinBoard,     Books,
        BookChecks,   CreatingBooks, Board,      BoardOpen,     Analysis,      GameAnalysis, BoardPlay,
        Suites,       NewTournament, Summary,    Profiles,      Format,        Participants, OwnBooks,
        EngineSettings, TimeControl, Concurrency, Openings,     Chess960,      Adjudication, PgnMisc,
        Tips,         Running,      Overview,    Standings,     Teams,         Schedule,     Live,
        BigScreen,    Games,        OpeningsTab, Panels,        Processor,     PauseStop,    Several,
        CannotStart,  Changing,     Queue,       Ratings,       RatingList,    EngineCard,   HeadToHead,
        Progress,     Broadcast,    LiveFiles,   Tlcs,          WebPage,       Lichess,      Tunnel,
        Chat,         Headless,     Machines,    Files,         Portable,      CommandLine,  Troubleshooting,
        TroubleStart, TroubleEngine, TroubleGames, TroubleBroadcast, Glossary};
    return all;
}

namespace {

// One code point of UTF-8 text at `i`, which moves past it; U+FFFD for a malformed byte.
char32_t next(std::string_view s, std::size_t& i) {
    const auto b = static_cast<unsigned char>(s[i]);
    int len      = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 0;
    if (len == 0 || i + len > s.size()) {
        ++i;
        return 0xFFFD;
    }
    char32_t cp = len == 1 ? b : b & (0x7F >> len);
    for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += len;
    return cp;
}

void append(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

char32_t lower(char32_t cp) {
    if (cp >= 0x41 && cp <= 0x5A) return cp + 0x20;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 0x20;
    if ((cp >= 0x100 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177)) return cp % 2 == 0 ? cp + 1 : cp;
    if ((cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E)) return cp % 2 == 1 ? cp + 1 : cp;
    if (cp == 0x178) return 0xFF;
    if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) return cp + 0x20;
    if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;
    if (cp >= 0x400 && cp <= 0x40F) return cp + 0x50;
    return cp;
}

bool dropped(char32_t cp) {
    return (cp >= 0xA0 && cp <= 0xBF) || cp == 0xD7 || cp == 0xF7 || (cp >= 0x2000 && cp <= 0x206F) ||
           (cp >= 0x20A0 && cp <= 0x20CF) || (cp >= 0x2190 && cp <= 0x23FF) || (cp >= 0x2500 && cp <= 0x27BF) ||
           (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20) ||
           (cp >= 0xFF3B && cp <= 0xFF40) || (cp >= 0xFF5B && cp <= 0xFF65);
}

bool fence(std::string_view line) { return line.substr(0, 3) == "```" || line.substr(0, 3) == "~~~"; }

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// "<!-- en: <id> <hash> -->": the id and the hash, nullopt when the line is not one.
std::optional<std::pair<std::string, std::string>> stampOf(std::string_view line) {
    line = trimmed(line);
    while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
    constexpr std::string_view open = "<!-- en: ", close = " -->";
    if (line.size() < open.size() + close.size() || line.substr(0, open.size()) != open ||
        line.substr(line.size() - close.size()) != close)
        return std::nullopt;
    const auto inner = line.substr(open.size(), line.size() - open.size() - close.size());
    const auto space = inner.rfind(' ');
    if (space == std::string_view::npos || space == 0) return std::nullopt;
    const auto hash = inner.substr(space + 1);
    if (hash.size() != 8 || hash.find_first_not_of("0123456789abcdef") != std::string_view::npos) return std::nullopt;
    return std::pair{std::string(inner.substr(0, space)), std::string(hash)};
}

}  // namespace

std::string slug(std::string_view heading) {
    std::string out;
    for (std::size_t i = 0; i < heading.size();) {
        const char32_t cp = next(heading, i);
        if (cp < 0x80) {
            const char c = static_cast<char>(cp);
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
                out += c;
            else if (c >= 'A' && c <= 'Z')
                out += static_cast<char>(c - 'A' + 'a');
            else if (c == ' ')
                out += '-';
        } else if (!dropped(cp)) {
            append(out, lower(cp));
        }
    }
    return out;
}

uint32_t fnv1a(std::string_view text) {
    uint32_t h = 0x811C9DC5u;
    for (const char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x01000193u;
    }
    return h;
}

std::string hex8(uint32_t value) { return fmt::format("{:08x}", value); }

std::vector<Heading> headings(std::string_view markdown, bool translation) {
    std::vector<Heading> out;
    std::map<std::string, int> seen;
    bool fenced          = false;
    bool expect_stamp    = false;
    std::size_t at       = 0;
    while (at < markdown.size()) {
        const auto end      = markdown.find('\n', at);
        const auto line_end = end == std::string_view::npos ? markdown.size() : end;
        const auto next_at  = end == std::string_view::npos ? markdown.size() : end + 1;
        auto line           = trimmed(markdown.substr(at, line_end - at));

        if (expect_stamp) {
            expect_stamp = false;
            if (const auto stamp = stampOf(line)) {
                out.back().id    = stamp->first;
                out.back().stamp = stamp->second;
                out.back().body  = next_at;
                at               = next_at;
                continue;
            }
        }
        if (fence(line)) {
            fenced = !fenced;
        } else if (!fenced && !line.empty() && line[0] == '#') {
            const auto hashes = line.find_first_not_of('#');
            if (hashes != std::string_view::npos && hashes <= 6 && line[hashes] == ' ') {
                auto text = line.substr(hashes + 1);
                // "## Title ##": the closing hashes, after a space, are not part of it.
                if (const auto last = text.find_last_not_of('#'); last != std::string_view::npos && text[last] == ' ')
                    text = text.substr(0, last);
                text = trimmed(text);
                while (!text.empty() && text.front() == ' ') text.remove_prefix(1);

                if (!out.empty()) out.back().end = at;
                Heading h;
                h.level  = static_cast<int>(hashes);
                h.title  = std::string(text);
                h.anchor = slug(text);
                if (const int n = seen[h.anchor]++; n > 0) h.anchor.append("-").append(std::to_string(n));
                h.id   = translation ? "" : h.anchor;
                h.line = at;
                h.body = next_at;
                h.end  = markdown.size();
                out.push_back(std::move(h));
                expect_stamp = translation;
            }
        }
        at = next_at;
    }
    return out;
}

std::vector<std::string> anchors(std::string_view markdown) {
    std::vector<std::string> out;
    for (auto& h : headings(markdown, false)) out.push_back(std::move(h.anchor));
    return out;
}

std::string sectionText(std::string_view markdown, const Heading& heading) {
    std::vector<std::string_view> lines;
    const auto heading_line = trimmed(markdown.substr(heading.line, markdown.find('\n', heading.line) - heading.line));
    std::size_t at          = heading.body;
    while (at < heading.end) {
        auto end = markdown.find('\n', at);
        if (end == std::string_view::npos || end > heading.end) end = heading.end;
        const auto line = trimmed(markdown.substr(at, end - at));
        if (!stampOf(line)) lines.push_back(line);
        at = end + 1;
    }
    while (!lines.empty() && lines.front().empty()) lines.erase(lines.begin());
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    std::string out(heading_line);
    for (const auto line : lines) {
        out += '\n';
        out += line;
    }
    return out;
}

std::vector<std::string> codeSpans(std::string_view text) {
    std::vector<std::string> out;
    bool fenced    = false;
    std::size_t at = 0;
    while (at < text.size()) {
        auto end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        const auto line = text.substr(at, end - at);
        at              = end + 1;
        if (fence(line)) {
            fenced = !fenced;
            continue;
        }
        if (fenced) continue;
        for (std::size_t i = 0; i < line.size();) {
            if (line[i] != '`') {
                ++i;
                continue;
            }
            std::size_t ticks = 0;
            while (i + ticks < line.size() && line[i + ticks] == '`') ++ticks;
            const auto marker = line.substr(i, ticks);
            const auto close  = line.find(marker, i + ticks);
            if (close == std::string_view::npos) break;
            auto span = line.substr(i + ticks, close - i - ticks);
            while (!span.empty() && span.front() == ' ') span.remove_prefix(1);
            while (!span.empty() && span.back() == ' ') span.remove_suffix(1);
            if (!span.empty()) out.emplace_back(span);
            i = close + ticks;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> codeBlocks(std::string_view text) {
    std::vector<std::string> out;
    std::string block;
    bool fenced    = false;
    std::size_t at = 0;
    while (at < text.size()) {
        auto end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        const auto line = text.substr(at, end - at);
        at              = end + 1;
        if (fence(line)) {
            if (fenced) {
                if (!block.empty()) block.pop_back();
                out.push_back(std::move(block));
                block.clear();
            }
            fenced = !fenced;
        } else if (fenced) {
            block += trimmed(line);
            block += '\n';
        }
    }
    return out;
}

std::vector<std::string> sectionLinks(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t at = text.find("](#"); at != std::string_view::npos; at = text.find("](#", at + 3)) {
        const auto end = text.find(')', at + 3);
        if (end == std::string_view::npos) break;
        const auto target = text.substr(at + 3, end - at - 3);
        if (!target.empty() && target.find_first_of(" \t\n") == std::string_view::npos) out.emplace_back(target);
    }
    return out;
}

int Doc::byId(std::string_view id) const {
    for (std::size_t i = 0; i < headings.size(); ++i)
        if (headings[i].id == id) return static_cast<int>(i);
    return -1;
}

int Doc::byAnchor(std::string_view anchor) const {
    for (std::size_t i = 0; i < headings.size(); ++i)
        if (headings[i].anchor == anchor) return static_cast<int>(i);
    return -1;
}

namespace {

const assets::DocFile* file(std::string_view name) {
    for (std::size_t i = 0; i < assets::doc_files_count; ++i)
        if (name == assets::doc_files[i].name) return &assets::doc_files[i];
    return nullptr;
}

std::string fileName(std::string_view code) {
    return code.empty() || code == "en" ? std::string("guide.md") : fmt::format("guide.{}.md", code);
}

}  // namespace

const Doc* doc(std::string_view code) {
    // Read once each, and kept: the panel and the palette ask for them every frame. The window's thread asks, and the
    // tests from theirs, hence the lock.
    static std::mutex mutex;
    static std::map<std::string, std::unique_ptr<Doc>, std::less<>> docs;
    const std::string key = code.empty() ? "en" : std::string(code);
    std::lock_guard lock(mutex);
    if (const auto it = docs.find(key); it != docs.end()) return it->second.get();

    std::unique_ptr<Doc> made;
    if (const auto* f = file(fileName(key))) {
        const std::string_view bytes(reinterpret_cast<const char*>(f->data), f->size);
        std::optional<std::string> text = f->gzipped ? gunzipped(bytes) : std::optional<std::string>(bytes);
        if (text) {
            made           = std::make_unique<Doc>();
            made->code     = key;
            made->text     = std::move(*text);
            made->headings = headings(made->text, key != "en");
        }
    }
    return (docs[key] = std::move(made)).get();
}

std::vector<std::string> translations() {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < assets::doc_files_count; ++i) {
        const std::string_view name = assets::doc_files[i].name;
        if (name.size() > 9 && name.substr(0, 6) == "guide." && name.substr(name.size() - 3) == ".md")
            out.emplace_back(name.substr(6, name.size() - 9));
    }
    return out;
}

std::optional<std::string_view> picture(std::string_view name) {
    const auto* f = file(name);
    if (!f || f->gzipped) return std::nullopt;
    return std::string_view(reinterpret_cast<const char*>(f->data), f->size);
}

Shown shown(std::string_view code, std::size_t english_index) {
    const Doc* en = doc("en");
    if (!en || english_index >= en->headings.size()) return {};
    const Heading& h = en->headings[english_index];
    Shown out{en, &h, Fallback::None};
    if (code.empty() || code == "en") return out;
    const Doc* tr = doc(code);
    // The title of a translation has no stamp: it is the first heading.
    if (tr && english_index == 0 && !tr->headings.empty() && tr->headings[0].level == 1)
        return {tr, &tr->headings[0], Fallback::None};
    const int i = tr ? tr->byId(h.id) : -1;
    if (i < 0) {
        out.fallback = Fallback::Missing;
        return out;
    }
    const Heading& t = tr->headings[static_cast<std::size_t>(i)];
    // The hash of the English text, computed once per section.
    static std::mutex mutex;
    static std::map<std::size_t, std::string> hashes;
    std::string hash;
    {
        std::lock_guard lock(mutex);
        auto& cached = hashes[english_index];
        if (cached.empty()) cached = hex8(fnv1a(sectionText(en->text, h)));
        hash = cached;
    }
    if (t.stamp != hash) {
        out.fallback = Fallback::Stale;
        return out;
    }
    return {tr, &t, Fallback::None};
}

std::pair<std::size_t, std::size_t> page(std::size_t english_index) {
    const Doc* en = doc("en");
    if (!en || en->headings.empty()) return {0, 0};
    const auto& hs = en->headings;
    std::size_t first = std::min(english_index, hs.size() - 1);
    while (first > 0 && hs[first].level > 2) --first;
    std::size_t last = first + 1;
    while (last < hs.size() && hs[last].level > 2) ++last;
    return {first, last};
}

std::string url(std::string_view code, std::string_view id) {
    // The translated section when it is the one shown, else the English one (a section not translated, or stale).
    std::string file = "guide.md", anchor(id);
    const Doc* en    = doc("en");
    if (const Doc* d = doc(code); d && en && d != en) {
        if (const int i = en->byId(id); i >= 0) {
            const auto s = shown(code, static_cast<std::size_t>(i));
            if (s.heading && s.doc == d) {
                file   = fileName(code);
                anchor = s.heading->anchor;
            }
        }
    }
    return fmt::format("https://github.com/jromang/fastchess-gui/blob/v{}/docs/{}#{}", FCG_VERSION, file, anchor);
}

std::string currentCode() {
    const auto& info = i18n::info(i18n::language());
    return info.po && *info.po ? std::string(info.po) : std::string("en");
}

}  // namespace fcg::guide
