#include "i18n.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <SDL3/SDL_locale.h>
#include <tinygettext/dictionary.hpp>
#include <tinygettext/log.hpp>
#include <tinygettext/po_parser.hpp>

// The .po files of gui/i18n, embedded by cmake/embed_po.cmake.
namespace fcg::assets {
struct PoFile {
    const char* code;
    const unsigned char* data;
    std::size_t size;
};
extern const PoFile po_files[];
extern const std::size_t po_files_count;
}  // namespace fcg::assets

namespace fcg::i18n {

namespace {

constexpr int LANGUAGES = 8;

const std::vector<LanguageInfo> g_languages = {
    {Language::German, "de", "de", "Deutsch"},   {Language::English, "en", "", "English"},
    {Language::Spanish, "es", "es", "Español"},  {Language::French, "fr", "fr", "Français"},
    {Language::Italian, "it", "it", "Italiano"}, {Language::Portuguese, "pt", "pt_BR", "Português (Brasil)"},
    {Language::Russian, "ru", "ru", "Русский"},  {Language::Chinese, "zh", "zh_CN", "中文（简体）"},
};

std::atomic<Language> g_language{Language::English};

struct Hash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

// The dictionaries, read from their .po file the first time a language is used, and the strings handed out by
// tr(), so that the returned pointers stay valid. Both under g_mutex.
std::mutex g_mutex;
std::unique_ptr<tinygettext::Dictionary> g_dictionaries[LANGUAGES];
std::unordered_map<std::string, std::string, Hash, std::equal_to<>> g_cache[LANGUAGES];

tinygettext::Dictionary& dictionary(Language language) {
    auto& dict = g_dictionaries[static_cast<int>(language)];
    if (dict) return *dict;

    // A message without translation falls back to English: nothing to say about it.
    tinygettext::Log::set_log_info_callback(nullptr);
    tinygettext::Log::set_log_warning_callback(nullptr);
    dict                 = std::make_unique<tinygettext::Dictionary>();
    const std::string po = info(language).po;
    for (std::size_t i = 0; i < assets::po_files_count; ++i) {
        if (po.empty() || po != assets::po_files[i].code) continue;
        std::istringstream in(
            std::string(reinterpret_cast<const char*>(assets::po_files[i].data), assets::po_files[i].size));
        try {
            tinygettext::POParser::parse(po + ".po", in, *dict);
        } catch (...) {
            // What was read before stays; the rest is shown in English.
        }
    }
    return *dict;
}

}  // namespace

const std::vector<LanguageInfo>& languages() { return g_languages; }

const LanguageInfo& info(Language language) {
    for (const auto& l : g_languages) {
        if (l.language == language) return l;
    }
    return g_languages[1];  // English
}

Language systemLanguage() {
    int count     = 0;
    auto* locales = SDL_GetPreferredLocales(&count);
    Language lang = Language::English;
    if (locales && count > 0 && locales[0] && locales[0]->language) {
        const std::string_view code(locales[0]->language);
        for (const auto& l : g_languages) {
            if (code == l.code) lang = l.language;
        }
    }
    SDL_free(locales);
    return lang;
}

Language resolve(const std::string& setting) {
    for (const auto& l : g_languages) {
        if (setting == l.code) return l.language;
    }
    return systemLanguage();
}

void setLanguage(Language language) { g_language = language; }

Language language() { return g_language; }

const char* tr(const std::string& english) {
    const Language language = g_language;

    std::lock_guard<std::mutex> lock(g_mutex);
    auto& cache = g_cache[static_cast<int>(language)];
    if (auto it = cache.find(english); it != cache.end()) return it->second.c_str();

    std::string out = english;
    if (language != Language::English) {
        const auto split = english.find("##");
        const auto text  = english.substr(0, split);
        out = dictionary(language).translate(text) + (split == std::string::npos ? "" : english.substr(split));
    }

    return cache.emplace(english, std::move(out)).first->second.c_str();
}

const char* tr(const char* english) {
    if (g_language == Language::English) return english;  // literals are already English
    return tr(std::string(english));
}

std::string trPlural(const char* one, const char* other, uint64_t n) {
    const Language language = g_language;
    if (language == Language::English) return n == 1 ? one : other;
    // The rules look at the last digits only: a huge n keeps them.
    const auto count = static_cast<int>(n > 1'000'000 ? n % 1'000'000 + 1'000'000 : n);
    std::lock_guard<std::mutex> lock(g_mutex);
    return dictionary(language).translate_plural(one, other, count);
}

std::vector<Message> messages(Language language) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<Message> out;
    dictionary(language).foreach ([&](const std::string& id, const std::vector<std::string>& forms) {
        if (!id.empty()) out.push_back({id, forms});
    });
    return out;
}

int pluralForms(Language language) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(dictionary(language).get_plural_forms().get_nplural());
}

}  // namespace fcg::i18n
