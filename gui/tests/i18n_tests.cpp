#include <doctest/doctest.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>

#include "i18n.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

// The content of a C string literal, with the escapes the sources use.
std::string unescape(const std::string& literal) {
    std::string out;
    for (std::size_t i = 0; i < literal.size(); ++i) {
        if (literal[i] == '\\' && i + 1 < literal.size()) {
            const char c = literal[++i];
            out += c == 'n' ? '\n' : c;
        } else {
            out += literal[i];
        }
    }
    return out;
}

}  // namespace

TEST_CASE("every TR() and trf() string has a French translation") {
    std::set<std::string_view> french;
    for (const auto& [en, fr] : i18n::frenchTable()) {
        CHECK_MESSAGE(french.insert(en).second, "duplicate entry: " << en);
        CHECK_FALSE(fr.empty());
    }

    // TR("a" "b") is one string: clang-format splits long literals.
    // The sources are read where they were compiled, or where FCG_SOURCE_DIR points (CI on another machine).
    const char* env    = std::getenv("FCG_SOURCE_DIR");
    const fs::path dir = fs::path(env ? env : FCG_SOURCE_DIR) / "gui" / "src";
    if (!fs::is_directory(dir)) {
        MESSAGE("sources not found, skipped: " << dir.string());
        return;
    }

    const std::regex call(R"re(\b(?:TR|trf)\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+))re");
    const std::regex literal(R"re("((?:[^"\\]|\\.)*)")re");
    int checked = 0;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".cpp") continue;

        std::ifstream in(entry.path(), std::ios::binary);
        const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        for (std::sregex_iterator it(source.begin(), source.end(), call), end; it != end; ++it) {
            const auto parts = (*it)[1].str();
            std::string text;
            for (std::sregex_iterator p(parts.begin(), parts.end(), literal), e; p != e; ++p)
                text += unescape((*p)[1].str());
            text = text.substr(0, text.find("##"));
            CHECK_MESSAGE(french.count(text) == 1, entry.path().filename().string()
                                                       << ": no French for \"" << text << "\"");
            ++checked;
        }
    }

    CHECK(checked > 150);
}

TEST_CASE("translation keeps ImGui ids and falls back to English") {
    i18n::setLanguage(i18n::Language::English);
    const char* literal = "Games###games";
    CHECK(i18n::tr(literal) == literal);  // English: the literal itself

    i18n::setLanguage(i18n::Language::French);
    CHECK(std::string(i18n::tr("Games###games")) == "Parties###games");
    CHECK(std::string(i18n::tr("Flip##replay")) == "Retourner##replay");
    CHECK(std::string(i18n::tr("Stop")) == "Arrêter");
    CHECK(std::string(i18n::tr("not in the table")) == "not in the table");
    CHECK(trf("{} games", 12) == "12 parties");
    CHECK(std::string(i18n::tr(std::string("White mates"))) == "Les Blancs matent");

    // Pointers stay valid.
    const char* a = i18n::tr("Stop");
    const char* b = i18n::tr("Stop");
    CHECK(a == b);

    i18n::setLanguage(i18n::Language::English);
    CHECK(trf("{} games", 12) == "12 games");

    CHECK(i18n::resolve("fr") == i18n::Language::French);
    CHECK(i18n::resolve("en") == i18n::Language::English);
}
