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

// The {} and printf placeholders of a string, sorted: a translation must keep them, or trf() throws.
std::multiset<std::string> placeholders(std::string_view text) {
    static const std::regex mark(R"re(\{[^{}]*\}|%[-+#0]*\d*(?:\.\d+)?(?:zu|ld|lld|[dsfcu%]))re");
    std::multiset<std::string> out;
    const std::string s(text);
    for (std::sregex_iterator it(s.begin(), s.end(), mark), end; it != end; ++it) out.insert(it->str());
    return out;
}

TEST_CASE("every TR() and trf() string has a French and a German translation, with the same placeholders") {
    const std::pair<const char*, const std::vector<std::pair<std::string_view, std::string_view>>*> tables[] = {
        {"French", &i18n::frenchTable()}, {"German", &i18n::germanTable()}};
    std::set<std::string_view> keys[2];
    for (int t = 0; t < 2; ++t) {
        for (const auto& [en, text] : *tables[t].second) {
            CHECK_MESSAGE(keys[t].insert(en).second, tables[t].first << ": duplicate entry: " << en);
            CHECK_FALSE(text.empty());
            CHECK_MESSAGE(placeholders(en) == placeholders(text),
                          tables[t].first << ": placeholders differ: \"" << en << "\" / \"" << text << "\"");
        }
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
            for (int t = 0; t < 2; ++t) {
                CHECK_MESSAGE(keys[t].count(text) == 1, entry.path().filename().string()
                                                            << ": no " << tables[t].first << " for \"" << text << "\"");
            }
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

    i18n::setLanguage(i18n::Language::German);
    CHECK(std::string(i18n::tr("Games###games")) == "Partien###games");
    CHECK(trf("{} games", 12) == "12 Partien");
    CHECK(std::string(i18n::tr(std::string("White mates"))) == "Weiß setzt matt");

    i18n::setLanguage(i18n::Language::English);
    CHECK(trf("{} games", 12) == "12 games");

    CHECK(i18n::resolve("de") == i18n::Language::German);
    CHECK(i18n::resolve("fr") == i18n::Language::French);
    CHECK(i18n::resolve("en") == i18n::Language::English);
}
