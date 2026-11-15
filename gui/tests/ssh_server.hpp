#pragma once

// An SSH server for the tests of the remote machines, in the process, on ssh_wire's packets and key exchange: it lets
// a user in with a key (an RSA signature checked, an Ed25519 one taken on its length: PSA cannot verify it) or
// without any, runs scripted commands on exec channels, and serves SFTP (version 3, as OpenSSH) on a folder of the
// test's. One connection at a time, then the next.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fcg::ssh {

class MockServer {
   public:
    // What a command does: its output, error output and exit status at once; or, with `talk`, a program that answers
    // what it reads (its output to each piece of input; `ended` set once it exits, with `status`).
    struct Command {
        std::string out, err;
        int status = 0;
        std::function<std::string(const std::string& input, bool input_closed, bool& ended)> talk;
    };

    struct Options {
        std::string user = "tester";
        // The public key blobs a user may log in with; none: anyone, without a key.
        std::vector<std::string> keys;
        std::filesystem::path root;  // SFTP: the user's home folder, "/" too
        std::function<Command(const std::string& command)> exec;
        // Ends the connection once SFTP wrote this many bytes in it (0: never): a network cut in the middle of a copy.
        uint64_t cut_after = 0;
    };

    MockServer(uint16_t port, Options options);
    ~MockServer();
    MockServer(const MockServer&)            = delete;
    MockServer& operator=(const MockServer&) = delete;

    // What the tests read, once the clients are done.
    [[nodiscard]] std::vector<std::string> commands() const;
    [[nodiscard]] std::string error() const;
    std::atomic<int> connections{0};
    std::atomic<int> logins{0};
    std::atomic<int> ed25519_logins{0};  // with a signature of the right length, not verified
    std::atomic<int> rsa_logins{0};      // with a signature verified
    std::atomic<uint64_t> written{0};    // by SFTP, every connection

   private:
    struct Connection;
    void serve();

    Options options_;
    struct Listener;
    std::unique_ptr<Listener> listener_;
    std::thread thread_;
    std::atomic_bool quit_{false};
    mutable std::mutex mutex_;
    std::vector<std::string> commands_;
    std::string error_;
};

// The blob of an OpenSSH public key line ("ssh-ed25519 AAAA... comment"): the base64 field decoded.
[[nodiscard]] std::string publicKeyBlob(const std::string& line);

}  // namespace fcg::ssh
