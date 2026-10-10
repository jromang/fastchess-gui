#include "i18n.hpp"

#include <atomic>
#include <mutex>
#include <unordered_map>

#include <SDL3/SDL_locale.h>

namespace fcg::i18n {

namespace {

std::atomic<Language> g_language{Language::English};

struct Hash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

const std::unordered_map<std::string_view, std::string_view>& lookup() {
    static const auto map = [] {
        std::unordered_map<std::string_view, std::string_view> m;
        for (const auto& [en, fr] : frenchTable()) m.emplace(en, fr);
        return m;
    }();
    return map;
}

// Strings handed out by tr(), per language, so that the returned pointers stay valid.
std::mutex g_cache_mutex;
std::unordered_map<std::string, std::string, Hash, std::equal_to<>> g_cache[2];

}  // namespace

Language systemLanguage() {
    int count     = 0;
    auto* locales = SDL_GetPreferredLocales(&count);
    Language lang = Language::English;
    if (locales && count > 0 && locales[0] && locales[0]->language) {
        if (std::string_view(locales[0]->language) == "fr") lang = Language::French;
    }
    SDL_free(locales);
    return lang;
}

Language resolve(const std::string& setting) {
    if (setting == "fr") return Language::French;
    if (setting == "en") return Language::English;
    return systemLanguage();
}

void setLanguage(Language language) { g_language = language; }

Language language() { return g_language; }

const char* tr(const std::string& english) {
    const bool french = g_language == Language::French;

    std::lock_guard<std::mutex> lock(g_cache_mutex);
    auto& cache = g_cache[french ? 1 : 0];
    if (auto it = cache.find(english); it != cache.end()) return it->second.c_str();

    std::string out = english;
    if (french) {
        const auto split  = english.find("##");
        const auto text   = std::string_view(english).substr(0, split);
        const auto suffix = split == std::string::npos ? std::string_view() : std::string_view(english).substr(split);
        if (auto found = lookup().find(text); found != lookup().end())
            out = std::string(found->second) + std::string(suffix);
    }

    return cache.emplace(english, std::move(out)).first->second.c_str();
}

const char* tr(const char* english) {
    if (g_language == Language::English) return english;  // literals are already English
    return tr(std::string(english));
}

}  // namespace fcg::i18n
