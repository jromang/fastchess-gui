// The profiles of the setup's sections: applied to a setup, told apart from it, saved, updated and reverted, and the
// templates that name them.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#include "profiles.hpp"
#include "storage.hpp"

using namespace fcg;
namespace fs = std::filesystem;

TEST_CASE("profiles: every field of a section is a field of the setup, and no field is in two sections") {
    const nlohmann::json all = MatchSetup{};
    std::set<std::string> seen;
    for (const auto s : SETUP_SECTIONS) {
        CHECK(sectionOfKey(sectionKey(s)) == s);
        for (const auto& key : sectionFields(s)) {
            CHECK_MESSAGE(all.contains(key), key);
            CHECK_MESSAGE(seen.insert(key).second, key);
        }
    }
    // The Syzygy directories are this machine's: in no profile.
    CHECK(seen.count("tb_paths") == 0);
    CHECK(!sectionOfKey("tournament"));
}

TEST_CASE("profiles: copying a section copies every field it lists, and nothing of the others") {
    for (const auto s : SETUP_SECTIONS) {
        // Every field of the section given another value than the default.
        nlohmann::json j = MatchSetup{};
        for (const auto& key : sectionFields(s)) {
            auto& v = j[key];
            if (v.is_boolean())
                v = !v.get<bool>();
            else if (v.is_number_unsigned())
                v = v.get<uint64_t>() + 1;
            else if (v.is_number_integer())
                v = v.get<int64_t>() + 1;
            else if (v.is_number_float())
                v = v.get<double>() + 0.5;
            else if (v.is_string())
                v = v.get<std::string>() + "x";
        }
        const auto from = j.get<MatchSetup>();
        MatchSetup to;
        copySection(to, from, s);
        CHECK_MESSAGE(sectionValues(to, s) == sectionValues(from, s), sectionKey(s));
        for (const auto other : SETUP_SECTIONS) {
            if (other != s) CHECK(sectionValues(to, other) == sectionValues(MatchSetup{}, other));
        }
    }
}

TEST_CASE("profiles: a setup goes through JSON unchanged, so that applying a profile changes nothing else") {
    MatchSetup setup;
    setup.format               = TournamentFormat::Gauntlet;
    setup.name                 = "Night";
    setup.participants         = {{"A", 64, 2, 90, 0, 0, {"/books/a.bin", "a", 8}}, {"B"}};
    setup.tc_inc_s             = 0.3;
    setup.tb_paths             = "/tb";
    setup.pgn_out              = "/all.pgn";
    setup.profiles             = {{"time", "Blitz"}};
    setup.panes                = {{"books", 0.3f}};
    const nlohmann::json once  = setup;
    const nlohmann::json twice = once.get<MatchSetup>();
    CHECK(once == twice);
}

TEST_CASE("profiles: applied to a setup, a profile sets its section's fields and nothing else") {
    ProfileStore store;
    const auto* tc = store.find(SetupSection::Time, "40/15");
    REQUIRE(tc);
    CHECK(tc->preset);

    MatchSetup setup;
    setup.format       = TournamentFormat::RoundRobin;
    setup.participants = {{"A"}, {"B"}};
    setup.draw_enabled = true;
    setup.tb_paths     = "/tb";
    applyProfile(setup, SetupSection::Time, *tc);
    CHECK(setup.limit == LimitKind::Time);
    CHECK(setup.tc_moves == 40);
    CHECK(setup.tc_base_s == 900);
    CHECK(setup.tc_inc_s == 0);
    CHECK(sectionProfile(setup, SetupSection::Time) == "40/15");
    // The other sections, and what no profile holds, as they were.
    CHECK(setup.format == TournamentFormat::RoundRobin);
    CHECK(setup.participants.size() == 2);
    CHECK(setup.draw_enabled);
    CHECK(setup.tb_paths == "/tb");
    CHECK(profileState(setup, SetupSection::Time, store) == ProfileState::Same);
}

TEST_CASE("profiles: a setup changed for this tournament differs from its profile, in the fields that matter only") {
    ProfileStore store;
    MatchSetup setup;
    applyProfile(setup, SetupSection::Time, *store.find(SetupSection::Time, "10+0.1"));
    REQUIRE(profileState(setup, SetupSection::Time, store) == ProfileState::Same);

    // A node count that a clock does not use: still the profile.
    setup.nodes = 123456;
    CHECK(profileState(setup, SetupSection::Time, store) == ProfileState::Same);
    setup.tc_inc_s = 0.2;
    CHECK(profileState(setup, SetupSection::Time, store) == ProfileState::Changed);
    setup.tc_inc_s = 0.1;
    setup.limit    = LimitKind::Nodes;
    CHECK(profileState(setup, SetupSection::Time, store) == ProfileState::Changed);

    // The adjudication: the score of a draw rule that is off does not count; turning it on does.
    applyProfile(setup, SetupSection::Adjudication, *store.find(SetupSection::Adjudication, "None"));
    setup.draw_score = 25;
    CHECK(profileState(setup, SetupSection::Adjudication, store) == ProfileState::Same);
    setup.draw_enabled = true;
    CHECK(profileState(setup, SetupSection::Adjudication, store) == ProfileState::Changed);

    // The SPRT calculator's guesses change no game.
    applyProfile(setup, SetupSection::Format, *store.find(SetupSection::Format, "SPRT gainer [0, 3]"));
    setup.sprt_draw_ratio = 0.6;
    CHECK(profileState(setup, SetupSection::Format, store) == ProfileState::Same);
    setup.sprt_elo1 = 2.5;
    CHECK(profileState(setup, SetupSection::Format, store) == ProfileState::Changed);

    // No profile named, or one that is gone.
    setup.profiles.erase("engines");
    CHECK(profileState(setup, SetupSection::Engines, store) == ProfileState::None);
    setup.profiles["engines"] = "Removed since";
    CHECK(profileState(setup, SetupSection::Engines, store) == ProfileState::Missing);
}

TEST_CASE("profiles: saved, updated, reverted and removed; a profile of the user's hides a preset of its name") {
    ProfileStore store;
    MatchSetup setup;
    setup.limit     = LimitKind::Time;
    setup.tc_base_s = 180;
    setup.tc_inc_s  = 2;

    // Save as profile: the section names it, and plays as it.
    const auto& blitz = store.save(SetupSection::Time, "Blitz 3+2", setup);
    CHECK(!blitz.preset);
    setup.profiles["time"] = "Blitz 3+2";
    CHECK(profileState(setup, SetupSection::Time, store) == ProfileState::Same);
    CHECK(store.list(SetupSection::Time).front().name == "Blitz 3+2");  // the user's first, then the presets

    // Changed for this tournament, then reverted.
    setup.tc_inc_s = 3;
    CHECK(profileState(setup, SetupSection::Time, store) == ProfileState::Changed);
    applyProfile(setup, SetupSection::Time, *store.find(SetupSection::Time, "Blitz 3+2"));
    CHECK(setup.tc_inc_s == 2);
    CHECK(profileState(setup, SetupSection::Time, store) == ProfileState::Same);

    // Changed again, then the profile updated: the setup plays as it again.
    setup.tc_inc_s = 3;
    store.save(SetupSection::Time, "Blitz 3+2", setup);
    CHECK(profileState(setup, SetupSection::Time, store) == ProfileState::Same);
    CHECK(store.find(SetupSection::Time, "Blitz 3+2")->setup.tc_inc_s == 3);
    CHECK(std::count_if(store.list(SetupSection::Time).begin(), store.list(SetupSection::Time).end(),
                        [](const auto& p) { return p.name == "Blitz 3+2"; }) == 1);

    // A preset updated: the user's profile of that name takes its place, and removing it brings the preset back.
    const auto presets = store.list(SetupSection::Time).size();
    setup.tc_margin_ms = 50;
    store.save(SetupSection::Time, "40/15", setup);
    CHECK(store.list(SetupSection::Time).size() == presets);
    CHECK(!store.find(SetupSection::Time, "40/15")->preset);
    CHECK(store.find(SetupSection::Time, "40/15")->setup.tc_margin_ms == 50);
    CHECK(store.remove(SetupSection::Time, "40/15"));
    CHECK(store.find(SetupSection::Time, "40/15")->preset);
    CHECK(store.find(SetupSection::Time, "40/15")->setup.tc_moves == 40);
    CHECK(!store.remove(SetupSection::Time, "40/15"));  // a preset is not removed

    // Kept in profiles.json: the user's only, read back the same.
    const auto dir = fs::temp_directory_path() / "fcg-tests-profiles";
    fs::remove_all(dir);
    {
        const Storage storage(dir);
        CHECK(storage.loadProfiles() == nlohmann::json::object());
        storage.saveProfiles(store.toJson());
    }
    const Storage storage(dir);
    ProfileStore again;
    again.fromJson(storage.loadProfiles());
    REQUIRE(again.find(SetupSection::Time, "Blitz 3+2"));
    CHECK(again.find(SetupSection::Time, "Blitz 3+2")->setup.tc_inc_s == 3);
    CHECK(again.toJson() == store.toJson());
    CHECK(!storage.loadProfiles().contains("format"));
    fs::remove_all(dir);
}

TEST_CASE("profiles: a profile written by hand or by an older version takes the defaults for what it lacks") {
    ProfileStore store;
    store.fromJson(nlohmann::json::parse(R"({
        "adjudication": [{"name": "Short", "values": {"draw_enabled": true, "draw_movenumber": 30, "format": 3}},
                         {"name": "Short", "values": {"draw_enabled": true, "draw_movenumber": 32}},
                         {"values": {}}, {"name": ""}, 7],
        "time": "not a list",
        "weather": [{"name": "Sunny"}]
    })"));
    const auto* p = store.find(SetupSection::Adjudication, "Short");
    REQUIRE(p);
    CHECK(p->setup.draw_movenumber == 32);  // the last of the same name
    CHECK(p->setup.draw_movecount == MatchSetup{}.draw_movecount);
    CHECK(!p->values.contains("format"));  // nothing of another section
    MatchSetup setup;
    setup.format = TournamentFormat::Swiss;
    applyProfile(setup, SetupSection::Adjudication, *p);
    CHECK(setup.format == TournamentFormat::Swiss);
    CHECK(store.toJson()["adjudication"].size() == 1);
    CHECK(!store.toJson().contains("time"));
}

TEST_CASE("profiles: an old setup gets the profiles it plays like; the default setup plays like the presets") {
    ProfileStore store;
    MatchSetup setup;
    recognizeProfiles(setup, store);
    for (const auto s : SETUP_SECTIONS) {
        CHECK_MESSAGE(profileState(setup, s, store) == ProfileState::Same, sectionKey(s));
    }
    CHECK(sectionProfile(setup, SetupSection::Time) == "200k nodes");

    // A time control no profile has stays without one; a profile removed since stays named, as missing.
    MatchSetup other;
    other.limit                    = LimitKind::Time;
    other.tc_base_s                = 7;
    other.profiles["adjudication"] = "Gone";
    recognizeProfiles(other, store);
    CHECK(sectionProfile(other, SetupSection::Time).empty());
    CHECK(sectionProfile(other, SetupSection::Adjudication) == "Gone");
    CHECK(profileState(other, SetupSection::Adjudication, store) == ProfileState::Missing);
}

TEST_CASE("templates: a section that played as its profile follows it, one changed for its tournament keeps its own") {
    ProfileStore store;
    MatchSetup setup;
    setup.name             = "Weekly";
    setup.format           = TournamentFormat::RoundRobin;
    setup.participants     = {{"A"}, {"B"}, {"C"}};
    setup.concurrency_auto = false;
    setup.concurrency      = 6;
    setup.limit            = LimitKind::Time;
    setup.tc_base_s        = 120;
    setup.tc_inc_s         = 1;
    store.save(SetupSection::Time, "Blitz", setup);
    setup.profiles["time"] = "Blitz";
    setup.draw_enabled     = true;
    store.save(SetupSection::Adjudication, "Draws", setup);
    setup.profiles["adjudication"] = "Draws";
    setup.draw_score               = 30;  // this tournament only
    REQUIRE(profileState(setup, SetupSection::Adjudication, store) == ProfileState::Changed);

    const auto j = templateJson(setup, store);
    CHECK(j["follow"].size() >= 1);

    // The profile updated since: the template plays it; the section changed for its tournament keeps its values.
    MatchSetup update = setup;
    update.tc_inc_s   = 2;
    store.save(SetupSection::Time, "Blitz", update);
    update.draw_score = 5;
    store.save(SetupSection::Adjudication, "Draws", update);
    const auto loaded = templateSetup(j, store);
    CHECK(loaded.tc_inc_s == 2);
    CHECK(loaded.draw_score == 30);
    CHECK(profileState(loaded, SetupSection::Adjudication, store) == ProfileState::Changed);
    // What no profile holds comes from the template.
    CHECK(loaded.name == "Weekly");
    CHECK(loaded.participants.size() == 3);
    CHECK(loaded.concurrency == 6);

    // A profile removed since: the template's own values.
    store.remove(SetupSection::Time, "Blitz");
    const auto without = templateSetup(j, store);
    CHECK(without.tc_inc_s == 1);
    CHECK(profileState(without, SetupSection::Time, store) == ProfileState::Missing);
}

TEST_CASE("templates: one saved before templates named profiles loads as it was, and gets the profiles it plays like") {
    const auto dir = fs::temp_directory_path() / "fcg-tests-old-template";
    fs::remove_all(dir);
    fs::create_directories(dir);
    MatchSetup old;
    old.limit        = LimitKind::Time;
    old.tc_moves     = 40;
    old.tc_base_s    = 900;
    old.tc_inc_s     = 0;
    old.seeds        = 2;
    nlohmann::json j = old;
    // As 0.4 wrote it: the PGN fields were then off by default, and some did not exist.
    j.erase("revision");
    j.erase("pgn_tbhits");
    j.erase("pgn_decision");
    j["pgn_nodes"] = j["pgn_seldepth"] = j["pgn_nps"] = j["pgn_pv"] = false;
    REQUIRE(writeJson(dir / "old.json", j));
    std::ofstream(dir / "broken.json") << "{ not json";

    ProfileStore store;
    const auto loaded = readTemplate(dir / "old.json", store);
    REQUIRE(loaded);
    CHECK(loaded->seeds == 2);
    CHECK(loaded->pgn_nodes);  // upgraded as readSavedSetup() does
    CHECK(sectionProfile(*loaded, SetupSection::Time) == "40/15");
    CHECK(!readTemplate(dir / "broken.json", store));
    CHECK(!readTemplate(dir / "missing.json", store));
    fs::remove_all(dir);
}

TEST_CASE("profiles: each section said on one line, and as cutechess and fastchess write it") {
    const std::vector<BookEntry> books{{"UHO 2024", "/books/uho.epd"}};
    MatchSetup s;
    s.limit     = LimitKind::Time;
    s.tc_moves  = 40;
    s.tc_base_s = 900;
    s.tc_inc_s  = 0;
    CHECK(sectionSummary(s, SetupSection::Time, books) == "40 moves in 15 min, repeated");
    CHECK(sectionCommandLine(s, SetupSection::Time) == "tc=40/900");
    s.tc_moves  = 0;
    s.tc_base_s = 120;
    s.tc_inc_s  = 1;
    CHECK(sectionSummary(s, SetupSection::Time, books) == "2 min + 1 s per move");
    CHECK(sectionCommandLine(s, SetupSection::Time) == "tc=120+1");
    s.tc_base_s = 10;
    s.tc_inc_s  = 0.1;
    CHECK(sectionSummary(s, SetupSection::Time, books) == "10 s + 0.1 s per move");
    s.limit = LimitKind::Nodes;
    s.nodes = 200000;
    CHECK(sectionSummary(s, SetupSection::Time, books) == "200k nodes per move");
    CHECK(durationText(90) == "1 min 30 s");
    CHECK(durationText(3600) == "1 h");

    s.draw_enabled = true;
    s.tb_enabled   = true;
    s.tb_pieces    = 6;
    CHECK(sectionSummary(s, SetupSection::Adjudication, books) == "draw 40/8/10 cp · TB 6 pieces");
    CHECK(sectionCommandLine(s, SetupSection::Adjudication) ==
          "-draw movenumber=40 movecount=8 score=10 -tb … -tbpieces 6");
    CHECK(sectionSummary(MatchSetup{}, SetupSection::Adjudication, books) == "none: every game is played to its end");

    s.format = TournamentFormat::Gauntlet;
    s.cycles = 2;
    CHECK(sectionSummary(s, SetupSection::Format, books) == "Gauntlet · 1 seed · 2 cycles of 2 games");
    CHECK(sectionSummary(MatchSetup{}, SetupSection::Format, books) == "Match · SPRT [0, 3]");

    s.book = "/books/uho.epd";
    s.seed = 7;
    CHECK(sectionSummary(s, SetupSection::Openings, books) == "UHO 2024 · random · seed 7");
    CHECK(sectionCommandLine(s, SetupSection::Openings) == "-openings file=uho.epd format=epd order=random -srand 7");
    // The randomness only chooses the moves of a Polyglot book: said for the tournament's, and for the participants'
    // own books when it is not the weights as they are.
    s.book_randomness = -40;
    CHECK(sectionSummary(s, SetupSection::Openings, books) == "UHO 2024 · random · seed 7");
    s.participants              = {{"Alpha"}, {"Beta"}};  // a gauntlet
    s.participants[1].book.path = "/books/a.bin";
    CHECK(sectionSummary(s, SetupSection::Openings, books) == "UHO 2024 · random · seed 7 · own books: randomness -40");
    s.participants.clear();
    s.book = "/books/Perfect2023.bin";
    s.seed = 0;
    CHECK(sectionSummary(s, SetupSection::Openings, books) == "Perfect2023.bin · 8 plies · random · randomness -40");
    s.book_randomness = 0;
    s.book_plies      = 12;
    CHECK(sectionSummary(s, SetupSection::Openings, books) ==
          "Perfect2023.bin · 12 plies · random · moves by their "
          "weights");
    CHECK(randomnessText(-100) == "the best moves only");
    CHECK(randomnessText(100) == "every move alike");
    CHECK(randomnessText(25) == "randomness +25");
    s.book       = "/books/uho.epd";
    s.book_plies = -1;

    s.common_hash    = 256;
    s.common_threads = 1;
    s.engine_syzygy  = true;
    CHECK(sectionSummary(s, SetupSection::Engines, books) == "Hash 256 MB · 1 thread · Syzygy");
    CHECK(sectionCommandLine(s, SetupSection::Engines) == "-each option.Hash=256 option.Threads=1 option.SyzygyPath=…");
}
