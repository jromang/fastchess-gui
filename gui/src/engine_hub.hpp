#pragma once

// The engines that run on remote machines (machines.hpp), for the programs this process starts: fastchess in a
// tournament process, the window's reading of an engine, its checks, console and quick test. Each engine is started as
// a relay (engine_relay.hpp) that connects here on the loopback and names its engine by its key; the hub starts the
// engine on its machine over SSH, on a channel of a connection shared with the machine's other engines (10 channels a
// connection at most, the MaxSessions of an OpenSSH server), and passes the bytes both ways. A relay that goes, even
// killed, has its engine ended there; a connection that breaks ends the relays of its engines, and the hub says which.
//
// One thread serves every relay and every connection without waiting on any of them, so that no engine's move waits
// for another's; connections are made on threads of their own, as logging in takes round trips. A connection never
// trusts a host key the test of the machine did not show (SshTarget::trust_new_host_key).

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <tour/model.hpp>

#include "ssh_client.hpp"

namespace fcg {

// Takes FCG_ENGINE_HUB out of this process's environment: a process without a hub of its own (a tournament process,
// which the window started with its own hub in its environment) must not hand its engines to another's.
void clearHubEnvironment();

class EngineHub {
   public:
    struct Options {
        std::filesystem::path profile;  // machines.json and known_hosts.json
        int channels_per_connection = 10;
        std::chrono::milliseconds idle_close{60000};   // a connection without engines is closed after this
        std::chrono::milliseconds end_after{2000};     // an engine whose relay went is ended after this
        std::chrono::milliseconds retry_after{5000};   // a machine that could not be reached is tried again after this
        std::chrono::milliseconds keepalive{10000};    // an SSH keepalive this often on a connection
        std::chrono::milliseconds silence{45000};      // a connection whose server says nothing this long is gone
        std::chrono::milliseconds connect_timeout{20000};
    };

    explicit EngineHub(Options options);
    ~EngineHub();
    EngineHub(const EngineHub&)            = delete;
    EngineHub& operator=(const EngineHub&) = delete;

    // Listens on the loopback and starts serving. False with `error` when it cannot.
    bool start(std::string& error);
    // FCG_ENGINE_HUB's value: the port and the secret the relays need.
    [[nodiscard]] std::string environment() const;
    // Puts it in this process's environment, for the programs started from now on.
    void exportEnvironment() const;

    // The engines it may start, by their key (relay::keyOf()); a relay asking for another one is refused.
    void allow(const std::vector<tour::RemoteEngine>& engines);
    // A key file's passphrase, kept in memory and wiped when the hub goes; empty forgets it.
    void setPassphrase(const std::string& key_file, std::string passphrase);

    struct MachineState {
        std::string id;
        std::string name;
        int connections = 0;
        int engines     = 0;  // playing now
        // Why the last connection could not be made or broke; empty since one works.
        std::string error;
        SshError::Kind error_kind = SshError::Kind::None;
        std::chrono::system_clock::time_point since{};  // of the error
    };
    // The machines it was asked for, in the order they first were.
    [[nodiscard]] std::vector<MachineState> machines() const;

    // A connection that broke while engines played on it: the machine, why, and the relays it ended (their process
    // ids, which the engines' process trees name). Taken once.
    struct Cut {
        std::string machine;
        std::string reason;
        std::vector<int64_t> pids;
        std::chrono::system_clock::time_point when{};
    };
    [[nodiscard]] std::vector<Cut> takeCuts();

    struct Impl;

   private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace fcg
