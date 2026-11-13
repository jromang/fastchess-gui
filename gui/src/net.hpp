#pragma once

// The few socket calls the broadcast servers need, the same on POSIX and Windows: a UDP socket (TLCS) and a TCP
// listener (the web page). Blocking calls take a timeout, so server threads can notice they must stop.

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fcg::net {

#ifdef _WIN32
using Handle = std::uintptr_t;
#else
using Handle = int;
#endif

struct Endpoint {
    uint32_t ip   = 0;  // host byte order
    uint16_t port = 0;

    bool operator==(const Endpoint&) const = default;
    bool operator<(const Endpoint& o) const { return ip != o.ip ? ip < o.ip : port < o.port; }
    [[nodiscard]] std::string text() const;
};

class UdpSocket {
   public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&)            = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Binds every interface on `port`. Returns an error message, empty on success.
    std::string bind(uint16_t port);
    void close();
    [[nodiscard]] bool open() const noexcept;

    // Waits up to `timeout_ms` for a datagram; nullopt on timeout or error.
    std::optional<std::string> receive(Endpoint& from, int timeout_ms);
    void send(const Endpoint& to, std::string_view data);

   private:
    Handle fd_ = invalid();
    static Handle invalid();
};

class TcpConnection {
   public:
    // With `running`, the waits end as soon as it turns false: a server that stops does not wait for a slow client.
    explicit TcpConnection(Handle fd, const std::atomic_bool* running = nullptr);
    ~TcpConnection();
    TcpConnection(const TcpConnection&)            = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    // Reads until `end` is seen, `max` bytes, or `timeout_ms` in all.
    std::string readUntil(std::string_view end, std::size_t max, int timeout_ms);
    // False when the other side does not take it all within `timeout_ms`.
    bool sendAll(std::string_view data, int timeout_ms = 10000);

   private:
    Handle fd_;
    const std::atomic_bool* running_;
};

class TcpListener {
   public:
    TcpListener() = default;
    ~TcpListener();
    TcpListener(const TcpListener&)            = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    // Listens on every interface. Returns an error message, empty on success.
    std::string listen(uint16_t port);
    void close();

    // Waits up to `timeout_ms` for a connection.
    std::optional<Handle> accept(int timeout_ms);

   private:
    Handle fd_ = 0;
    bool open_ = false;
};

// A connected TCP socket that never waits, for a thread that serves several at once (the SSH tunnel): waitAny()
// tells which can be read or written. Closed with the object.
class Socket {
   public:
    explicit Socket(Handle fd);
    ~Socket();
    Socket(const Socket&)            = delete;
    Socket& operator=(const Socket&) = delete;

    [[nodiscard]] Handle handle() const noexcept { return fd_; }
    // Appends what there is to read, `max` bytes at most: how many, 0 when nothing came yet, -1 once the other side
    // closed the connection or it failed.
    long receive(std::string& out, std::size_t max);
    // Sends what can be sent at once: how many bytes (0 when it would have to wait), -1 on an error.
    long send(std::string_view data);
    // No more data from this side: the other one reads the end of the stream.
    void shutdownWrite();

   private:
    Handle fd_;
};

// A socket to wait for, and what it is ready for once waitAny() returns.
struct Wait {
    Handle fd   = 0;
    bool read   = false;
    bool write  = false;
    bool can_read  = false;  // or the connection ended: the read tells
    bool can_write = false;
};
// Waits up to `timeout_ms` until one of `waits` can be read or written as asked.
void waitAny(std::vector<Wait>& waits, int timeout_ms);

// Connects to a TCP server (tests, mostly). nullopt when it cannot.
[[nodiscard]] std::optional<Handle> connectTo(uint32_t ip, uint16_t port);

// Connects to `host` (a name or an IPv4 address), giving up after `timeout_ms`. A blocking socket, which sends
// give up on after `timeout_ms` too; nullopt and `error` when it cannot.
[[nodiscard]] std::optional<Handle> connectHost(const std::string& host, const std::string& port, int timeout_ms,
                                                std::string& error);

// The machine's IPv4 addresses other than loopback, to tell people where to connect.
[[nodiscard]] std::vector<std::string> localAddresses();

}  // namespace fcg::net
