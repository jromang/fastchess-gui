#pragma once

// An engine of the library that runs on a remote machine, as fastchess and the window start it:
// `fastchess-gui --remote-engine <key>`, a relay between its standard input and output and the engine hub of the
// process that started it (engine_hub.hpp). The hub is found through the environment (FCG_ENGINE_HUB: its port on the
// loopback and a secret, which only this user's programs can read), the engine by its key among those the hub may
// start. The hub starts it on its machine, and sends back in frames its output, its error output and how it ended.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tour/model.hpp>

namespace fcg::relay {

inline constexpr const char* FLAG = "--remote-engine";
inline constexpr const char* ENV  = "FCG_ENGINE_HUB";

// The key of an engine on a machine: the same engine, the same key, in every process (a tournament's spec keeps the
// engine, the relay's command line only its key).
[[nodiscard]] std::string keyOf(const tour::RemoteEngine& engine);

// FCG_ENGINE_HUB: "port:secret", and back.
[[nodiscard]] std::string hubEnvironment(uint16_t port, const std::string& secret);
[[nodiscard]] std::optional<std::pair<uint16_t, std::string>> parseHubEnvironment(std::string_view text);

// The line a relay sends first: the hub's secret, the engine's key, and its own process id (the hub says which relays
// a broken connection ended).
struct Hello {
    std::string secret;
    std::string key;
    int64_t pid    = 0;
    int64_t parent = 0;  // the process that started it (the WinBoard adapter, for a WinBoard engine); 0: not told
};
[[nodiscard]] std::string helloLine(const Hello& hello);
[[nodiscard]] std::optional<Hello> parseHello(std::string_view line);

// What the hub sends back: frames of a type, a length on 4 bytes (little-endian) and the bytes.
enum class Frame : char {
    Output  = 'o',  // the engine's output
    Errors  = 'e',  // its error output
    Exit    = 'x',  // it ended: its exit code, in decimal
    Failure = 'f',  // it could not start, or was refused: why
    Cut     = 'c',  // the connection to its machine broke: why
};
[[nodiscard]] std::string frame(Frame type, std::string_view payload);

class FrameReader {
   public:
    void feed(std::string_view data) { buffer_.append(data); }
    // The next whole frame; nullopt until one is there, and once the stream is malformed (broken()).
    std::optional<std::pair<Frame, std::string>> next();
    [[nodiscard]] bool broken() const noexcept { return broken_; }

   private:
    std::string buffer_;
    bool broken_ = false;
};

// The relay: connects to the hub `hub` names (FCG_ENGINE_HUB's value), asks for the engine `key`, then passes what
// `input` reads (0 at its end) to the engine, and the hub's frames to `output` and `errors` (the engine's error output,
// and the relay's messages), until the engine ends. Its exit code; 1 when it could not start or its machine was cut
// off. `input` may still be reading when it returns: it is then left to end on its own.
int run(const std::string& key, const std::string& hub, std::function<long(char*, std::size_t)> input,
        const std::function<void(std::string_view)>& output, const std::function<void(std::string_view)>& errors);
// `fastchess-gui --remote-engine <key>`, on the standard input, output and error output.
int main(const std::vector<std::string>& args);

}  // namespace fcg::relay
