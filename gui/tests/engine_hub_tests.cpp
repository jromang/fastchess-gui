// The engines that run on a remote machine: the relay's protocol, the commands that start and end them there, and the
// engine hub against the test SSH server (several connections at once, a limit of channels, cuts and silences).

#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <libssh2.h>  // before windows.h: it includes winsock2.h

#include "engine_hub.hpp"
#include "engine_relay.hpp"
#include "free_ports.hpp"
#include "machines.hpp"
#include "net.hpp"
#include "ssh_client.hpp"
#include "ssh_server.hpp"

using namespace fcg;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

// Waits until `done()`, as long as `limit` at most: what is awaited, not a fixed sleep.
template <typename F>
bool waitFor(F done, std::chrono::milliseconds limit = 20s) {
    const auto until = Clock::now() + limit;
    while (!done()) {
        if (Clock::now() > until) return false;
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

fs::path freshDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("fcg-hub-" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

// A profile with one Linux machine on the test server's port, tested or not (its host key known).
Machine writeMachine(const fs::path& profile, uint16_t port) {
    Machine m;
    m.id           = "box1";
    m.name         = "test-box";
    m.host         = "127.0.0.1";
    m.port         = port;
    m.user         = "tester";
    m.auth         = SshAuth::Agent;  // the test server lets the user in without a key
    m.system.shell = RemoteShell::Posix;
    m.system.os    = "Linux";
    m.system.checked = "2026-11-15 10:00";  // the system is kept once tested
    REQUIRE(saveMachines(profile, {m}));
    return m;
}

// The machine's host key trusted, as its test on the Machines page does.
void testMachine(const fs::path& profile, const Machine& m) {
    SshClient client;
    auto target    = targetOf(m, profile, "");
    target.timeout = 10s;
    REQUIRE(client.connect(target));
    CHECK(client.hostKeyNew());
}

// The words a POSIX shell reads in `line`: single quotes taken as they are, a backslash outside them escaping the
// next character, the rest split on spaces (operators such as && stay words of their own here).
std::vector<std::string> shWords(const std::string& line) {
    std::vector<std::string> words;
    std::string word;
    bool in_word = false, quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '\'') {
                quoted = false;
            } else {
                word += c;
            }
        } else if (c == '\'') {
            quoted  = true;
            in_word = true;
        } else if (c == '\\' && i + 1 < line.size()) {
            word += line[++i];
            in_word = true;
        } else if (c == ' ') {
            if (in_word) words.push_back(std::exchange(word, {}));
            in_word = false;
        } else {
            word += c;
            in_word = true;
        }
    }
    if (in_word) words.push_back(word);
    return words;
}

tour::RemoteEngine engineAt(const std::string& program, std::vector<std::string> args = {}) {
    return {"box1", program, std::move(args), ""};
}

// A Linux box with engines: a UCI one, one that ends on neither quit nor the end of its input, and the commands that
// end processes.
ssh::MockServer::Command engines(const std::string& command) {
    ssh::MockServer::Command reply;
    if (command.find("exec '\\''/engines/mock'\\''") != std::string::npos) {
        reply.out  = "4242\n";  // its process id, before anything
        reply.talk = [](const std::string& input, bool closed, bool& ended) {
            std::string out;
            if (input.find("uci") != std::string::npos) out += "id name Mock 1.0\nuciok\n";
            if (input.find("isready") != std::string::npos) out += "readyok\n";
            ended = closed || input.find("quit") != std::string::npos;
            return out;
        };
    } else if (command.find("exec '\\''/engines/stubborn'\\''") != std::string::npos) {
        reply.out  = "777\n";
        reply.talk = [](const std::string& input, bool, bool& ended) {
            ended = false;
            return input.find("uci") != std::string::npos ? std::string("uciok\n") : std::string();
        };
    } else if (command.find("kill ") != std::string::npos) {
        reply.status = 0;
    } else {
        reply.err    = "sh: not found\n";
        reply.status = 127;
    }
    return reply;
}

// fastchess' side of a relay: relay::run() on a thread, its input written by the test, its output collected.
class TestRelay {
   public:
    TestRelay(const std::string& key, const std::string& hub) {
        thread_ = std::thread([shared = shared_, key, hub] {
            const int code = relay::run(
                key, hub,
                [shared](char* buffer, std::size_t size) -> long {
                    std::unique_lock<std::mutex> lock(shared->mutex);
                    shared->cv.wait(lock, [&] { return !shared->in.empty() || shared->closed; });
                    if (shared->in.empty()) return 0;
                    auto& front  = shared->in.front();
                    const auto n = std::min(size, front.size());
                    std::copy_n(front.data(), n, buffer);
                    front.erase(0, n);
                    if (front.empty()) shared->in.pop_front();
                    return static_cast<long>(n);
                },
                [shared](std::string_view d) {
                    std::lock_guard<std::mutex> lock(shared->mutex);
                    shared->out.append(d);
                },
                [shared](std::string_view d) {
                    std::lock_guard<std::mutex> lock(shared->mutex);
                    shared->err.append(d);
                });
            shared->code = code;
        });
    }
    ~TestRelay() {
        close();
        if (thread_.joinable()) thread_.join();
    }
    TestRelay(const TestRelay&)            = delete;
    TestRelay& operator=(const TestRelay&) = delete;

    void send(const std::string& text) {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->in.push_back(text);
        shared_->cv.notify_all();
    }
    // The end of the engine's input, as fastchess closing its pipe.
    void close() {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->closed = true;
        shared_->cv.notify_all();
    }
    std::string out() const {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        return shared_->out;
    }
    std::string err() const {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        return shared_->err;
    }
    bool saw(const std::string& text, std::chrono::milliseconds limit = 20s) const {
        return waitFor([&] { return out().find(text) != std::string::npos; }, limit);
    }
    // Its exit code once it ended, within `limit`; nullopt while it runs.
    std::optional<int> exit(std::chrono::milliseconds limit = 20s) {
        if (!waitFor([&] { return shared_->code != RUNNING; }, limit)) return std::nullopt;
        thread_.join();
        return shared_->code.load();
    }

   private:
    static constexpr int RUNNING = -1000;
    struct Shared {
        mutable std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::string> in;
        bool closed = false;
        std::string out, err;
        std::atomic<int> code{RUNNING};
    };
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    std::thread thread_;
};

EngineHub::Options hubOptions(const fs::path& profile) {
    EngineHub::Options o;
    o.profile         = profile;
    o.connect_timeout = 10s;
    return o;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------

TEST_CASE("Relay: frames read back whole however they arrive; a stream that is not frames is told") {
    const auto stream = relay::frame(relay::Frame::Output, "id name Mock\nuciok\n") +
                        relay::frame(relay::Frame::Errors, "") + relay::frame(relay::Frame::Exit, "0");
    relay::FrameReader reader;
    std::vector<std::pair<relay::Frame, std::string>> got;
    for (const char c : stream) {  // a byte at a time
        reader.feed(std::string(1, c));
        while (auto f = reader.next()) got.push_back(*f);
    }
    REQUIRE(got.size() == 3);
    CHECK(got[0].first == relay::Frame::Output);
    CHECK(got[0].second == "id name Mock\nuciok\n");
    CHECK(got[1].first == relay::Frame::Errors);
    CHECK(got[1].second.empty());
    CHECK(got[2].first == relay::Frame::Exit);
    CHECK(got[2].second == "0");
    CHECK_FALSE(reader.broken());

    relay::FrameReader bad;
    bad.feed("zzzzzzzz");
    CHECK_FALSE(bad.next().has_value());
    CHECK(bad.broken());
}

TEST_CASE("Relay: the hello and the hub's environment read back; an engine's key the same for the same engine") {
    const auto line = relay::helloLine({"s3cr3t", "abc", 1234});
    CHECK(line.back() == '\n');
    const auto hello = relay::parseHello(line.substr(0, line.size() - 1));
    REQUIRE(hello);
    CHECK(hello->secret == "s3cr3t");
    CHECK(hello->key == "abc");
    CHECK(hello->pid == 1234);
    CHECK_FALSE(relay::parseHello("not json").has_value());
    CHECK_FALSE(relay::parseHello("{\"key\":\"abc\"}").has_value());

    const auto env = relay::parseHubEnvironment(relay::hubEnvironment(40123, "f00d"));
    REQUIRE(env);
    CHECK(env->first == 40123);
    CHECK(env->second == "f00d");
    CHECK_FALSE(relay::parseHubEnvironment("").has_value());
    CHECK_FALSE(relay::parseHubEnvironment("70000:x").has_value());
    CHECK_FALSE(relay::parseHubEnvironment("12:").has_value());

    const auto a = engineAt("/engines/sf", {"--threads", "2"});
    CHECK(relay::keyOf(a) == relay::keyOf(a));
    CHECK(relay::keyOf(a).size() == 32);
    CHECK(relay::keyOf(a) != relay::keyOf(engineAt("/engines/sf", {"--threads", "3"})));
    CHECK(relay::keyOf(a) != relay::keyOf(engineAt("/engines/sf", {"--threads 2"})));
}

TEST_CASE("Machines: an engine's command quotes each argument, its process id first on a POSIX machine") {
    const auto posix = engineCommand(RemoteShell::Posix, "/opt/sf dev/stockfish", {"--x", "a'b; rm -rf ~"}, "");
    REQUIRE(posix);
    const auto outer = shWords(*posix);
    REQUIRE(outer.size() == 3);
    CHECK(outer[0] == "sh");
    CHECK(outer[1] == "-c");
    CHECK(shWords(outer[2]) == std::vector<std::string>{"echo", "$$;", "cd", "/opt/sf dev", "&&", "exec",
                                                        "/opt/sf dev/stockfish", "--x", "a'b; rm -rf ~"});
    const auto in_dir = engineCommand(RemoteShell::Posix, "sf", {}, "/data/nets");
    REQUIRE(in_dir);
    CHECK(shWords(shWords(*in_dir)[2]) ==
          std::vector<std::string>{"echo", "$$;", "cd", "/data/nets", "&&", "exec", "sf"});
    const auto windows = engineCommand(RemoteShell::Cmd, "C:\\Engines\\sf.exe", {"a b"}, "");
    REQUIRE(windows);
    CHECK(*windows == "cd /d \"C:\\Engines\" && \"C:\\Engines\\sf.exe\" \"a b\"");
    CHECK_FALSE(engineCommand(RemoteShell::Cmd, "C:\\sf.exe", {"100%"}, "").has_value());
    CHECK(endProcessCommand(4242) == "sh -c 'kill 4242 2>/dev/null; sleep 1; kill -9 4242 2>/dev/null; true'");
}

TEST_CASE("Engine hub: an engine played through a relay, after its machine was tested; never one not allowed") {
    const auto profile = freshDir("relay");
    const auto port    = test::freePort();
    ssh::MockServer::Options server_options;
    server_options.root = profile;
    server_options.exec = engines;
    ssh::MockServer server(port, server_options);
    const auto machine = writeMachine(profile, port);

    EngineHub hub(hubOptions(profile));
    std::string error;
    REQUIRE(hub.start(error));
    const auto mock = engineAt("/engines/mock");
    hub.allow({mock});

    SUBCASE("a machine never tested is refused, nothing sent") {
        TestRelay relay(relay::keyOf(mock), hub.environment());
        const auto code = relay.exit();
        REQUIRE(code);
        CHECK(*code == 1);
        CHECK(relay.err().find("never been tested") != std::string::npos);
        CHECK(server.commands().empty());
        CHECK(waitFor([&] {
            const auto states = hub.machines();
            return states.size() == 1 && states[0].error_kind == SshError::Kind::HostKey;
        }));
    }

    SUBCASE("once tested: the engine answers, its process id kept by the hub, and it ends with quit") {
        testMachine(profile, machine);
        TestRelay relay(relay::keyOf(mock), hub.environment());
        relay.send("uci\n");
        CHECK_MESSAGE(relay.saw("uciok"), std::string(relay.err() + " | " +
                                                      (hub.machines().empty() ? "" : hub.machines()[0].error) + " | " +
                                                      server.error()));
        relay.send("isready\n");
        CHECK(relay.saw("readyok"));
        CHECK(relay.out().find("4242") == std::string::npos);
        CHECK(relay.out().rfind("id name Mock 1.0\n", 0) == 0);
        CHECK(waitFor([&] {
            const auto states = hub.machines();
            return states.size() == 1 && states[0].name == "test-box" && states[0].engines == 1 &&
                   states[0].error.empty();
        }));
        relay.send("quit\n");
        const auto code = relay.exit();
        REQUIRE(code);
        CHECK(*code == 0);
        CHECK(waitFor([&] { return hub.machines()[0].engines == 0; }));
    }

    SUBCASE("an engine the hub was not told about, or a relay without the secret, is refused") {
        testMachine(profile, machine);
        TestRelay other(relay::keyOf(engineAt("/bin/sh", {"-c", "id"})), hub.environment());
        const auto code = other.exit();
        REQUIRE(code);
        CHECK(*code == 1);
        CHECK(other.err().find("not asked to start") != std::string::npos);

        const auto env = relay::parseHubEnvironment(hub.environment());
        REQUIRE(env);
        TestRelay forged(relay::keyOf(mock), relay::hubEnvironment(env->first, "0123456789abcdef"));
        const auto forged_code = forged.exit();
        REQUIRE(forged_code);
        CHECK(*forged_code == 1);
        for (const auto& c : server.commands()) CHECK(c.find("/bin/sh") == std::string::npos);
        CHECK(server.commands().empty());
    }
    fs::remove_all(profile);
}

TEST_CASE("Engine hub: twelve engines on two connections of ten channels; idle connections closed") {
    const auto profile = freshDir("pool");
    const auto port    = test::freePort();
    ssh::MockServer::Options server_options;
    server_options.root         = profile;
    server_options.exec         = engines;
    server_options.max_sessions = 10;
    ssh::MockServer server(port, server_options);
    const auto machine = writeMachine(profile, port);
    testMachine(profile, machine);
    REQUIRE(waitFor([&] { return server.live == 0; }));
    const int before = server.connections;

    auto options       = hubOptions(profile);
    options.idle_close = 300ms;
    EngineHub hub(options);
    std::string error;
    REQUIRE(hub.start(error));
    const auto mock = engineAt("/engines/mock");
    hub.allow({mock});

    std::vector<std::unique_ptr<TestRelay>> relays;
    for (int i = 0; i < 12; ++i) relays.push_back(std::make_unique<TestRelay>(relay::keyOf(mock), hub.environment()));
    for (auto& r : relays) r->send("uci\n");
    for (auto& r : relays) CHECK(r->saw("uciok"));
    CHECK(server.connections - before == 2);
    CHECK(server.refused_channels == 0);
    CHECK(waitFor([&] {
        const auto states = hub.machines();
        return states.size() == 1 && states[0].connections == 2 && states[0].engines == 12;
    }));

    for (auto& r : relays) r->send("quit\n");
    for (auto& r : relays) CHECK(r->exit() == std::optional<int>(0));
    CHECK(waitFor([&] { return server.live == 0; }));  // closed once idle
    CHECK(waitFor([&] { return hub.machines()[0].connections == 0; }));
    fs::remove_all(profile);
}

TEST_CASE("Engine hub: a relay that goes has its engine ended there, by its process id") {
    const auto profile = freshDir("end");
    const auto port    = test::freePort();
    ssh::MockServer::Options server_options;
    server_options.root = profile;
    server_options.exec = engines;
    ssh::MockServer server(port, server_options);
    const auto machine = writeMachine(profile, port);
    testMachine(profile, machine);

    auto options      = hubOptions(profile);
    options.end_after = 200ms;
    auto hub          = std::make_unique<EngineHub>(options);
    std::string error;
    REQUIRE(hub->start(error));
    const auto stubborn = engineAt("/engines/stubborn");
    hub->allow({stubborn});

    TestRelay relay(relay::keyOf(stubborn), hub->environment());
    relay.send("uci\n");
    REQUIRE(relay.saw("uciok"));
    relay.close();  // fastchess closed its input, and the engine does not end
    const auto ended = [&] {
        for (const auto& c : server.commands()) {
            if (c.find("kill 777") != std::string::npos) return true;
        }
        return false;
    };
    CHECK(waitFor(ended));
    hub.reset();  // the test server's engine does not die of it: the relay ends with the hub
    CHECK(relay.exit() == std::optional<int>(1));
    fs::remove_all(profile);
}

TEST_CASE("Engine hub: a cut ends the relays and says which; a machine that stops answering is cut too") {
    const auto profile = freshDir("cut");
    const auto port    = test::freePort();
    ssh::MockServer::Options server_options;
    server_options.root = profile;
    server_options.exec = engines;
    ssh::MockServer server(port, server_options);
    const auto machine = writeMachine(profile, port);
    testMachine(profile, machine);

    auto options      = hubOptions(profile);
    options.keepalive = 200ms;
    options.silence   = 1500ms;
    EngineHub hub(options);
    std::string error;
    REQUIRE(hub.start(error));
    const auto mock = engineAt("/engines/mock");
    hub.allow({mock});

    SUBCASE("the network cut") {
        TestRelay a(relay::keyOf(mock), hub.environment());
        TestRelay b(relay::keyOf(mock), hub.environment());
        a.send("uci\n");
        b.send("uci\n");
        REQUIRE(a.saw("uciok"));
        REQUIRE(b.saw("uciok"));
        server.cut();
        CHECK(a.exit() == std::optional<int>(1));
        CHECK(b.exit() == std::optional<int>(1));
        CHECK(a.err().find("test-box") != std::string::npos);
        std::vector<EngineHub::Cut> cuts;
        REQUIRE(waitFor([&] {
            auto more = hub.takeCuts();
            cuts.insert(cuts.end(), more.begin(), more.end());
            return !cuts.empty();
        }));
        REQUIRE(cuts.size() == 1);
        CHECK(cuts[0].machine == "box1");
        CHECK(cuts[0].pids.size() >= 2);  // each relay's process, and the process that started it
        CHECK(hub.takeCuts().empty());  // taken once

        // The next engine connects again.
        TestRelay c(relay::keyOf(mock), hub.environment());
        c.send("uci\n");
        CHECK(c.saw("uciok"));
        CHECK(waitFor([&] { return hub.machines()[0].error.empty(); }));
    }

    SUBCASE("a machine that stops answering") {
        TestRelay a(relay::keyOf(mock), hub.environment());
        a.send("uci\n");
        REQUIRE(a.saw("uciok"));
        server.freeze(true);
        CHECK(a.exit() == std::optional<int>(1));
        CHECK(a.err().find("stopped answering") != std::string::npos);
        CHECK(waitFor([&] { return !hub.takeCuts().empty(); }));
        server.freeze(false);
    }
    fs::remove_all(profile);
}
