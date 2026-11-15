#pragma once

// The local socket a tournament process listens on (--control socket), so that it plays on without the window and the
// window connects to it again: a Unix socket (Linux, macOS), a named pipe (Windows). control.json in the tournament's
// folder says where it is and which process listens.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace fcg {

// Both ends of a connection: a pipe pair or a socket.
class Link {
   public:
    virtual ~Link() = default;
    // Waits for something: the bytes read, 0 at the end, -1 on an error.
    virtual long read(char* buffer, std::size_t size) = 0;
    virtual bool write(const std::string& data)       = 0;
    // Ends the connection: a read() waiting returns.
    virtual void close() = 0;
};

namespace local {

// Where the tournament in `dir` listens: control.sock in its folder, or in the user's runtime folder when that path is
// too long for a Unix socket; a named pipe on Windows, named after the folder.
[[nodiscard]] std::string endpointFor(const std::filesystem::path& dir);

// control.json: where the process playing the tournament listens, and which it is.
struct ControlFile {
    std::string endpoint;
    int64_t pid = 0;
    std::string version;
};
[[nodiscard]] std::filesystem::path controlFilePath(const std::filesystem::path& dir);
bool writeControlFile(const std::filesystem::path& dir, const ControlFile& file);
[[nodiscard]] std::optional<ControlFile> readControlFile(const std::filesystem::path& dir);
void removeControlFile(const std::filesystem::path& dir);

// The tournament process's end: it listens, and takes the connections one after the other.
class Server {
   public:
    Server() = default;
    ~Server();
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    // False with `error` when it cannot listen there (another process does).
    bool listen(const std::string& endpoint, std::string& error);
    // Waits for the next connection; nullptr once closed.
    std::unique_ptr<Link> accept();
    // Stops listening: an accept() waiting returns nullptr.
    void close();

   private:
    std::string endpoint_;
#ifdef _WIN32
    void* stop_ = nullptr;  // an event set by close()
#else
    std::atomic<int> fd_{-1};
#endif
};

// The window's end (and --attach's): nullptr with `error` when nothing listens there.
[[nodiscard]] std::unique_ptr<Link> connect(const std::string& endpoint, std::string& error);

}  // namespace local

}  // namespace fcg
