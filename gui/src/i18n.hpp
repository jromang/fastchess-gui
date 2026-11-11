#pragma once

// User interface language. The code is written in English; the translations are the .po files of gui/i18n, embedded
// in the program and read with tinygettext. messages.pot lists every message.
//
//   ImGui::Button(TR("Start"));
//   ImGui::Begin(TR("Games###games"));                 // "###id" keeps the ImGui id stable across languages
//   auto s = trf("{} vs {}", a, b);                    // fmt formatting of the translated string
//   auto g = trn("{} game", "{} games", n, n);         // the form that suits n, in every language

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <fmt/include/fmt/format.h>

namespace fcg::i18n {

enum class Language { English, French, German, Spanish, Russian, Italian, Portuguese, Chinese };

struct LanguageInfo {
    Language language;
    const char* code;  // in the setup, and the system's language code: "fr"
    const char* po;    // the translation, gui/i18n/<po>.po: "pt_BR"; empty for English
    const char* name;  // in the language itself: "Français"
};

// Every language, in the order the settings show them.
[[nodiscard]] const std::vector<LanguageInfo>& languages();
[[nodiscard]] const LanguageInfo& info(Language language);

// The first preferred language of the system, if there is a translation for it; English otherwise.
[[nodiscard]] Language systemLanguage();

// "auto" or a language code, as stored in the setup.
[[nodiscard]] Language resolve(const std::string& setting);

void setLanguage(Language language);
[[nodiscard]] Language language();

// Translation of an English UI string. Anything after "##" (an ImGui id) is kept as is.
// Unknown strings are returned unchanged. The pointer stays valid for the life of the program.
[[nodiscard]] const char* tr(const char* english);
[[nodiscard]] const char* tr(const std::string& english);

// The form of a message that suits the number n: `one` and `other` are the English singular and plural.
[[nodiscard]] std::string trPlural(const char* one, const char* other, uint64_t n);

// For the tests: every message of a translation, with its forms (several for a plural message), and how many
// plural forms the language has.
struct Message {
    std::string id;
    std::vector<std::string> forms;
};
[[nodiscard]] std::vector<Message> messages(Language language);
[[nodiscard]] int pluralForms(Language language);

}  // namespace fcg::i18n

#define TR(text) ::fcg::i18n::tr(text)
// Marks a string that is translated later, through a variable (a table of labels): gui/i18n/update.py finds it.
#define N_(text) text

namespace fcg {

template <typename... Args>
[[nodiscard]] std::string trf(const char* english, Args&&... args) {
    return fmt::format(fmt::runtime(i18n::tr(english)), std::forward<Args>(args)...);
}

template <typename... Args>
[[nodiscard]] std::string trn(const char* one, const char* other, uint64_t n, Args&&... args) {
    return fmt::format(fmt::runtime(i18n::trPlural(one, other, n)), std::forward<Args>(args)...);
}

}  // namespace fcg
