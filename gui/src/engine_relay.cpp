#include "engine_relay.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

#ifdef _WIN32
#    include <winsock2.h>
#    include <windows.h>
#else
#    include <unistd.h>
#endif

#include <json.hpp>

#include "machines.hpp"
#include "net.hpp"

namespace fcg::relay {

namespace {

constexpr std::size_t HEADER     = 5;                      // the type, and the length on 4 bytes
constexpr uint32_t MAX_FRAME     = 64u << 20;              // more is not a frame the hub sends
constexpr uint32_t LOOPBACK      = 0x7f000001;             // 127.0.0.1
constexpr int CONNECT_TIMEOUT_MS = 5000;

bool knownFrame(char type) {
    return type == 'o' || type == 'e' || type == 'x' || type == 'f' || type == 'c';
}

// All of `data` on the socket, waiting as long as the hub does not take it; false once the connection is gone.
bool sendAll(net::Socket& socket, std::string_view data) {
    while (!data.empty()) {
        const auto n = socket.send(data);
        if (n < 0) return false;
        if (n == 0) {
            std::vector<net::Wait> waits{{socket.handle(), false, true}};
            net::waitAny(waits, 1000);
            continue;
        }
        data.remove_prefix(static_cast<std::size_t>(n));
    }
    return true;
}

int64_t ownPid() {
#ifdef _WIN32
    return static_cast<int64_t>(GetCurrentProcessId());
#else
    return static_cast<int64_t>(getpid());
#endif
}

int64_t parentPid() {
#ifdef _WIN32
    return 0;  // not told: a WinBoard engine's cut games are not found on Windows
#else
    return static_cast<int64_t>(getppid());
#endif
}

}  // namespace

std::string keyOf(const tour::RemoteEngine& engine) {
    const nlohmann::json j = {
        {"machine", engine.machine}, {"program", engine.program}, {"args", engine.args}, {"dir", engine.dir}};
    return sha256Hex(j.dump()).substr(0, 32);
}

std::string hubEnvironment(uint16_t port, const std::string& secret) { return std::to_string(port) + ":" + secret; }

std::optional<std::pair<uint16_t, std::string>> parseHubEnvironment(std::string_view text) {
    const auto colon = text.find(':');
    if (colon == std::string_view::npos || colon + 1 >= text.size()) return std::nullopt;
    unsigned port    = 0;
    const auto first = text.data();
    const auto [end, ec] = std::from_chars(first, first + colon, port);
    if (ec != std::errc() || end != first + colon || port == 0 || port > 65535) return std::nullopt;
    return std::make_pair(static_cast<uint16_t>(port), std::string(text.substr(colon + 1)));
}

std::string helloLine(const Hello& hello) {
    const nlohmann::json j = {{"secret", hello.secret}, {"key", hello.key}, {"pid", hello.pid}, {"parent", hello.parent}};
    return j.dump() + "\n";
}

std::optional<Hello> parseHello(std::string_view line) {
    const auto j = nlohmann::json::parse(line, nullptr, false);
    if (!j.is_object()) return std::nullopt;
    const auto secret = j.find("secret");
    const auto key    = j.find("key");
    const auto pid    = j.find("pid");
    if (secret == j.end() || !secret->is_string() || key == j.end() || !key->is_string()) return std::nullopt;
    Hello hello;
    hello.secret = secret->get<std::string>();
    hello.key    = key->get<std::string>();
    if (pid != j.end() && pid->is_number_integer()) hello.pid = pid->get<int64_t>();
    if (const auto parent = j.find("parent"); parent != j.end() && parent->is_number_integer())
        hello.parent = parent->get<int64_t>();
    return hello;
}

std::string frame(Frame type, std::string_view payload) {
    std::string out;
    out.reserve(HEADER + payload.size());
    out += static_cast<char>(type);
    const auto n = static_cast<uint32_t>(payload.size());
    for (int i = 0; i < 4; ++i) out += static_cast<char>((n >> (8 * i)) & 0xff);
    out.append(payload);
    return out;
}

std::optional<std::pair<Frame, std::string>> FrameReader::next() {
    if (broken_ || buffer_.size() < HEADER) return std::nullopt;
    if (!knownFrame(buffer_[0])) {
        broken_ = true;
        return std::nullopt;
    }
    uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n |= static_cast<uint32_t>(static_cast<unsigned char>(buffer_[1 + i])) << (8 * i);
    if (n > MAX_FRAME) {
        broken_ = true;
        return std::nullopt;
    }
    if (buffer_.size() < HEADER + n) return std::nullopt;
    auto out = std::make_pair(static_cast<Frame>(buffer_[0]), buffer_.substr(HEADER, n));
    buffer_.erase(0, HEADER + n);
    return out;
}

int run(const std::string& key, const std::string& hub, std::function<long(char*, std::size_t)> input,
        const std::function<void(std::string_view)>& output, const std::function<void(std::string_view)>& errors) {
    const auto say = [&](const std::string& text) { errors("fastchess-gui --remote-engine: " + text + "\n"); };
    const auto where = parseHubEnvironment(hub);
    if (!where) {
        say(std::string("no engine hub in ") + ENV +
            ": this program is started by fastchess-gui, for an engine that runs on a machine");
        return 1;
    }
    const auto fd = net::connectTo(LOOPBACK, where->first, CONNECT_TIMEOUT_MS);
    if (!fd) {
        say("the engine hub does not answer on port " + std::to_string(where->first));
        return 1;
    }
    // Shared with the thread that reads the input, which may outlive this call (a read that waits cannot be ended).
    const auto socket = std::make_shared<net::Socket>(*fd);
    if (!sendAll(*socket, helloLine({where->second, key, ownPid(), parentPid()}))) {
        say("the engine hub closed the connection");
        return 1;
    }
    std::thread([socket, input = std::move(input)] {
        std::string buffer(std::size_t(1) << 16, '\0');
        while (true) {
            const auto n = input(buffer.data(), buffer.size());
            if (n <= 0) break;
            if (!sendAll(*socket, std::string_view(buffer.data(), static_cast<std::size_t>(n)))) return;
        }
        socket->shutdownWrite();  // the engine reads the end of its input
    }).detach();

    FrameReader reader;
    std::string chunk;
    while (true) {
        while (auto f = reader.next()) {
            switch (f->first) {
                case Frame::Output:
                    output(f->second);
                    break;
                case Frame::Errors:
                    errors(f->second);
                    break;
                case Frame::Exit: {
                    int code             = 1;
                    const auto& t        = f->second;
                    const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), code);
                    socket->shutdownBoth();
                    return ec == std::errc() && end == t.data() + t.size() && code >= 0 ? code : 1;
                }
                case Frame::Failure:
                case Frame::Cut:
                    say(f->second);
                    socket->shutdownBoth();
                    return 1;
            }
        }
        if (reader.broken()) {
            say("the engine hub sent something that is not a frame");
            socket->shutdownBoth();
            return 1;
        }
        chunk.clear();
        const auto n = socket->receive(chunk, std::size_t(1) << 16);
        if (n < 0) {
            say("the connection to the engine hub broke");
            socket->shutdownBoth();
            return 1;
        }
        if (n == 0) {
            std::vector<net::Wait> waits{{socket->handle(), true, false}};
            net::waitAny(waits, 1000);
            continue;
        }
        reader.feed(chunk);
    }
}

int main(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        std::fprintf(stderr, "usage: fastchess-gui %s KEY (started by fastchess-gui, not by hand)\n", FLAG);
        return 2;
    }
    const char* hub = std::getenv(ENV);
#ifdef _WIN32
    const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    const auto read = [in](char* buffer, std::size_t size) -> long {
        DWORD n = 0;
        if (!ReadFile(in, buffer, static_cast<DWORD>(std::min<std::size_t>(size, 1 << 20)), &n, nullptr)) return 0;
        return static_cast<long>(n);
    };
    const auto writeTo = [](HANDLE h, std::string_view data) {
        while (!data.empty()) {
            DWORD n = 0;
            if (!WriteFile(h, data.data(), static_cast<DWORD>(std::min<std::size_t>(data.size(), 1 << 20)), &n,
                           nullptr) ||
                n == 0)
                return;
            data.remove_prefix(n);
        }
    };
    const int code = run(
        args[0], hub ? hub : "", read, [&](std::string_view d) { writeTo(out, d); },
        [&](std::string_view d) { writeTo(err, d); });
#else
    const auto read = [](char* buffer, std::size_t size) -> long {
        while (true) {
            const auto n = ::read(STDIN_FILENO, buffer, size);
            if (n < 0 && errno == EINTR) continue;
            return n < 0 ? 0 : static_cast<long>(n);
        }
    };
    const auto writeTo = [](int fd, std::string_view data) {
        while (!data.empty()) {
            const auto n = ::write(fd, data.data(), data.size());
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return;
            data.remove_prefix(static_cast<std::size_t>(n));
        }
    };
    const int code = run(
        args[0], hub ? hub : "", read, [&](std::string_view d) { writeTo(STDOUT_FILENO, d); },
        [&](std::string_view d) { writeTo(STDERR_FILENO, d); });
#endif
    // The thread reading the input may wait on it still: the process ends without it.
    std::_Exit(code);
}

}  // namespace fcg::relay
