// The engines of the library that run on a machine, apart from the hub (engine_hub_tests.cpp): how they are started,
// kept, counted, checked before a start, and played by a tournament that runs on their machine.

#include <doctest/doctest.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include <engine/uci_engine.hpp>
#include <tour/budget.hpp>
#include <tour/edits.hpp>
#include <tour/model.hpp>

#include "engine_relay.hpp"
#include "engines.hpp"
#include "machine_run.hpp"
#include "machines.hpp"
#include "memory.hpp"
#include "portable.hpp"
#include "setup_checks.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

Machine gpuBox() {
    Machine m;
    m.id             = "gpu1";
    m.name           = "gpu-box";
    m.host           = "10.0.0.7";
    m.user           = "jf";
    m.system.shell   = RemoteShell::Posix;
    m.system.os      = "Linux";
    m.system.arch    = "x86_64";
    m.system.home    = "/home/jf";
    m.system.checked = "2026-11-15 10:00";
    return m;
}

EngineEntry remoteLc0() {
    EngineEntry e;
    e.name          = "Lc0 0.31";
    e.machine       = "gpu1";
    e.cmd           = "/home/jf/engines/lc0/lc0";
    e.args          = "--backend=cuda-fp16 \"--weights=/data/nets/BT4 big.pb.gz\"";
    e.round_trip_ms = 0.4;
    e.options       = {{"Hash", "1024"}, {"Threads", "2"}};
    return e;
}

EngineEntry localGaia() {
    EngineEntry e;
    e.name    = "Gaia";
    e.cmd     = "/home/me/engines/gaia";
    e.options = {{"Hash", "256"}};
    return e;
}

MatchSetup roundRobin(const std::vector<std::string>& names) {
    MatchSetup s;
    s.format = TournamentFormat::RoundRobin;
    for (const auto& n : names) s.participants.push_back({n});
    s.limit     = LimitKind::Time;
    s.tc_base_s = 10;
    s.tc_inc_s  = 0.1;
    return s;
}

bool hasFix(const Notice& n, Fix::Kind kind) {
    for (const auto& f : n.fixes) {
        if (f.kind == kind) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("Remote engines: an engine on a machine starts as this program's relay, by the key of its engine there") {
    const auto saved                  = fastchess::engine::xboard_adapter;
    fastchess::engine::xboard_adapter = "/opt/fcg/fastchess-gui";
    const auto lc0                    = remoteLc0();
    const auto remote                 = remoteEngine(lc0);
    REQUIRE(remote);
    CHECK(remote->machine == "gpu1");
    CHECK(remote->program == "/home/jf/engines/lc0/lc0");
    CHECK(remote->args == std::vector<std::string>{"--backend=cuda-fp16", "--weights=/data/nets/BT4 big.pb.gz"});
    const auto start = engineStart(lc0);
    CHECK(start.cmd == "/opt/fcg/fastchess-gui");
    CHECK(start.args == std::string(relay::FLAG) + " " + relay::keyOf(*remote));
    CHECK(fs::is_directory(start.dir));  // the relay's, any folder of this computer

    const auto gaia = localGaia();
    CHECK_FALSE(remoteEngine(gaia).has_value());
    CHECK(engineStart(gaia).cmd == gaia.cmd);
    fastchess::engine::xboard_adapter = saved;

    // The arguments, split and joined back as fastchess splits them.
    const std::vector<std::string> args{"-x", "a b", "it's", "say \"hi\""};
    CHECK(splitArguments(joinArguments({"-x", "a b"})) == std::vector<std::string>{"-x", "a b"});
    CHECK(joinArguments(args) == "-x \"a b\" \"it's\" 'say \"hi\"'");
    CHECK(splitArguments("  ").empty());
}

TEST_CASE("Remote engines: kept in the library and in a tournament's spec and edits, their paths left to their machine") {
    auto lc0       = remoteLc0();
    lc0.local_cmd  = "/home/me/engines/lc0";
    lc0.remote_sha = "3f9a";
    const nlohmann::json j = lc0;
    CHECK(j.at("machine") == "gpu1");
    CHECK(j.at("local_cmd") == "/home/me/engines/lc0");
    const auto back = j.get<EngineEntry>();
    CHECK(back.machine == "gpu1");
    CHECK(back.local_cmd == lc0.local_cmd);
    CHECK(back.remote_sha == "3f9a");
    CHECK(back.round_trip_ms == doctest::Approx(0.4));
    // An engine of this computer keeps none of it.
    const nlohmann::json local = localGaia();
    CHECK_FALSE(local.contains("machine"));
    CHECK_FALSE(local.contains("round_trip_ms"));

    tour::Participant p;
    p.id     = 3;
    p.remote = remoteEngine(lc0);
    const nlohmann::json pj = p;
    const auto p_back       = pj.get<tour::Participant>();
    REQUIRE(p_back.remote);
    CHECK(*p_back.remote == *p.remote);
    CHECK_FALSE(nlohmann::json(tour::Participant{}).contains("remote"));

    tour::Edit edit;
    edit.remote = remoteEngine(lc0);
    const auto e_back = nlohmann::json(edit).get<tour::Edit>();
    REQUIRE(e_back.remote);
    CHECK(*e_back.remote == *edit.remote);

    // A portable profile names this computer's files relative to the program's folder, never the paths there.
    portable::setBase("/home/jf");
    const auto stored = portable::stored(lc0);
    CHECK(stored.cmd == "/home/jf/engines/lc0/lc0");
    auto resolved = stored;
    resolved.cmd  = "engines/lc0/lc0";  // relative to the home folder there
    portable::resolve(resolved);
    CHECK(resolved.cmd == "engines/lc0/lc0");
    portable::setBase({});
}

TEST_CASE("Remote engines: this computer counts nothing of their threads, hash and GPU, only their relay") {
    tour::Participant local, remote;
    local.engine.options  = {{"Hash", "256"}, {"Threads", "4"}};
    remote.engine.options = {{"Hash", "4096"}, {"Threads", "16"}};
    remote.gpu            = true;
    remote.remote         = remoteEngine(remoteLc0());
    const auto cost       = tour::gameCost(local, remote);
    CHECK(cost.threads == 4);
    CHECK(cost.gpus == 0);
    CHECK(cost.memory_mb == tour::processMb(local.engine) + tour::PROCESS_MB);

    tour::TournamentSpec spec;
    spec.participants = {remote, remote};
    const auto largest = tour::largestGame(spec);
    CHECK(largest.threads == 1);
    CHECK(largest.gpus == 0);
    CHECK(largest.memory_mb == 2 * tour::PROCESS_MB);

    const std::vector<EngineEntry> engines{remoteLc0(), localGaia()};
    const auto setup  = roundRobin({"Lc0 0.31", "Gaia"});
    const auto hashes = setupHashes(setup, engines);
    REQUIRE(hashes.size() == 2);
    CHECK(hashes[0] == 0);
    CHECK(hashes[1] == 256);
    const auto game = setupGameCost(setup, engines);
    CHECK(game.gpus == 0);
}

TEST_CASE("Remote engines: before a start, their machine, its round trip against the clock, and Run on") {
    const auto box     = gpuBox();
    auto lc0           = remoteLc0();
    auto setup         = roundRobin({"Lc0 0.31", "Gaia"});
    const auto engines = [&] { return std::vector<EngineEntry>{lc0, localGaia()}; };

    // 0.4 ms of a 100 ms increment: a line, no warning.
    auto notices = remoteNotices(setup, engines(), {box});
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].level == Notice::Level::Ok);
    CHECK(notices[0].text.find("gpu-box") != std::string::npos);

    // 38 ms of it: a warning, and the margin it asks for.
    lc0.round_trip_ms = 38;
    notices           = remoteNotices(setup, engines(), {box});
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].level == Notice::Level::Warning);
    REQUIRE(hasFix(notices[0], Fix::Kind::SetMargin));
    CHECK(notices[0].fixes[0].value == 80);
    CHECK(marginFor(38) == 80);
    CHECK(marginFor(0.4) == 10);
    setup.tc_margin_ms = 80;  // set: no more warning
    CHECK(remoteNotices(setup, engines(), {box})[0].level == Notice::Level::Ok);
    // Without an increment, against the time a move has (10 s over 40 moves): 38 ms is 15 %.
    setup.tc_margin_ms = 0;
    setup.tc_inc_s     = 0;
    CHECK(remoteNotices(setup, engines(), {box})[0].level == Notice::Level::Warning);
    // No clock, no warning.
    setup.limit = LimitKind::Nodes;
    CHECK(remoteNotices(setup, engines(), {box})[0].level == Notice::Level::Ok);

    // A machine never tested, or gone: refused, with what to do.
    auto untested           = box;
    untested.system.checked = "";
    notices                 = remoteNotices(setup, engines(), {untested});
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].level == Notice::Level::Error);
    CHECK(hasFix(notices[0], Fix::Kind::TestMachine));
    notices = remoteNotices(setup, engines(), {});
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].level == Notice::Level::Error);
    CHECK(hasFix(notices[0], Fix::Kind::OpenEngine));

    // Run on another machine: refused, with here or its machine; on its own machine: played there.
    auto other    = box;
    other.id      = "tc1";
    other.name    = "tc-box";
    setup.machine = "tc1";
    notices       = remoteNotices(setup, engines(), {box, other});
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].level == Notice::Level::Error);
    REQUIRE(notices[0].fixes.size() == 2);
    CHECK(notices[0].fixes[0].kind == Fix::Kind::SetRunOn);
    CHECK(notices[0].fixes[0].path.empty());
    CHECK(notices[0].fixes[1].path == "gpu1");
    setup.machine = "gpu1";
    CHECK(remoteNotices(setup, engines(), {box, other})[0].level == Notice::Level::Ok);
}

TEST_CASE("Remote engines: a tournament run on their machine plays them there as its own engines") {
    const auto dir = fs::temp_directory_path() / "fcg-remote-spec";
    fs::create_directories(dir);
    auto box    = gpuBox();
    box.folder  = "fcg";
    box.engines = {{"Lc0 0.31", "/home/jf/engines/lc0/lc0", -1, "", ""}};

    tour::TournamentSpec spec;
    tour::Participant p;
    p.id          = 1;
    p.engine.name = "Lc0 0.31";
    p.engine.cmd  = "/opt/fcg/fastchess-gui";
    p.engine.args = std::string(relay::FLAG) + " abc";
    p.remote      = remoteEngine(remoteLc0());
    p.remote->dir = "/data/nets";
    spec.participants.push_back(p);

    tour::TournamentSpec out;
    REQUIRE(remoteSpec(spec, box, {}, dir, remoteTournamentDir(box, dir), out).empty());
    const auto& there = out.participants[0];
    CHECK(there.engine.cmd == "/home/jf/engines/lc0/lc0");
    CHECK(there.engine.args == "--backend=cuda-fp16 \"--weights=/data/nets/BT4 big.pb.gz\"");
    CHECK(there.engine.dir == "/data/nets");
    CHECK_FALSE(there.remote.has_value());

    // An engine of another machine cannot play there.
    auto other = box;
    other.id   = "tc1";
    CHECK_FALSE(remoteSpec(spec, other, {}, dir, remoteTournamentDir(other, dir), out).empty());
    fs::remove_all(dir);
}
