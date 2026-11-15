#include "local_socket.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>

#include <json.hpp>

#include <tour/files.hpp>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <poll.h>
#    include <sys/socket.h>
#    include <sys/stat.h>
#    include <sys/un.h>
#    include <unistd.h>
#endif

namespace fcg::local {

namespace fs = std::filesystem;

namespace {

// A short name for a folder: its path's CRC, in hexadecimal.
std::string folderKey(const fs::path& dir) {
    const auto text = fs::absolute(dir).lexically_normal().string();
    uint32_t crc    = 0xffffffffu;
    for (const unsigned char c : text) {
        crc ^= c;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%08x", ~crc);
    return buffer;
}

#ifdef _WIN32

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// A named pipe's end, read and written with overlapped I/O, so that close() ends a read waiting on another thread.
class PipeConnection final : public Link {
   public:
    explicit PipeConnection(HANDLE pipe) : pipe_(pipe) {}
    ~PipeConnection() override {
        close();
        if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
    }
    long read(char* buffer, std::size_t size) override {
        if (closed_) return 0;
        OVERLAPPED o{};
        o.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD n  = 0;
        BOOL ok  = ReadFile(pipe_, buffer, static_cast<DWORD>(size), &n, &o);
        if (!ok && GetLastError() == ERROR_IO_PENDING) ok = GetOverlappedResult(pipe_, &o, &n, TRUE);
        CloseHandle(o.hEvent);
        if (!ok) return GetLastError() == ERROR_BROKEN_PIPE || closed_ ? 0 : -1;
        return static_cast<long>(n);
    }
    bool write(const std::string& data) override {
        if (closed_) return false;
        OVERLAPPED o{};
        o.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD n  = 0;
        BOOL ok  = WriteFile(pipe_, data.data(), static_cast<DWORD>(data.size()), &n, &o);
        if (!ok && GetLastError() == ERROR_IO_PENDING) ok = GetOverlappedResult(pipe_, &o, &n, TRUE);
        CloseHandle(o.hEvent);
        return ok && n == data.size();
    }
    void close() override {
        if (closed_.exchange(true)) return;
        CancelIoEx(pipe_, nullptr);
        DisconnectNamedPipe(pipe_);
    }
    // DisconnectNamedPipe() drops what the client has not read yet: it waits for it to be read first.
    void finish() override {
        if (!closed_) FlushFileBuffers(pipe_);
        close();
    }

   private:
    HANDLE pipe_;
    std::atomic_bool closed_{false};
};

#else

// A socket no program started meanwhile inherits, and that never raises SIGPIPE (macOS has neither SOCK_CLOEXEC nor
// MSG_NOSIGNAL).
int closingSocket(int fd) {
    if (fd < 0) return fd;
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
#    ifdef __APPLE__
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#    endif
    return fd;
}

#    ifdef MSG_NOSIGNAL
constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#    else
constexpr int SEND_FLAGS = 0;
#    endif

class SocketConnection final : public Link {
   public:
    explicit SocketConnection(int fd) : fd_(fd) {}
    ~SocketConnection() override {
        close();
        ::close(fd_);
    }
    long read(char* buffer, std::size_t size) override {
        for (;;) {
            const auto n = ::recv(fd_, buffer, size, 0);
            if (n < 0 && errno == EINTR) continue;
            return static_cast<long>(n);
        }
    }
    bool write(const std::string& data) override {
        const char* p    = data.data();
        std::size_t left = data.size();
        while (left > 0) {
            const auto n = ::send(fd_, p, left, SEND_FLAGS);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            p += n;
            left -= static_cast<std::size_t>(n);
        }
        return true;
    }
    void close() override { ::shutdown(fd_, SHUT_RDWR); }
    // What it sent is read to its end before the socket goes: closed with data still unread, it would be reset, and
    // the other side could lose the last line.
    void finish() override {
        ::shutdown(fd_, SHUT_WR);
        char buffer[512];
        for (;;) {
            const auto n = ::recv(fd_, buffer, sizeof buffer, 0);
            if (n > 0 || (n < 0 && errno == EINTR)) continue;
            break;
        }
        close();
    }

   private:
    int fd_;
};

bool socketAddress(const std::string& endpoint, sockaddr_un& addr) {
    std::memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (endpoint.size() >= sizeof addr.sun_path) return false;
    std::memcpy(addr.sun_path, endpoint.data(), endpoint.size());
    return true;
}

#endif

}  // namespace

std::string endpointFor(const fs::path& dir) {
#ifdef _WIN32
    return "\\\\.\\pipe\\fastchess-gui-" + folderKey(dir);
#else
    const auto in_folder = (fs::absolute(dir) / "control.sock").string();
    if (in_folder.size() < 100) return in_folder;
    // Too long for a Unix socket's address: in the user's runtime folder, named after the tournament's.
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    fs::path base       = runtime && *runtime ? fs::path(runtime) : fs::temp_directory_path();
    base /= "fastchess-gui";
    std::error_code ec;
    fs::create_directories(base, ec);
    ::chmod(base.c_str(), 0700);
    return (base / (folderKey(dir) + ".sock")).string();
#endif
}

fs::path controlFilePath(const fs::path& dir) { return dir / "control.json"; }

bool writeControlFile(const fs::path& dir, const ControlFile& file) {
    const nlohmann::json j = {{"endpoint", file.endpoint}, {"pid", file.pid}, {"version", file.version}};
    try {
        tour::replaceFile(controlFilePath(dir), j.dump(2) + "\n");
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<ControlFile> readControlFile(const fs::path& dir) {
    std::ifstream in(controlFilePath(dir), std::ios::binary);
    if (!in) return std::nullopt;
    try {
        const auto j = nlohmann::json::parse(in);
        ControlFile f;
        f.endpoint = j.value("endpoint", std::string{});
        f.pid      = j.value("pid", int64_t{0});
        f.version  = j.value("version", std::string{});
        if (f.endpoint.empty()) return std::nullopt;
        return f;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void removeControlFile(const fs::path& dir) {
    std::error_code ec;
    fs::remove(controlFilePath(dir), ec);
}

Server::~Server() { close(); }

#ifdef _WIN32

bool Server::listen(const std::string& endpoint, std::string& error) {
    // The first instance with FILE_FLAG_FIRST_PIPE_INSTANCE: another process listening there already is refused.
    const auto name = wide(endpoint);
    HANDLE pipe =
        CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                         PIPE_UNLIMITED_INSTANCES, 1 << 16, 1 << 16, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        error = "cannot listen on " + endpoint + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pipe);  // the instances are made by accept()
    endpoint_ = endpoint;
    stop_     = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return true;
}

std::unique_ptr<Link> Server::accept() {
    if (!stop_) return nullptr;
    const auto name = wide(endpoint_);
    HANDLE pipe     = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       PIPE_UNLIMITED_INSTANCES, 1 << 16, 1 << 16, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return nullptr;
    OVERLAPPED o{};
    o.hEvent       = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool connected = ConnectNamedPipe(pipe, &o) != 0;
    if (!connected) {
        const DWORD e = GetLastError();
        if (e == ERROR_PIPE_CONNECTED) {
            connected = true;
        } else if (e == ERROR_IO_PENDING) {
            HANDLE both[2] = {o.hEvent, static_cast<HANDLE>(stop_)};
            if (WaitForMultipleObjects(2, both, FALSE, INFINITE) == WAIT_OBJECT_0) {
                DWORD n   = 0;
                connected = GetOverlappedResult(pipe, &o, &n, FALSE) != 0;
            } else {
                CancelIoEx(pipe, &o);
            }
        }
    }
    CloseHandle(o.hEvent);
    if (!connected) {
        CloseHandle(pipe);
        return nullptr;
    }
    return std::make_unique<PipeConnection>(pipe);
}

void Server::close() {
    if (stop_) SetEvent(static_cast<HANDLE>(stop_));
}

std::unique_ptr<Link> connect(const std::string& endpoint, std::string& error) {
    const auto name = wide(endpoint);
    for (int attempt = 0; attempt < 20; ++attempt) {
        HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) return std::make_unique<PipeConnection>(pipe);
        if (GetLastError() != ERROR_PIPE_BUSY) break;
        WaitNamedPipeW(name.c_str(), 100);
    }
    error = "nothing listens on " + endpoint;
    return nullptr;
}

#else

bool Server::listen(const std::string& endpoint, std::string& error) {
    sockaddr_un addr;
    if (!socketAddress(endpoint, addr)) {
        error = "the socket's path is too long: " + endpoint;
        return false;
    }
    // A socket file left by a process that ended without removing it: nothing answers there, it is replaced.
    std::string ignored;
    if (auto other = connect(endpoint, ignored)) {
        error = "another process listens on " + endpoint;
        return false;
    }
    ::unlink(endpoint.c_str());
    const int fd = closingSocket(::socket(AF_UNIX, SOCK_STREAM, 0));
    if (fd < 0) {
        error = std::string("cannot make a socket: ") + std::strerror(errno);
        return false;
    }
    // Only its owner may connect: the socket's permissions, set before anyone can.
    const auto old_mask = ::umask(0077);
    const bool bound    = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0;
    ::umask(old_mask);
    if (!bound || ::listen(fd, 4) != 0) {
        error = "cannot listen on " + endpoint + ": " + std::strerror(errno);
        ::close(fd);
        return false;
    }
    endpoint_ = endpoint;
    fd_       = fd;
    return true;
}

std::unique_ptr<Link> Server::accept() {
    for (;;) {
        const int listening = fd_;
        if (listening < 0) return nullptr;
        // A poll with a timeout, so that close() ends the wait.
        pollfd p{listening, POLLIN, 0};
        const int ready = ::poll(&p, 1, 200);
        if (ready < 0 && errno != EINTR) return nullptr;
        if (ready <= 0) continue;
        const int client = closingSocket(::accept(listening, nullptr, nullptr));
        if (client >= 0) return std::make_unique<SocketConnection>(client);
        if (errno != EINTR && errno != EAGAIN && errno != ECONNABORTED) return nullptr;
    }
}

void Server::close() {
    const int fd = fd_.exchange(-1);
    if (fd < 0) return;
    ::close(fd);
    ::unlink(endpoint_.c_str());
}

std::unique_ptr<Link> connect(const std::string& endpoint, std::string& error) {
    sockaddr_un addr;
    if (!socketAddress(endpoint, addr)) {
        error = "the socket's path is too long: " + endpoint;
        return nullptr;
    }
    const int fd = closingSocket(::socket(AF_UNIX, SOCK_STREAM, 0));
    if (fd < 0) {
        error = std::string("cannot make a socket: ") + std::strerror(errno);
        return nullptr;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        error = "nothing listens on " + endpoint;
        ::close(fd);
        return nullptr;
    }
    return std::make_unique<SocketConnection>(fd);
}

#endif

}  // namespace fcg::local
