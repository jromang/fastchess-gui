#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "engine_changes.hpp"
#include "storage.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

UciOptionInfo spin(const std::string& name, const std::string& def, const std::string& min = "1",
                   const std::string& max = "1024") {
    return {name, "spin", def, min, max, {}};
}

UciOptionInfo combo(const std::string& name, const std::string& def, std::vector<std::string> vars) {
    return {name, "combo", def, "", "", std::move(vars)};
}

const OptionChange* row(const ProgramChanges& c, const std::string& name) {
    for (const auto& o : c.options) {
        if (o.name == name) return &o;
    }
    return nullptr;
}

// An engine as its first build was read, with values set by the user.
EngineEntry firstBuild() {
    EngineEntry e;
    e.name        = "Alpha";
    e.cmd         = "/engines/alpha";
    e.id_name     = "Alpha 1.0";
    e.id_author   = "A. Author";
    e.uci_options = {spin("Hash", "16"),
                     spin("Move Overhead", "10", "0", "5000"),
                     spin("Threads", "1", "1", "512"),
                     combo("Style", "Normal", {"Normal", "Risky"}),
                     {"Ponder", "check", "false", "", "", {}},
                     {"Clear Hash", "button", "", "", "", {}},
                     spin("Contempt", "24", "-100", "100")};
    e.options     = {{"Hash", "256"}, {"Threads", "64"}, {"Style", "Risky"}, {"Contempt", "0"}};
    return e;
}

// Its second build: Hash's default up (the value set stays above it), Move Overhead's too (none set), Threads to 32
// at most (the value set no longer fits), Style without Risky, Ponder renamed, Contempt gone, a new option.
EngineRead secondBuild() {
    EngineRead r;
    r.id_name   = "Alpha 1.1";
    r.id_author = "A. Author";
    r.options   = {spin("Hash", "64"),
                   spin("Move Overhead", "30", "0", "5000"),
                   spin("Threads", "1", "1", "32"),
                   combo("Style", "Normal", {"Normal", "Solid"}),
                   {"UCI_Ponder", "check", "false", "", "", {}},
                   {"Clear Hash", "button", "x", "", "", {}},
                   {"EvalFile", "string", "nn-1.nnue", "", "", {}}};
    return r;
}

}  // namespace

TEST_CASE("engine changes: a value fits an option by its type, bounds and choices") {
    CHECK(fitsOption(spin("Hash", "16"), "1024"));
    CHECK_FALSE(fitsOption(spin("Hash", "16"), "2048"));
    CHECK_FALSE(fitsOption(spin("Hash", "16"), "0"));
    CHECK_FALSE(fitsOption(spin("Hash", "16"), "12a"));
    CHECK(fitsOption(spin("Skill", "0", "", "abc"), "-5"));  // bounds that are not numbers are none
    CHECK(fitsOption({"Ponder", "check", "false", "", "", {}}, "true"));
    CHECK_FALSE(fitsOption({"Ponder", "check", "false", "", "", {}}, "yes"));
    CHECK(fitsOption(combo("Style", "Normal", {"Normal", "Risky"}), "risky"));
    CHECK_FALSE(fitsOption(combo("Style", "Normal", {"Normal", "Risky"}), "Solid"));
    CHECK(fitsOption({"EvalFile", "string", "", "", "", {}}, "anything at all"));
}

TEST_CASE("engine changes: the new read against the one before, with the values set") {
    const auto e = firstBuild();
    const auto c = compareReads(lastRead(e), secondBuild(), e.options);

    CHECK(c.nameChanged());
    CHECK(c.id_name_before == "Alpha 1.0");
    CHECK(c.id_name_after == "Alpha 1.1");
    CHECK_FALSE(c.authorChanged());
    CHECK_FALSE(c.empty());

    // Clear Hash: a button, whose default means nothing.
    CHECK(row(c, "Clear Hash") == nullptr);
    REQUIRE(c.options.size() == 8);

    // The choices first, by name.
    CHECK(c.options[0].name == "Hash");
    CHECK(c.options[1].name == "Move Overhead");
    CHECK(c.options[2].name == "Style");
    CHECK(c.options[3].name == "Threads");
    const auto* hash = row(c, "Hash");
    CHECK(hash->before.default_value == "16");
    CHECK(hash->after.default_value == "64");
    CHECK(hash->has_value);
    CHECK(hash->value == "256");
    CHECK(needsChoice(*hash));
    const auto* overhead = row(c, "Move Overhead");
    CHECK_FALSE(overhead->has_value);
    CHECK(needsChoice(*overhead));           // it played with 10, the new default is 30
    CHECK(needsChoice(*row(c, "Threads")));  // the same default, but 64 is above the new maximum
    CHECK(needsChoice(*row(c, "Style")));    // Risky is no longer one of its choices

    // Then what only tells: new options, then the gone ones, with the value they had.
    CHECK(c.options[4].name == "EvalFile");
    CHECK(c.options[4].before.name.empty());
    CHECK(c.options[5].name == "UCI_Ponder");
    CHECK(c.options[6].name == "Contempt");
    CHECK(c.options[6].after.name.empty());
    CHECK(c.options[6].value == "0");
    CHECK(c.options[7].name == "Ponder");
    CHECK_FALSE(needsChoice(c.options[6]));
    CHECK(choicesLeft(c) == 4);
    CHECK(tellsMore(c));

    // The same read twice: nothing changed.
    CHECK(compareReads(lastRead(e), lastRead(e), e.options).empty());
}

TEST_CASE("engine changes: a new default the value set already is, or bounds the value still fits, ask nothing") {
    EngineRead before{"X", "", {spin("Hash", "16"), spin("Threads", "1", "1", "512")}};
    EngineRead after{"X", "", {spin("Hash", "64"), spin("Threads", "1", "1", "1024")}};
    const auto c = compareReads(before, after, {{"Hash", "64"}, {"Threads", "8"}});
    REQUIRE(c.options.size() == 2);
    CHECK_FALSE(needsChoice(c.options[0]));
    CHECK_FALSE(needsChoice(c.options[1]));
    CHECK(choicesLeft(c) == 0);
    CHECK(tellsMore(c));
}

TEST_CASE("engine changes: the read before is found back from the changes and the read after") {
    const auto e     = firstBuild();
    const auto after = secondBuild();
    auto c           = compareReads(lastRead(e), after, e.options);
    const auto back  = readBefore(c, after);
    CHECK(back.id_name == "Alpha 1.0");
    CHECK(compareReads(back, lastRead(e), {}).empty());

    // A decided option counts as seen: as the new read has it.
    c.options[0].choice = 1;
    const auto seen     = readBefore(c, after);
    const auto hash = std::find_if(seen.options.begin(), seen.options.end(), [](auto& o) { return o.name == "Hash"; });
    REQUIRE(hash != seen.options.end());
    CHECK(hash->default_value == "64");
}

TEST_CASE("engine changes: keep my value or use the new default, for one option or all, then the notice goes") {
    auto e = firstBuild();
    REQUIRE(noteChanges(e, secondBuild(), "2026-11-14 10:00"));
    REQUIRE(e.changes);
    CHECK(e.changes->when == "2026-11-14 10:00");

    // Hash: the value set kept. Move Overhead: none was set, the old default is kept as a value.
    CHECK(chooseValue(e, "Hash", ValueChoice::Keep));
    CHECK(*e.option("Hash") == "256");
    CHECK(chooseValue(e, "Move Overhead", ValueChoice::Keep));
    CHECK(*e.option("Move Overhead") == "10");
    CHECK_FALSE(chooseValue(e, "Move Overhead", ValueChoice::NewDefault));  // decided already
    CHECK(row(*e.changes, "Hash")->choice == 1);
    CHECK(choicesLeft(*e.changes) == 2);

    // The others: the new defaults.
    CHECK(chooseAllValues(e, ValueChoice::NewDefault));
    CHECK(e.option("Threads") == nullptr);
    CHECK(e.option("Style") == nullptr);
    CHECK(choicesLeft(*e.changes) == 0);
    // The new name and the options new and gone are still to be read: dismissed by the user.
    CHECK(e.changes.has_value());

    // Changes that only ask: gone once every one is decided.
    EngineEntry f;
    f.uci_options = {spin("Hash", "16"), spin("Move Overhead", "10", "0", "5000")};
    f.options     = {{"Hash", "128"}};
    EngineRead next{"", "", {spin("Hash", "32"), spin("Move Overhead", "50", "0", "5000")}};
    REQUIRE(noteChanges(f, next, "today"));
    CHECK_FALSE(tellsMore(*f.changes));
    CHECK(chooseValue(f, "Hash", ValueChoice::NewDefault));
    CHECK(f.option("Hash") == nullptr);
    CHECK(f.changes.has_value());
    CHECK(chooseAllValues(f, ValueChoice::Keep));
    CHECK(*f.option("Move Overhead") == "10");
    CHECK_FALSE(f.changes.has_value());
}

TEST_CASE("engine changes: several builds before the notice is dismissed make one notice") {
    auto e = firstBuild();
    REQUIRE(noteChanges(e, secondBuild(), "day 1"));
    // The entry takes the read, as applyProbe does; the value of the gone option is dropped from the library.
    auto second   = secondBuild();
    e.id_name     = second.id_name;
    e.uci_options = second.options;
    e.clearOption("Contempt");
    (void)chooseValue(e, "Hash", ValueChoice::NewDefault);

    // A third build: Move Overhead back to 10, Hash's default up again, Contempt still gone.
    auto third       = second;
    third.id_name    = "Alpha 1.2";
    third.options[0] = spin("Hash", "128");
    third.options[1] = spin("Move Overhead", "10", "0", "5000");
    REQUIRE(noteChanges(e, third, "day 2"));
    const auto& c = *e.changes;
    CHECK(c.when == "day 2");
    CHECK(c.id_name_before == "Alpha 1.0");  // against the first build, not yet dismissed
    CHECK(c.id_name_after == "Alpha 1.2");
    CHECK(row(c, "Move Overhead") == nullptr);  // as it was: nothing to tell
    const auto* hash = row(c, "Hash");          // decided for 64, seen; now 64 -> 128
    REQUIRE(hash);
    CHECK(hash->before.default_value == "64");
    CHECK(hash->after.default_value == "128");
    CHECK(hash->choice == 0);
    const auto* contempt = row(c, "Contempt");  // gone, its value still shown
    REQUIRE(contempt);
    CHECK(contempt->value == "0");

    // A build that undoes every change: no notice.
    EngineEntry f = firstBuild();
    REQUIRE(noteChanges(f, secondBuild(), "day 1"));
    f.id_name     = secondBuild().id_name;
    f.uci_options = secondBuild().options;
    CHECK_FALSE(noteChanges(f, lastRead(firstBuild()), "day 2"));
    CHECK_FALSE(f.changes.has_value());
}

TEST_CASE("engine changes: kept in engines.json only while there is a notice; earlier lists read as before") {
    const auto root = fs::temp_directory_path() / "fcg-engine-changes";
    fs::remove_all(root);
    fs::create_directories(root);
    Storage storage(root);

    // engines.json of an earlier version.
    std::ofstream(root / "engines.json") << R"([{"name": "Alpha", "cmd": "/e/alpha", "id_name": "Alpha 1.0"}])";
    auto engines = storage.loadEngines();
    REQUIRE(engines.size() == 1);
    CHECK_FALSE(engines[0].changes.has_value());
    CHECK(engines[0].read_from.size == 0);
    CHECK(nlohmann::json(engines[0]).count("changes") == 0);
    CHECK(nlohmann::json(engines[0]).count("read_from") == 0);

    engines[0] = firstBuild();
    REQUIRE(noteChanges(engines[0], secondBuild(), "2026-11-14 10:00"));
    (void)chooseValue(engines[0], "Hash", ValueChoice::Keep);
    engines[0].read_from = {123, 456, 789};
    storage.saveEngines(engines);
    const auto back = storage.loadEngines();
    REQUIRE(back[0].changes.has_value());
    CHECK(back[0].changes->id_name_after == "Alpha 1.1");
    CHECK(back[0].changes->options.size() == engines[0].changes->options.size());
    CHECK(row(*back[0].changes, "Hash")->choice == 1);
    CHECK(row(*back[0].changes, "Contempt")->value == "0");
    CHECK(back[0].read_from.crc32 == 456);
    fs::remove_all(root);
}

TEST_CASE("engine changes: the program an engine played its last tournament with") {
    tour::Summary older, newer;
    older.spec.name = "Old";
    newer.spec.name = "New";
    tour::Participant a, b, c;
    a.engine.cmd            = "/e/alpha";
    a.program               = {100, 1, 0};
    b.engine.cmd            = "/e/alpha";
    b.program               = {120, 2, 0};
    c.engine.cmd            = "/e/alpha";
    c.engine.args           = "--uci";
    c.program               = {130, 3, 0};
    older.spec.participants = {a, c};
    newer.spec.participants = {b};
    const auto played       = lastPlayedPrograms({newer, older});
    REQUIRE(played.size() == 2);
    CHECK(played.at(programKey("/e/alpha", "")).tournament == "New");
    CHECK(played.at(programKey("/e/alpha", "")).program.size == 120);
    CHECK(played.at(programKey("/e/alpha", "--uci")).tournament == "Old");

    EngineEntry e;
    e.cmd           = "/e/alpha";
    e.program       = {120, 2, 0};
    e.program_state = ProgramState::Ok;
    CHECK(rebuiltSincePlayed(e, played) == nullptr);
    e.program         = {150, 9, 0};
    const auto* since = rebuiltSincePlayed(e, played);
    REQUIRE(since);
    CHECK(since->tournament == "New");
    // Changed since its last read, not read again yet: changed since the tournament too.
    e.program       = {120, 2, 0};
    e.program_state = ProgramState::Changed;
    CHECK(rebuiltSincePlayed(e, played) != nullptr);
    e.program_state = ProgramState::Missing;  // told otherwise
    CHECK(rebuiltSincePlayed(e, played) == nullptr);
    e.cmd           = "/e/beta";  // never played
    e.program_state = ProgramState::Ok;
    CHECK(rebuiltSincePlayed(e, played) == nullptr);
}

TEST_CASE("engine changes: the participants of a tournament whose program changed or went missing since they joined") {
    const auto dir = fs::temp_directory_path() / "fcg-engine-changes-programs";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto write = [&](const char* name, const char* text) {
        std::ofstream(dir / name, std::ios::binary | std::ios::trunc) << text;
        return (dir / name).string();
    };
    tour::TournamentSpec spec;
    for (const auto* name : {"A", "B", "C", "D"}) {
        tour::Participant p;
        p.id          = static_cast<int>(spec.participants.size()) + 1;
        p.engine.name = name;
        p.engine.cmd  = write(name, "build 1");
        p.program     = tour::fingerprint(p.engine.cmd);
        spec.participants.push_back(p);
    }
    spec.participants[3].active = false;  // withdrawn: it plays no more

    CHECK(changedPrograms(spec).empty());
    const auto built = fs::last_write_time(dir / "A");
    write("A", "build 2");
    fs::last_write_time(dir / "A", built + std::chrono::seconds(2));
    fs::remove(dir / "B");
    write("D", "build 22");
    const auto changed = changedPrograms(spec);
    REQUIRE(changed.size() == 2);
    CHECK(changed[0].name == "A");
    CHECK_FALSE(changed[0].missing);
    CHECK(changed[1].name == "B");
    CHECK(changed[1].missing);
    fs::remove_all(dir);
}
