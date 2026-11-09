#include "net.hpp"

#include <cstring>
#include <mutex>

#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <ifaddrs.h>
#    include <netinet/in.h>
#    include <sys/select.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

namespace fcg::net {

namespace {

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
#else
using SockLen = socklen_t;
void ensureStarted() {}
void closeHandle(Handle fd) { ::close(fd); }
constexpr Handle INVALID = -1;
#endif

// True when `fd` has something to read within `timeout_ms`.
bool readable(Handle fd, int timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
#ifdef _WIN32
    FD_SET(static_cast<SOCKET>(fd), &set);
#else
    FD_SET(fd, &set);
#endif
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    return select(static_cast<int>(fd) + 1, &set, nullptr, nullptr, &tv) > 0;
}

sockaddr_in address(uint32_t ip, uint16_t port) {
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(port);
    a.sin_addr.s_addr = htonl(ip);
    return a;
}

void reuse(Handle fd) {
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
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
    fd_ = static_cast<Handle>(socket(AF_INET, SOCK_DGRAM, 0));
    if (fd_ == INVALID) return "cannot create a UDP socket";
    reuse(fd_);
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

TcpConnection::~TcpConnection() { closeHandle(fd_); }

std::string TcpConnection::readUntil(std::string_view end, std::size_t max, int timeout_ms) {
    std::string out;
    char buffer[2048];
    while (out.size() < max && out.find(end) == std::string::npos) {
        if (!readable(fd_, timeout_ms)) break;
        const auto n = recv(fd_, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        out.append(buffer, static_cast<std::size_t>(n));
    }
    return out;
}

void TcpConnection::sendAll(std::string_view data) {
    while (!data.empty()) {
        const auto n = ::send(fd_, data.data(), static_cast<int>(data.size()), 0);
        if (n <= 0) return;
        data.remove_prefix(static_cast<std::size_t>(n));
    }
}

TcpListener::~TcpListener() { close(); }

std::string TcpListener::listen(uint16_t port) {
    ensureStarted();
    close();
    fd_ = static_cast<Handle>(socket(AF_INET, SOCK_STREAM, 0));
    if (fd_ == INVALID) return "cannot create a TCP socket";
    open_ = true;
    reuse(fd_);
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
    const auto fd = static_cast<Handle>(::accept(fd_, nullptr, nullptr));
    if (fd == INVALID) return std::nullopt;
    return fd;
}

std::optional<Handle> connectTo(uint32_t ip, uint16_t port) {
    ensureStarted();
    const auto fd = static_cast<Handle>(socket(AF_INET, SOCK_STREAM, 0));
    if (fd == INVALID) return std::nullopt;
    const auto a = address(ip, port);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0) {
        closeHandle(fd);
        return std::nullopt;
    }
    return fd;
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
