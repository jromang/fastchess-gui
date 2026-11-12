#include "net.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <fcntl.h>
#    include <ifaddrs.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <sys/select.h>
#    include <sys/socket.h>
#    include <unistd.h>

#    include <cerrno>
#endif

namespace fcg::net {

namespace {

using Clock = std::chrono::steady_clock;

#ifdef _WIN32
using SockLen = int;
void ensureStarted() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}
void closeHandle(Handle fd) { closesocket(static_cast<SOCKET>(fd)); }
constexpr Handle INVALID = static_cast<Handle>(INVALID_SOCKET);
// The engines started while a server runs must not inherit its socket (they would keep its port).
Handle openSocket(int type, int family = AF_INET) {
    const auto fd = static_cast<Handle>(socket(family, type, 0));
    if (fd != INVALID) SetHandleInformation(reinterpret_cast<HANDLE>(fd), HANDLE_FLAG_INHERIT, 0);
    return fd;
}
void setBlocking(Handle fd, bool blocking) {
    u_long mode = blocking ? 0 : 1;
    ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode);
}
// A non-blocking call that would have had to wait (a connect: that is under way).
bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
bool connecting() { return WSAGetLastError() == WSAEWOULDBLOCK; }
void sendTimeout(Handle fd, int timeout_ms) {
    const DWORD ms = static_cast<DWORD>(timeout_ms);
    setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
}
constexpr int SEND_FLAGS = 0;
#else
using SockLen = socklen_t;
void ensureStarted() {}
void closeHandle(Handle fd) { ::close(fd); }
constexpr Handle INVALID = -1;
// The engines started while a server runs must not inherit its socket (they would keep its port).
Handle openSocket(int type, int family = AF_INET) { return socket(family, type | SOCK_CLOEXEC, 0); }
void setBlocking(Handle fd, bool blocking) {
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK);
}
bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
bool connecting() { return errno == EINPROGRESS; }
void sendTimeout(Handle fd, int timeout_ms) {
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}
// A client gone does not kill the program with SIGPIPE.
#    ifdef MSG_NOSIGNAL
constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#    else
constexpr int SEND_FLAGS = 0;
#    endif
#endif

// True when `fd` has something to read (or room to write) within `timeout_ms`; an error counts, for the call that
// follows to tell it (Windows reports a failed connect that way).
bool ready(Handle fd, bool write, int timeout_ms) {
    fd_set set, error;
    FD_ZERO(&set);
    FD_ZERO(&error);
#ifdef _WIN32
    FD_SET(static_cast<SOCKET>(fd), &set);
    FD_SET(static_cast<SOCKET>(fd), &error);
#else
    FD_SET(fd, &set);
    FD_SET(fd, &error);
#endif
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    return select(static_cast<int>(fd) + 1, write ? nullptr : &set, write ? &set : nullptr, &error, &tv) > 0;
}

bool readable(Handle fd, int timeout_ms) { return ready(fd, false, timeout_ms); }

int msLeft(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return static_cast<int>(std::clamp<long long>(left, 0, 1 << 30));
}

// Waits in short steps, until `fd` is ready, the deadline passes, or `running` turns false.
bool readyBefore(Handle fd, bool write, Clock::time_point deadline, const std::atomic_bool* running) {
    while (!running || *running) {
        const int left = msLeft(deadline);
        if (left <= 0) return false;
        if (ready(fd, write, std::min(left, 100))) return true;
    }
    return false;
}

sockaddr_in address(uint32_t ip, uint16_t port) {
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(port);
    a.sin_addr.s_addr = htonl(ip);
    return a;
}

// A port another program uses must be refused, to say so. On Windows SO_REUSEADDR would let us share it (TCP and
// UDP alike), and SO_EXCLUSIVEADDRUSE keeps others off ours. On POSIX, SO_REUSEADDR only lets a TCP listener start
// again while the connections of the last one linger, but it would let two UDP sockets share a port.
void exclusive(Handle fd, bool tcp) {
#ifdef _WIN32
    (void)tcp;
    BOOL yes = TRUE;
    setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes),
               sizeof(yes));
#else
    if (!tcp) return;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
}

}  // namespace

std::string Endpoint::text() const {
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) +
           "." + std::to_string(ip & 255) + ":" + std::to_string(port);
}

Handle UdpSocket::invalid() { return INVALID; }

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::open() const noexcept { return fd_ != INVALID; }

std::string UdpSocket::bind(uint16_t port) {
    ensureStarted();
    close();
    fd_ = openSocket(SOCK_DGRAM);
    if (fd_ == INVALID) return "cannot create a UDP socket";
    exclusive(fd_, false);
    const auto a = address(INADDR_ANY, port);
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0) {
        close();
        return "UDP port " + std::to_string(port) + " is in use";
    }
    return {};
}

void UdpSocket::close() {
    if (fd_ != INVALID) closeHandle(fd_);
    fd_ = INVALID;
}

std::optional<std::string> UdpSocket::receive(Endpoint& from, int timeout_ms) {
    if (fd_ == INVALID || !readable(fd_, timeout_ms)) return std::nullopt;
    char buffer[4096];
    sockaddr_in a{};
    SockLen len  = sizeof(a);
    const auto n = recvfrom(fd_, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&a), &len);
    if (n <= 0) return std::nullopt;
    from.ip   = ntohl(a.sin_addr.s_addr);
    from.port = ntohs(a.sin_port);
    return std::string(buffer, static_cast<std::size_t>(n));
}

void UdpSocket::send(const Endpoint& to, std::string_view data) {
    if (fd_ == INVALID) return;
    const auto a = address(to.ip, to.port);
    sendto(fd_, data.data(), static_cast<int>(data.size()), 0, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
}

// Non-blocking: no call waits longer than the deadlines say.
TcpConnection::TcpConnection(Handle fd, const std::atomic_bool* running) : fd_(fd), running_(running) {
    setBlocking(fd_, false);
}

TcpConnection::~TcpConnection() { closeHandle(fd_); }

std::string TcpConnection::readUntil(std::string_view end, std::size_t max, int timeout_ms) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string out;
    char buffer[2048];
    while (out.size() < max && out.find(end) == std::string::npos) {
        if (!readyBefore(fd_, false, deadline, running_)) break;
        const auto n = recv(fd_, buffer, static_cast<int>(std::min(sizeof(buffer), max - out.size())), 0);
        if (n < 0 && wouldBlock()) continue;
        if (n <= 0) break;
        out.append(buffer, static_cast<std::size_t>(n));
    }
    return out;
}

bool TcpConnection::sendAll(std::string_view data, int timeout_ms) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!data.empty()) {
        const auto size = static_cast<int>(std::min<std::size_t>(data.size(), 1 << 20));
        const auto n    = ::send(fd_, data.data(), size, SEND_FLAGS);
        if (n > 0) {
            data.remove_prefix(static_cast<std::size_t>(n));
        } else if (n == 0 || !wouldBlock() || !readyBefore(fd_, true, deadline, running_)) {
            return false;
        }
    }
    return true;
}

TcpListener::~TcpListener() { close(); }

std::string TcpListener::listen(uint16_t port) {
    ensureStarted();
    close();
    fd_ = openSocket(SOCK_STREAM);
    if (fd_ == INVALID) return "cannot create a TCP socket";
    open_ = true;
    exclusive(fd_, true);
    const auto a = address(INADDR_ANY, port);
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0 || ::listen(fd_, 16) != 0) {
        close();
        return "TCP port " + std::to_string(port) + " is in use";
    }
    return {};
}

void TcpListener::close() {
    if (open_) closeHandle(fd_);
    open_ = false;
}

std::optional<Handle> TcpListener::accept(int timeout_ms) {
    if (!open_ || !readable(fd_, timeout_ms)) return std::nullopt;
#ifdef _WIN32
    const auto fd = static_cast<Handle>(::accept(fd_, nullptr, nullptr));
    if (fd == INVALID) return std::nullopt;
    SetHandleInformation(reinterpret_cast<HANDLE>(fd), HANDLE_FLAG_INHERIT, 0);
#else
    const auto fd = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd == INVALID) return std::nullopt;
#endif
    return fd;
}

std::optional<Handle> connectTo(uint32_t ip, uint16_t port) {
    ensureStarted();
    const auto fd = openSocket(SOCK_STREAM);
    if (fd == INVALID) return std::nullopt;
    const auto a = address(ip, port);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0) {
        closeHandle(fd);
        return std::nullopt;
    }
    return fd;
}

std::optional<Handle> connectHost(const std::string& host, const std::string& port, int timeout_ms,
                                  std::string& error) {
    ensureStarted();
    addrinfo hints{}, *list = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &list) != 0 || !list) {
        error = "cannot find " + host;
        return std::nullopt;
    }
    // Each address in turn (IPv6, IPv4), within the one timeout.
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    std::optional<Handle> out;
    error = "cannot connect to " + host;
    for (auto* p = list; p && !out; p = p->ai_next) {
        const auto fd = openSocket(SOCK_STREAM, p->ai_family);
        if (fd == INVALID) continue;
        setBlocking(fd, false);
        bool ok = ::connect(fd, p->ai_addr, static_cast<SockLen>(p->ai_addrlen)) == 0;
        if (!ok && connecting()) {
            if (ready(fd, true, msLeft(deadline))) {
                int code    = 0;
                SockLen len = sizeof(code);
                ok = getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&code), &len) == 0 && code == 0;
            } else {
                error = "no answer from " + host + " in time";
            }
        }
        if (!ok) {
            closeHandle(fd);
            continue;
        }
        setBlocking(fd, true);
        sendTimeout(fd, timeout_ms);
        out = fd;
    }
    freeaddrinfo(list);
    return out;
}

std::vector<std::string> localAddresses() {
    std::vector<std::string> out;
#ifdef _WIN32
    ensureStarted();
    char host[256];
    if (gethostname(host, sizeof(host)) != 0) return out;
    addrinfo hints{}, *list = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, nullptr, &hints, &list) != 0) return out;
    for (auto* p = list; p; p = p->ai_next) {
        const auto ip = ntohl(reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr.s_addr);
        if ((ip >> 24) != 127) out.push_back(Endpoint{ip, 0}.text().substr(0, Endpoint{ip, 0}.text().rfind(':')));
    }
    freeaddrinfo(list);
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (auto* p = list; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        const auto ip = ntohl(reinterpret_cast<sockaddr_in*>(p->ifa_addr)->sin_addr.s_addr);
        if ((ip >> 24) == 127) continue;
        const auto text = Endpoint{ip, 0}.text();
        out.push_back(text.substr(0, text.rfind(':')));
    }
    freeifaddrs(list);
#endif
    return out;
}

}  // namespace fcg::net
