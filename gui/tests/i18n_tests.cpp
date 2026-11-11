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

TEST_CASE("every TR(), trf(), trn() and N_() message has a translation in every language, with the same placeholders") {
    // TR("a" "b") is one string: clang-format splits long literals.
    // The sources are read where they were compiled, or where FCG_SOURCE_DIR points (CI on another machine).
    const char* env    = std::getenv("FCG_SOURCE_DIR");
    const fs::path dir = fs::path(env ? env : FCG_SOURCE_DIR) / "gui" / "src";
    std::set<std::string> used, plural;
    if (fs::is_directory(dir)) {
        const std::string lits = R"re(((?:"(?:[^"\\]|\\.)*"\s*)+))re";
        const std::regex call(R"re(\b(TR|trf|trn|N_)\(\s*)re" + lits);
        const std::regex literal(R"re("((?:[^"\\]|\\.)*)")re");
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() != ".cpp") continue;
            std::ifstream in(entry.path(), std::ios::binary);
            const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            for (std::sregex_iterator it(source.begin(), source.end(), call), end; it != end; ++it) {
                const auto parts = (*it)[2].str();
                std::string text;
                for (std::sregex_iterator p(parts.begin(), parts.end(), literal), e; p != e; ++p)
                    text += unescape((*p)[1].str());
                text = text.substr(0, text.find("##"));
                used.insert(text);
                if ((*it)[1] == "trn") plural.insert(text);
            }
        }
        CHECK(used.size() > 150);
        CHECK(plural.size() > 10);
    } else {
        MESSAGE("sources not found, only the translations are checked: " << dir.string());
    }

    for (const auto& language : i18n::languages()) {
        if (language.language == i18n::Language::English) continue;
        CAPTURE(language.code);
        const auto messages = i18n::messages(language.language);
        const int forms     = i18n::pluralForms(language.language);
        CHECK(messages.size() > 700);
        CHECK(forms >= 1);
        std::set<std::string> ids;
        for (const auto& m : messages) {
            ids.insert(m.id);
            for (const auto& text : m.forms) {
                CHECK_MESSAGE(!text.empty(), "empty: " << m.id);
                CHECK_MESSAGE(placeholders(m.id) == placeholders(text),
                              "placeholders differ: \"" << m.id << "\" / \"" << text << "\"");
            }
            if (plural.count(m.id)) {
                CHECK_MESSAGE(static_cast<int>(m.forms.size()) == forms, "plural forms: " << m.id);
            }
        }
        for (const auto& text : used) CHECK_MESSAGE(ids.count(text) == 1, "no translation for \"" << text << "\"");
    }
}

TEST_CASE("translation keeps ImGui ids and falls back to English") {
    i18n::setLanguage(i18n::Language::English);
    const char* literal = "Games###games";
    CHECK(i18n::tr(literal) == literal);  // English: the literal itself
    CHECK(trn("{} game", "{} games", 1, 1) == "1 game");
    CHECK(trn("{} game", "{} games", 0, 0) == "0 games");

    i18n::setLanguage(i18n::Language::French);
    CHECK(std::string(i18n::tr("Games###games")) == "Parties###games");
    CHECK(std::string(i18n::tr("Flip##replay")) == "Retourner##replay");
    CHECK(std::string(i18n::tr("Stop")) == "Arrêter");
    CHECK(std::string(i18n::tr("not in the table")) == "not in the table");
    CHECK(trn("{} game", "{} games", 12, 12) == "12 parties");
    CHECK(trn("{} game", "{} games", 1, 1) == "1 partie");
    CHECK(trn("{} game", "{} games", 0, 0) == "0 partie");  // French: 0 is singular
    CHECK(trn("not in the table", "not in the tables", 2) == "not in the tables");
    CHECK(std::string(i18n::tr(std::string("White mates"))) == "Les Blancs matent");

    // Pointers stay valid.
    const char* a = i18n::tr("Stop");
    const char* b = i18n::tr("Stop");
    CHECK(a == b);

    i18n::setLanguage(i18n::Language::German);
    CHECK(std::string(i18n::tr("Games###games")) == "Partien###games");
    CHECK(trn("{} game", "{} games", 12, 12) == "12 Partien");
    CHECK(trn("{} game", "{} games", 0, 0) == "0 Partien");
    CHECK(std::string(i18n::tr(std::string("White mates"))) == "Weiß setzt matt");

    // Russian: one form for 1 and 21, another for 2 to 4, a third for 5 to 20.
    i18n::setLanguage(i18n::Language::Russian);
    CHECK(i18n::pluralForms(i18n::Language::Russian) == 3);
    CHECK(trn("{} game", "{} games", 1, 1) != trn("{} game", "{} games", 2, 1));
    CHECK(trn("{} game", "{} games", 2, 1) != trn("{} game", "{} games", 5, 1));
    CHECK(trn("{} game", "{} games", 21, 1) == trn("{} game", "{} games", 1, 1));
    CHECK(trn("{} game", "{} games", 12, 1) == trn("{} game", "{} games", 5, 1));

    i18n::setLanguage(i18n::Language::Chinese);
    CHECK(i18n::pluralForms(i18n::Language::Chinese) == 1);
    CHECK(trn("{} game", "{} games", 1, 1) == trn("{} game", "{} games", 3, 1));

    i18n::setLanguage(i18n::Language::English);
    CHECK(trf("{} vs {}", "A", "B") == "A vs B");

    for (const auto& l : i18n::languages()) CHECK(i18n::resolve(l.code) == l.language);
    CHECK(i18n::resolve("pt") == i18n::Language::Portuguese);
    CHECK(i18n::resolve("zh") == i18n::Language::Chinese);
}
