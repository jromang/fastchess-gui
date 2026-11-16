#include "engine_hub.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <thread>

#include <fmt/include/fmt/format.h>
#include <mbedtls/platform_util.h>
#include <psa/crypto.h>

#include "engine_relay.hpp"
#include "i18n.hpp"
#include "machines.hpp"
#include "net.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr std::size_t HELLO_LIMIT   = 4096;
constexpr std::size_t INPUT_LIMIT   = std::size_t(1) << 20;  // read no more from a relay until its engine took this
constexpr auto HELLO_TIMEOUT      = std::chrono::seconds(10);
constexpr auto GIVE_UP_AFTER_END  = std::chrono::seconds(15);  // an engine not gone this long after its end is let go
constexpr int MAX_PASSES          = 64;                          // over the programs of a connection, at one time
constexpr std::size_t PID_LINE_LIMIT = 32;

std::string randomSecret() {
    uint8_t bytes[16];
    if (psa_crypto_init() != PSA_SUCCESS || psa_generate_random(bytes, sizeof bytes) != PSA_SUCCESS) return {};
    std::string out;
    for (const auto b : bytes) out += fmt::format("{:02x}", b);
    return out;
}

// Compared in a time that does not tell how much of it matched.
bool sameSecret(const std::string& a, const std::string& b) {
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

void wipe(std::string& s) {
    if (!s.empty()) mbedtls_platform_zeroize(s.data(), s.size());
    s.clear();
}

void setEnvironment(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

}  // namespace

struct EngineHub::Impl {
    Options options;
    net::TcpListener listener;
    std::string secret;
    uint16_t port = 0;
    std::thread thread;
    std::atomic_bool stop{false};

    // Shared with the window's thread and the connecting threads.
    mutable std::mutex mutex;
    std::map<std::string, tour::RemoteEngine> allowed;  // by key
    std::map<std::string, std::string> passphrases;     // by key file
    std::vector<MachineState> states;
    std::vector<Cut> cuts;

    struct Connection {
        std::string machine;
        std::unique_ptr<SshClient> client;
        int sessions = 0;
        Clock::time_point idle_since{};
        Clock::time_point keepalive_at{};
        // Ending the engines whose relay went and that did not end by themselves.
        std::vector<std::unique_ptr<SshClient::Program>> enders;
    };

    // A relay, and its engine once started.
    struct Session {
        std::unique_ptr<net::Socket> socket;
        std::string in, out;  // from the relay (the hello, then the engine's input); frames for the relay
        enum class Phase { Hello, Waiting, Running, Closing } phase = Phase::Hello;
        Clock::time_point since = Clock::now();
        relay::Hello hello;
        tour::RemoteEngine engine;
        Connection* connection = nullptr;
        std::unique_ptr<SshClient::Program> program;
        bool wants_pid = false;  // the first line of the output is the process id there
        std::string pid_line;
        int64_t remote_pid = 0;
        bool relay_closed = false;  // the relay sent the end of its input, or went
        bool relay_gone   = false;  // its socket failed: nothing more goes to it
        bool input_closed = false;
        Clock::time_point end_at{};  // once its input closed: when the engine is ended there if it still runs
        bool ender_started = false;
    };

    // A machine the relays asked for.
    struct Slot {
        Machine machine;
        std::vector<std::unique_ptr<Connection>> connections;
        std::vector<Session*> waiting;  // for a channel
        bool connecting = false;
        std::string error;
        SshError::Kind kind = SshError::Kind::None;
        Clock::time_point failed_at{};
        std::chrono::system_clock::time_point error_since{};
    };

    // What a connecting thread made, taken by the hub's thread.
    struct Made {
        std::string machine;
        std::unique_ptr<SshClient> client;
        SshError error;
    };
    std::mutex made_mutex;
    std::vector<Made> made;
    struct Connector {
        std::thread thread;
        std::shared_ptr<std::atomic_bool> done;
    };
    std::vector<Connector> connectors;

    // The hub's thread only.
    std::vector<std::unique_ptr<Session>> sessions;
    std::map<std::string, Slot> slots;
    std::vector<std::string> order;  // the machines, as first asked for

    explicit Impl(Options o) : options(std::move(o)) {}

    // --- Sessions ---

    void finish(Session& s, relay::Frame type, const std::string& payload) {
        s.out += relay::frame(type, payload);
        s.phase = Session::Phase::Closing;
    }

    void failWaiting(Slot& slot, const std::string& why) {
        for (auto* s : slot.waiting) finish(*s, relay::Frame::Failure, why);
        slot.waiting.clear();
    }

    static std::string machineName(const Machine& m) { return m.name.empty() ? m.host : m.name; }

    // The machine `id`, read again from machines.json (its address or key may have changed meanwhile).
    std::optional<Machine> readMachine(const std::string& id) const {
        for (auto& m : loadMachines(options.profile)) {
            if (m.id == id) return m;
        }
        return std::nullopt;
    }

    Slot& slotFor(const std::string& id) {
        auto [it, added] = slots.try_emplace(id);
        if (added) order.push_back(id);
        return it->second;
    }

    void handleHello(Session& s) {
        const auto end = s.in.find('\n');
        if (end == std::string::npos) {
            if (s.in.size() > HELLO_LIMIT || s.relay_closed || Clock::now() - s.since > HELLO_TIMEOUT) {
                s.phase      = Session::Phase::Closing;  // not a relay: let go without a word
                s.relay_gone = true;
            }
            return;
        }
        const auto hello = relay::parseHello(std::string_view(s.in).substr(0, end));
        s.in.erase(0, end + 1);  // what follows is the engine's input already
        if (!hello || !sameSecret(hello->secret, secret)) {
            s.phase      = Session::Phase::Closing;
            s.relay_gone = true;
            return;
        }
        s.hello = *hello;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto it = allowed.find(hello->key);
            if (it == allowed.end()) {
                finish(s, relay::Frame::Failure, TR("fastchess-gui was not asked to start this engine on a machine."));
                return;
            }
            s.engine = it->second;
        }
        auto& slot = slotFor(s.engine.machine);
        if (slot.connections.empty() && !slot.connecting) {
            const auto machine = readMachine(s.engine.machine);
            if (!machine) {
                finish(s, relay::Frame::Failure,
                       TR("The machine of this engine is not in Settings > Machines any more: choose where it runs on "
                          "its card."));
                return;
            }
            slot.machine = *machine;
        }
        s.phase = Session::Phase::Waiting;
        slot.waiting.push_back(&s);
    }

    // Starts the engine of `s` on `c`; false when the session ended instead.
    bool startOn(Session& s, Slot& slot, Connection& c) {
        const auto& m  = slot.machine;
        const auto cmd = engineCommand(m.system.shell, s.engine.program, s.engine.args, s.engine.dir);
        if (m.system.shell == RemoteShell::Unknown) {
            finish(s, relay::Frame::Failure,
                   trf("{} has never been tested, so its host key is unknown and nothing was sent to it: test its "
                       "connection first (Settings > Machines).",
                       machineName(m)));
            return false;
        }
        if (!cmd) {
            finish(s, relay::Frame::Failure,
                   trf("{}: cmd on {} cannot take one of its arguments (a double quote, a % or a line break).",
                       s.engine.program, machineName(m)));
            return false;
        }
        s.program = c.client->launch(*cmd);
        if (!s.program) return false;
        s.connection = &c;
        s.wants_pid  = m.system.shell == RemoteShell::Posix;
        s.phase      = Session::Phase::Running;
        ++c.sessions;
        c.idle_since = {};
        return true;
    }

    void connectAsync(Slot& slot, const std::string& id) {
        if (const auto machine = readMachine(id)) slot.machine = *machine;
        if (const auto missing = incompleteMachine(slot.machine); !missing.empty()) {
            failWaiting(slot, trf("{}: {}", machineName(slot.machine), missing));
            return;
        }
        std::string passphrase;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (const auto it = passphrases.find(slot.machine.key_file); it != passphrases.end()) {
                passphrase = it->second;
            }
        }
        auto target = targetOf(slot.machine, options.profile, passphrase);
        wipe(passphrase);
        target.trust_new_host_key = false;
        target.timeout            = options.connect_timeout;
        slot.connecting           = true;
        auto done                 = std::make_shared<std::atomic_bool>(false);
        connectors.push_back({std::thread([this, id, target = std::move(target), done]() mutable {
                                  auto client = std::make_unique<SshClient>(&stop);
                                  Made result;
                                  result.machine = id;
                                  if (client->connect(target)) {
                                      result.client = std::move(client);
                                  } else {
                                      result.error = client->error();
                                  }
                                  wipe(target.passphrase);
                                  {
                                      std::lock_guard<std::mutex> lock(made_mutex);
                                      made.push_back(std::move(result));
                                  }
                                  *done = true;
                              }),
                              done});
    }

    void reapConnectors() {
        for (auto it = connectors.begin(); it != connectors.end();) {
            if (*it->done) {
                it->thread.join();
                it = connectors.erase(it);
            } else {
                ++it;
            }
        }
    }

    void takeMade() {
        std::vector<Made> ready;
        {
            std::lock_guard<std::mutex> lock(made_mutex);
            ready.swap(made);
        }
        const auto now = Clock::now();
        for (auto& r : ready) {
            auto& slot      = slotFor(r.machine);
            slot.connecting = false;
            if (r.client) {
                auto c          = std::make_unique<Connection>();
                c->machine      = r.machine;
                c->client       = std::move(r.client);
                c->idle_since   = now;
                c->keepalive_at = now + options.keepalive;
                slot.connections.push_back(std::move(c));
                slot.error.clear();
                slot.kind = SshError::Kind::None;
            } else {
                slot.error       = r.error.message;
                slot.kind        = r.error.kind;
                slot.failed_at   = now;
                slot.error_since = std::chrono::system_clock::now();
                failWaiting(slot, slot.error);
            }
        }
    }

    void assign(Slot& slot, const std::string& id) {
        while (!slot.waiting.empty()) {
            Connection* free = nullptr;
            for (auto& c : slot.connections) {
                if (c->client->connected() && c->sessions < options.channels_per_connection) {
                    free = c.get();
                    break;
                }
            }
            if (!free) break;
            auto* s = slot.waiting.front();
            slot.waiting.erase(slot.waiting.begin());
            if (!startOn(*s, slot, *free) && s->phase != Session::Phase::Closing) {
                finish(*s, relay::Frame::Failure, trf("The connection to {} broke.", machineName(slot.machine)));
            }
        }
        if (slot.waiting.empty() || slot.connecting) return;
        if (slot.kind != SshError::Kind::None && slot.failed_at != Clock::time_point{} &&
            Clock::now() - slot.failed_at < options.retry_after) {
            failWaiting(slot, slot.error);  // tried a moment ago: the same answer, without trying again at once
            return;
        }
        connectAsync(slot, id);
    }

    // A relay's input in, its end noted.
    void readRelay(Session& s) {
        if (s.relay_closed || s.phase == Session::Phase::Closing) return;
        while (s.in.size() < INPUT_LIMIT) {
            const auto n = s.socket->receive(s.in, INPUT_LIMIT - s.in.size());
            if (n == 0) break;
            if (n < 0) {
                s.relay_closed = true;
                break;
            }
        }
    }

    void feedEngine(Session& s) {
        if (s.phase != Session::Phase::Running || !s.program) return;
        if (!s.in.empty()) {
            s.program->write(s.in);
            s.in.clear();
        }
        if (s.relay_closed && !s.input_closed) {
            s.program->closeInput();
            s.input_closed = true;
            s.end_at       = Clock::now() + options.end_after;
        }
    }

    // What the engine wrote, to its relay; its end, once it ended.
    void drain(Session& s) {
        auto out = s.program->takeOutput();
        if (s.wants_pid && !out.empty()) {
            s.pid_line += out;
            out.clear();
            const auto eol = s.pid_line.find('\n');
            if (eol != std::string::npos) {
                const auto digits = s.pid_line.substr(0, eol);
                try {
                    s.remote_pid = std::stoll(digits);
                } catch (...) {
                    s.remote_pid = 0;
                }
                out = s.pid_line.substr(eol + 1);
                s.pid_line.clear();
                s.wants_pid = false;
            } else if (s.pid_line.size() > PID_LINE_LIMIT) {
                out = std::exchange(s.pid_line, {});
                s.wants_pid = false;
            }
        }
        if (!out.empty()) s.out += relay::frame(relay::Frame::Output, out);
        if (const auto err = s.program->takeErrors(); !err.empty()) s.out += relay::frame(relay::Frame::Errors, err);
        if (!s.program->done()) return;
        // Its connection broke: cut() tells every relay of it at once, and says which.
        if (s.program->state() == SshClient::Program::State::Failed && s.connection &&
            !s.connection->client->connected()) {
            return;
        }
        if (s.program->state() == SshClient::Program::State::Ended) {
            const int code = s.program->exitCode();
            finish(s, relay::Frame::Exit, std::to_string(code < 0 ? 1 : code));
        } else {
            finish(s, relay::Frame::Failure, s.program->failure());
        }
        release(s);
    }

    void release(Session& s) {
        s.program.reset();
        if (s.connection) --s.connection->sessions;
        s.connection = nullptr;
    }

    // The connection `c` of `slot` broke: its engines' relays are told, and the cut kept for the tournament.
    void cut(Slot& slot, Connection& c, const std::string& reason) {
        Cut record;
        record.machine = c.machine;
        record.reason  = reason;
        record.when    = std::chrono::system_clock::now();
        c.client->abandon();  // its channels let go without waiting for a server that may not answer
        for (auto& s : sessions) {
            if (s->connection != &c) continue;
            if (s->hello.pid) record.pids.push_back(s->hello.pid);
            if (s->hello.parent) record.pids.push_back(s->hello.parent);  // fastchess' process: the WinBoard adapter
            s->program.reset();
            s->connection = nullptr;
            finish(*s, relay::Frame::Cut, reason);
        }
        c.sessions = 0;
        c.enders.clear();
        if (!record.pids.empty()) {
            std::lock_guard<std::mutex> lock(mutex);
            cuts.push_back(std::move(record));
        }
        slot.error       = reason;
        slot.kind        = SshError::Kind::Network;
        slot.error_since = std::chrono::system_clock::now();
    }

    void serveConnections() {
        const auto now = Clock::now();
        for (auto& [id, slot] : slots) {
            for (auto it = slot.connections.begin(); it != slot.connections.end();) {
                auto& c = **it;
                if (!c.client->connected()) {
                    cut(slot, c, trf("The connection to {} broke.", machineName(slot.machine)));
                    it = slot.connections.erase(it);
                    continue;
                }
                if (now - c.client->lastReceived() > options.silence) {
                    cut(slot, c,
                        trf("{} stopped answering: the machine is off, or the network is down.",
                            machineName(slot.machine)));
                    it = slot.connections.erase(it);
                    continue;
                }
                if (now >= c.keepalive_at) {
                    c.client->keepAlive(std::chrono::duration_cast<std::chrono::seconds>(options.keepalive));
                    c.keepalive_at = now + options.keepalive;
                }
                serve(c);
                if (!c.client->connected()) {
                    cut(slot, c, trf("The connection to {} broke.", machineName(slot.machine)));
                    it = slot.connections.erase(it);
                    continue;
                }
                if (c.sessions == 0 && c.enders.empty()) {
                    if (c.idle_since == Clock::time_point{}) c.idle_since = now;
                    if (now - c.idle_since >= options.idle_close) {
                        it = slot.connections.erase(it);
                        continue;
                    }
                } else {
                    c.idle_since = {};
                }
                ++it;
            }
        }
    }

    // Moves the programs of `c` until nothing more moves, the data one read off the socket for another included.
    void serve(Connection& c) {
        for (int pass = 0; pass < MAX_PASSES; ++pass) {
            bool moved = false;
            for (auto& s : sessions) {
                if (s->connection != &c || !s->program) continue;
                feedEngine(*s);
                moved |= s->program->step();
                drain(*s);
            }
            for (auto& e : c.enders) moved |= e->step();
            if (!c.client->connected()) return;  // cut by the caller
            for (auto& s : sessions) {
                if (s->connection == &c && s->program && s->program->pending()) moved = true;
            }
            if (!moved) break;
        }
        c.enders.erase(std::remove_if(c.enders.begin(), c.enders.end(), [](const auto& e) { return e->done(); }),
                       c.enders.end());
        // The engines whose relay went: ended there if they do not end by themselves.
        const auto now = Clock::now();
        for (auto& s : sessions) {
            if (s->connection != &c || !s->program || !s->input_closed || now < s->end_at) continue;
            if (!s->ender_started && s->remote_pid > 0) {
                if (auto ender = c.client->launch(endProcessCommand(s->remote_pid))) c.enders.push_back(std::move(ender));
                s->ender_started = true;
            }
            if (s->remote_pid <= 0 || now >= s->end_at + GIVE_UP_AFTER_END) {
                // Nothing more to do there (no process id on Windows' cmd): its channel closed, its relay told.
                finish(*s, relay::Frame::Exit, "1");
                release(*s);
            }
        }
    }

    void sendRelays() {
        for (auto& s : sessions) {
            if (s->relay_gone) {
                s->out.clear();
                continue;
            }
            while (!s->out.empty()) {
                const auto n = s->socket->send(s->out);
                if (n == 0) break;
                if (n < 0) {
                    s->relay_gone   = true;
                    s->relay_closed = true;
                    s->out.clear();
                    break;
                }
                s->out.erase(0, static_cast<std::size_t>(n));
            }
        }
    }

    void dropClosed() {
        for (auto& [id, slot] : slots) {
            slot.waiting.erase(std::remove_if(slot.waiting.begin(), slot.waiting.end(),
                                              [](Session* s) {
                                                  return s->phase != Session::Phase::Waiting || s->relay_closed;
                                              }),
                               slot.waiting.end());
        }
        sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
                                      [](const std::unique_ptr<Session>& s) {
                                          if (s->phase == Session::Phase::Waiting && s->relay_closed) return true;
                                          return s->phase == Session::Phase::Closing &&
                                                 (s->out.empty() || s->relay_gone) && !s->program;
                                      }),
                       sessions.end());
    }

    void publish() {
        std::vector<MachineState> now;
        for (const auto& id : order) {
            const auto& slot = slots.at(id);
            MachineState m;
            m.id          = id;
            m.name        = machineName(slot.machine);
            m.connections = static_cast<int>(slot.connections.size());
            for (const auto& c : slot.connections) m.engines += c->sessions;
            m.error      = slot.error;
            m.error_kind = slot.kind;
            m.since      = slot.error_since;
            now.push_back(std::move(m));
        }
        std::lock_guard<std::mutex> lock(mutex);
        states = std::move(now);
    }

    int nextWaitMs() const {
        const auto now = Clock::now();
        auto until     = now + milliseconds(1000);
        const auto soon = [&](Clock::time_point t) { until = std::min(until, t); };
        if (!connectors.empty()) soon(now + milliseconds(20));
        for (const auto& s : sessions) {
            if (s->phase == Session::Phase::Hello) soon(s->since + HELLO_TIMEOUT);
            if (s->program && s->input_closed) soon(s->ender_started ? s->end_at + GIVE_UP_AFTER_END : s->end_at);
        }
        for (const auto& [id, slot] : slots) {
            for (const auto& c : slot.connections) {
                soon(c->keepalive_at);
                soon(c->client->lastReceived() + options.silence);
                if (c->idle_since != Clock::time_point{}) soon(c->idle_since + options.idle_close);
                if (!c->enders.empty()) soon(now + milliseconds(50));
            }
        }
        const auto ms = std::chrono::duration_cast<milliseconds>(until - now).count();
        return static_cast<int>(std::clamp<int64_t>(ms, 0, 1000));
    }

    void run() {
        while (!stop) {
            reapConnectors();
            takeMade();
            while (const auto fd = listener.accept(0)) {
                auto s    = std::make_unique<Session>();
                s->socket = std::make_unique<net::Socket>(*fd);
                sessions.push_back(std::move(s));
            }
            for (auto& s : sessions) {
                readRelay(*s);
                if (s->phase == Session::Phase::Hello) handleHello(*s);
            }
            for (auto& id : order) assign(slots.at(id), id);
            serveConnections();
            sendRelays();
            dropClosed();
            publish();

            std::vector<net::Wait> waits;
            waits.push_back({listener.handle(), true, false});
            for (const auto& s : sessions) {
                const bool read  = !s->relay_closed && s->phase != Session::Phase::Closing && s->in.size() < INPUT_LIMIT;
                const bool write = !s->relay_gone && !s->out.empty();
                if (read || write) waits.push_back({s->socket->handle(), read, write});
            }
            for (const auto& [id, slot] : slots) {
                for (const auto& c : slot.connections) {
                    waits.push_back({static_cast<net::Handle>(c->client->socketHandle()), true,
                                     c->client->waitsToWrite()});
                }
            }
            net::waitAny(waits, nextWaitMs());
        }
        // Going: the engines' channels before their connections.
        for (auto& s : sessions) s->program.reset();
        sessions.clear();
        slots.clear();
    }
};

EngineHub::EngineHub(Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}

EngineHub::~EngineHub() {
    impl_->stop = true;
    if (impl_->thread.joinable()) {
        // A connection of its own wakes its thread at once.
        if (const auto fd = net::connectTo(0x7f000001, impl_->port, 1000)) net::Socket wake(*fd);
        impl_->thread.join();
    }
    for (auto& c : impl_->connectors) c.thread.join();
    impl_->listener.close();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto& [file, secret] : impl_->passphrases) wipe(secret);
}

bool EngineHub::start(std::string& error) {
    if (impl_->thread.joinable()) return true;
    impl_->secret = randomSecret();
    if (impl_->secret.empty()) {
        error = TR("The cryptography cannot start.");
        return false;
    }
    if (const auto e = impl_->listener.listen(0, true); !e.empty()) {
        error = e;
        return false;
    }
    impl_->port   = impl_->listener.port();
    impl_->thread = std::thread([this] { impl_->run(); });
    return true;
}

std::string EngineHub::environment() const { return relay::hubEnvironment(impl_->port, impl_->secret); }

void EngineHub::exportEnvironment() const { setEnvironment(relay::ENV, environment()); }

void clearHubEnvironment() {
#ifdef _WIN32
    _putenv_s(relay::ENV, "");
#else
    unsetenv(relay::ENV);
#endif
}

void EngineHub::allow(const std::vector<tour::RemoteEngine>& engines) {
    std::map<std::string, tour::RemoteEngine> by_key;
    for (const auto& e : engines) by_key.emplace(relay::keyOf(e), e);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->allowed = std::move(by_key);
}

void EngineHub::setPassphrase(const std::string& key_file, std::string passphrase) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto& kept = impl_->passphrases[key_file];
    wipe(kept);
    if (passphrase.empty()) {
        impl_->passphrases.erase(key_file);
    } else {
        kept = std::move(passphrase);
    }
    wipe(passphrase);
}

std::vector<EngineHub::MachineState> EngineHub::machines() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->states;
}

std::vector<EngineHub::Cut> EngineHub::takeCuts() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return std::exchange(impl_->cuts, {});
}

}  // namespace fcg
