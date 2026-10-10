#pragma once

// User interface language. The code is written in English; i18n_table.cpp holds the French text.
//
//   ImGui::Button(TR("Start"));
//   ImGui::Begin(TR("Games###games"));            // "###id" keeps the ImGui id stable across languages
//   auto s = trf("{} games", n);                  // fmt formatting of the translated string

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/include/fmt/core.h>

namespace fcg::i18n {

enum class Language { English, French };

// The first preferred language of the system: French if it is French, English otherwise.
[[nodiscard]] Language systemLanguage();

// "auto", "en" or "fr", as stored in the setup.
[[nodiscard]] Language resolve(const std::string& setting);

void setLanguage(Language language);
[[nodiscard]] Language language();

// Translation of an English UI string. Anything after "##" (an ImGui id) is kept as is.
// Unknown strings are returned unchanged. The pointer stays valid for the life of the program.
[[nodiscard]] const char* tr(const char* english);
[[nodiscard]] const char* tr(const std::string& english);

// The table itself, for the completeness test.
[[nodiscard]] const std::vector<std::pair<std::string_view, std::string_view>>& frenchTable();

}  // namespace fcg::i18n

#define TR(text) ::fcg::i18n::tr(text)

namespace fcg {

template <typename... Args>
[[nodiscard]] std::string trf(const char* english, Args&&... args) {
    return fmt::format(fmt::runtime(i18n::tr(english)), std::forward<Args>(args)...);
}

}  // namespace fcg
