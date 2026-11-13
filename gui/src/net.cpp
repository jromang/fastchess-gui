#include "net.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <fcntl.h>
#    include <ifaddrs.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <poll.h>
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
#ifdef _WIN32
    // select on Windows: its fd_set is a list of handles, of any value, and WSAPoll misses a failed connect on the
    // Windows 10 versions before 2004.
    fd_set set, error;
    FD_ZERO(&set);
    FD_ZERO(&error);
    FD_SET(static_cast<SOCKET>(fd), &set);
    FD_SET(static_cast<SOCKET>(fd), &error);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    return select(0, write ? nullptr : &set, write ? &set : nullptr, &error, &tv) > 0;
#else
    // poll on POSIX: select's FD_SET writes past its set for a descriptor of FD_SETSIZE (1024) or more, which a
    // program with many engines, logs and connections open can have.
    pollfd p{fd, static_cast<short>(write ? POLLOUT : POLLIN), 0};
    return poll(&p, 1, timeout_ms) > 0;
#endif
}

bool readable(Handle fd, int timeout_ms) { return ready(fd, false, timeout_ms); }

int msLeft(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return static_cast<int>(std::clamp<long long>(left, 0, 1 << 30));
}

// Waits in short steps, until `fd` is ready, the deadline passes, or `running` turns false (`stopping` true).
bool readyBefore(Handle fd, bool write, Clock::time_point deadline, const std::atomic_bool* running,
                 const std::atomic_bool* stopping = nullptr) {
    while ((!running || *running) && (!stopping || !*stopping)) {
        const int left = msLeft(deadline);
        if (left <= 0) return false;
        if (ready(fd, write, std::min(left, 100))) return true;
    }
    return false;
}

// A non-blocking connect under way: true once it succeeded, false when it failed, or did not end in time.
bool connected(Handle fd, Clock::time_point deadline, const std::atomic_bool* stopping) {
    if (!readyBefore(fd, true, deadline, nullptr, stopping)) return false;
    int code    = 0;
    SockLen len = sizeof(code);
    return getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&code), &len) == 0 && code == 0;
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
    // Room for a burst of connections (many viewers, or a tunnel opening one per request): those the queue does not
    // take wait a second or more for the system to try them again.
    constexpr int BACKLOG = 128;
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0 || ::listen(fd_, BACKLOG) != 0) {
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

Socket::Socket(Handle fd) : fd_(fd) { setBlocking(fd_, false); }

Socket::~Socket() { closeHandle(fd_); }

long Socket::receive(std::string& out, std::size_t max) {
    char buffer[16384];
    const auto n = recv(fd_, buffer, static_cast<int>(std::min(sizeof(buffer), max)), 0);
    if (n > 0) {
        out.append(buffer, static_cast<std::size_t>(n));
        return static_cast<long>(n);
    }
    if (n < 0 && wouldBlock()) return 0;
    return -1;
}

long Socket::send(std::string_view data) {
    if (data.empty()) return 0;
    const auto size = static_cast<int>(std::min<std::size_t>(data.size(), 1 << 20));
    const auto n    = ::send(fd_, data.data(), size, SEND_FLAGS);
    if (n > 0) return static_cast<long>(n);
    if (n < 0 && wouldBlock()) return 0;
    return -1;
}

void Socket::shutdownWrite() {
#ifdef _WIN32
    shutdown(static_cast<SOCKET>(fd_), SD_SEND);
#else
    shutdown(fd_, SHUT_WR);
#endif
}

// poll rather than select: no limit on the number of sockets, nor on their values. Only the sockets waited for
// are polled: poll reports a hang-up or an error even for one asked nothing, at once, again and again, and the
// caller, which asked nothing of it, would wait no more.
void waitAny(std::vector<Wait>& waits, int timeout_ms) {
    std::vector<pollfd> fds;
    std::vector<std::size_t> of;  // the wait of each
    for (std::size_t i = 0; i < waits.size(); ++i) {
        waits[i].can_read = waits[i].can_write = false;
        if (!waits[i].read && !waits[i].write) continue;
        pollfd p{};
#ifdef _WIN32
        p.fd = static_cast<SOCKET>(waits[i].fd);
#else
        p.fd = waits[i].fd;
#endif
        p.events = static_cast<short>((waits[i].read ? POLLIN : 0) | (waits[i].write ? POLLOUT : 0));
        fds.push_back(p);
        of.push_back(i);
    }
    if (fds.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(timeout_ms, 0)));
        return;
    }
#ifdef _WIN32
    const int n = WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), timeout_ms);
#else
    const int n = poll(fds.data(), static_cast<nfds_t>(fds.size()), timeout_ms);
#endif
    if (n <= 0) return;
    for (std::size_t k = 0; k < fds.size(); ++k) {
        auto& wait       = waits[of[k]];
        const auto r     = fds[k].revents;
        const bool ended = (r & (POLLERR | POLLHUP | POLLNVAL)) != 0;
        wait.can_read    = wait.read && ((r & POLLIN) != 0 || ended);
        wait.can_write   = wait.write && ((r & POLLOUT) != 0 || ended);
    }
}

std::optional<Handle> connectTo(uint32_t ip, uint16_t port, int timeout_ms) {
    ensureStarted();
    const auto fd = openSocket(SOCK_STREAM);
    if (fd == INVALID) return std::nullopt;
    const auto a = address(ip, port);
    bool ok      = false;
    if (timeout_ms < 0) {
        ok = ::connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == 0;
    } else {
        // Bounded: on Windows, a connection refused, even on this machine, is told after two seconds or so.
        setBlocking(fd, false);
        ok = ::connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == 0;
        if (!ok && connecting()) ok = connected(fd, Clock::now() + std::chrono::milliseconds(timeout_ms), nullptr);
        setBlocking(fd, true);
    }
    if (!ok) {
        closeHandle(fd);
        return std::nullopt;
    }
    return fd;
}

std::optional<Handle> connectHost(const std::string& host, const std::string& port, int timeout_ms, std::string& error,
                                  const std::atomic_bool* stopping) {
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
            ok = connected(fd, deadline, stopping);
            if (!ok && Clock::now() >= deadline) error = "no answer from " + host + " in time";
        }
        if (stopping && *stopping) {
            closeHandle(fd);
            error = "stopped";
            break;
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
