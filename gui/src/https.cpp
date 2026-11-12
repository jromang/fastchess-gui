#include "https.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <optional>

#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <fmt/include/fmt/format.h>

#include "net.hpp"
#include "tls_roots.hpp"

namespace fcg {

namespace {

struct Url {
    bool tls = true;
    std::string host;
    std::string port;
    std::string path;
};

std::optional<Url> parseUrl(std::string_view url) {
    Url u;
    if (url.rfind("https://", 0) == 0) {
        url.remove_prefix(8);
    } else if (url.rfind("http://", 0) == 0) {
        u.tls = false;
        url.remove_prefix(7);
    } else {
        return std::nullopt;
    }
    const auto slash = url.find('/');
    const auto host  = url.substr(0, slash);
    u.path           = slash == std::string_view::npos ? "/" : std::string(url.substr(slash));
    const auto colon = host.rfind(':');
    u.host           = std::string(host.substr(0, colon));
    u.port           = colon == std::string_view::npos ? (u.tls ? "443" : "80") : std::string(host.substr(colon + 1));
    if (u.host.empty()) return std::nullopt;
    return u;
}

// A server that answers at all connects in far less.
constexpr int CONNECT_TIMEOUT_MS = 10000;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string base64(std::string_view data) {
    static constexpr char DIGITS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(static_cast<unsigned char>(data[i])) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(static_cast<unsigned char>(data[i + 1])) << 8;
        if (i + 2 < data.size()) n |= static_cast<unsigned char>(data[i + 2]);
        out += DIGITS[(n >> 18) & 63];
        out += DIGITS[(n >> 12) & 63];
        out += i + 1 < data.size() ? DIGITS[(n >> 6) & 63] : '=';
        out += i + 2 < data.size() ? DIGITS[n & 63] : '=';
    }
    return out;
}

// "p%40ss" is "p@ss": a user or a password in a URL is percent-encoded.
std::string percentDecode(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += static_cast<char>(std::stoi(std::string(text.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

// http://[user:password@]host[:port][/], or host[:port] alone. The messages never repeat the user nor the password.
Proxy parseProxy(std::string_view text, const char* variable) {
    Proxy proxy;
    proxy.variable = variable;
    if (const auto scheme = text.find("://"); scheme != std::string_view::npos) {
        if (lower(std::string(text.substr(0, scheme))) != "http") {
            proxy.error =
                fmt::format("{}: only http:// proxies can be used, not {}://", variable, text.substr(0, scheme));
            return proxy;
        }
        text.remove_prefix(scheme + 3);
    }
    text = text.substr(0, text.find_first_of("/?#"));
    if (const auto at = text.rfind('@'); at != std::string_view::npos) {
        const auto user     = text.substr(0, at);
        const auto colon    = user.find(':');
        const auto password = colon == std::string_view::npos ? std::string{} : percentDecode(user.substr(colon + 1));
        proxy.authorization = "Basic " + base64(percentDecode(user.substr(0, colon)) + ":" + password);
        text.remove_prefix(at + 1);
    }
    std::string_view port;
    if (text.rfind('[', 0) == 0 && text.find(']') != std::string_view::npos) {  // [::1]:3128
        const auto close = text.find(']');
        proxy.host       = std::string(text.substr(1, close - 1));
        if (text.substr(close + 1).rfind(':', 0) == 0) port = text.substr(close + 2);
    } else {
        const auto colon = text.rfind(':');
        proxy.host       = std::string(text.substr(0, colon));
        if (colon != std::string_view::npos) port = text.substr(colon + 1);
    }
    proxy.port        = port.empty() ? "1080" : std::string(port);  // curl's default
    const bool digits = proxy.port.size() <= 5 && std::all_of(proxy.port.begin(), proxy.port.end(),
                                                              [](unsigned char c) { return std::isdigit(c) != 0; });
    if (proxy.host.empty() || !digits || std::stoi(proxy.port) < 1 || std::stoi(proxy.port) > 65535) {
        proxy.error = fmt::format("{}: not a proxy's address", variable);
    }
    return proxy;
}

std::optional<std::string> environmentVariable(const char* name) {
    const char* value = std::getenv(name);
    if (!value) return std::nullopt;
    return std::string(value);
}

int msLeft(std::chrono::steady_clock::time_point deadline) {
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    return static_cast<int>(std::clamp<long long>(left, 0, 1 << 30));
}

std::string mbedError(int code) {
    char text[200];
    mbedtls_strerror(code, text, sizeof(text));
    return fmt::format("{} (-0x{:04x})", text, -code);
}

// The connection, plain or TLS, closed when it goes out of scope.
class Connection {
   public:
    Connection() {
        mbedtls_net_init(&net_);
        mbedtls_ssl_init(&ssl_);
        mbedtls_ssl_config_init(&conf_);
        mbedtls_x509_crt_init(&roots_);
    }
    ~Connection() {
        if (tls_) mbedtls_ssl_close_notify(&ssl_);
        mbedtls_net_free(&net_);
        mbedtls_ssl_free(&ssl_);
        mbedtls_ssl_config_free(&conf_);
        mbedtls_x509_crt_free(&roots_);
    }
    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;

    // An error message, empty once connected (and the server's certificate checked). Through `proxy`, when given.
    std::string open(const Url& url, const std::optional<Proxy>& proxy, int timeout_ms) {
        timeout_ms_ = static_cast<uint32_t>(timeout_ms);
        // Our own connect, which gives up in time (mbedtls_net_connect waits as long as the system does), and whose
        // socket the engines started meanwhile do not inherit. Mbed TLS keeps sockets as int, on Windows too.
        std::string error;
        const auto fd = net::connectHost(proxy ? proxy->host : url.host, proxy ? proxy->port : url.port,
                                         std::min(timeout_ms, CONNECT_TIMEOUT_MS), error);
        if (!fd) return proxy ? fmt::format("{} (the proxy of {})", error, proxy->variable) : error;
        net_.fd = static_cast<int>(*fd);
        if (proxy) {
            if (auto refused = tunnel(url, *proxy); !refused.empty()) return refused;
        }
        if (!url.tls) return {};
        tls_ = true;

        if (psa_crypto_init() != PSA_SUCCESS) return "cannot start the cryptography";
        // The PEM text with its final NUL, as mbedtls_x509_crt_parse wants it.
        const auto* pem = reinterpret_cast<const unsigned char*>(TLS_ROOTS);
        if (const int rc = mbedtls_x509_crt_parse(&roots_, pem, std::char_traits<char>::length(TLS_ROOTS) + 1)) {
            return "bad root certificates: " + mbedError(rc);
        }
        if (const int rc = mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                                       MBEDTLS_SSL_PRESET_DEFAULT)) {
            return mbedError(rc);
        }
        // The server's certificate must chain to one of the roots and name the host.
        mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conf_, &roots_, nullptr);
        mbedtls_ssl_conf_read_timeout(&conf_, timeout_ms_);
        if (const int rc = mbedtls_ssl_setup(&ssl_, &conf_)) return mbedError(rc);
        if (const int rc = mbedtls_ssl_set_hostname(&ssl_, url.host.c_str())) return mbedError(rc);
        mbedtls_ssl_set_bio(&ssl_, &net_, mbedtls_net_send, nullptr, mbedtls_net_recv_timeout);

        int rc = 0;
        while ((rc = mbedtls_ssl_handshake(&ssl_)) != 0) {
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (const auto flags = mbedtls_ssl_get_verify_result(&ssl_); flags != 0 && flags != uint32_t(-1)) {
                char why[256];
                mbedtls_x509_crt_verify_info(why, sizeof(why), "", flags);
                std::string text(why);
                while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
                return "the certificate of " + url.host + " is not trusted: " + text;
            }
            return "TLS with " + url.host + ": " + mbedError(rc);
        }
        return {};
    }

    bool send(std::string_view data) {
        // A send that times out comes back as WANT_WRITE: tried again until the deadline only.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
        while (!data.empty()) {
            const auto* p = reinterpret_cast<const unsigned char*>(data.data());
            const int n   = tls_ ? mbedtls_ssl_write(&ssl_, p, data.size()) : mbedtls_net_send(&net_, p, data.size());
            if ((n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) &&
                std::chrono::steady_clock::now() < deadline) {
                continue;
            }
            if (n <= 0) return false;
            data.remove_prefix(static_cast<std::size_t>(n));
        }
        return true;
    }

    // Everything the server sends until it closes the connection; false on a timeout or an error.
    bool receiveAll(std::string& out) {
        unsigned char buffer[4096];
        while (true) {
            const int n = tls_ ? mbedtls_ssl_read(&ssl_, buffer, sizeof(buffer))
                               : mbedtls_net_recv_timeout(&net_, buffer, sizeof(buffer), timeout_ms_);
            if (n > 0) {
                out.append(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return true;
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE ||
                n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
                continue;
            }
            // Some servers just close the socket once the answer is sent.
            return n == MBEDTLS_ERR_NET_CONN_RESET && !out.empty();
        }
    }

   private:
    // Asks the proxy for a tunnel to the server, through which TLS then goes as it would directly, the server's name
    // in its handshake. An error message, empty once the tunnel is open.
    std::string tunnel(const Url& url, const Proxy& proxy) {
        const auto target =
            fmt::format("{}:{}", url.host.find(':') != std::string::npos ? "[" + url.host + "]" : url.host, url.port);
        auto request = fmt::format("CONNECT {0} HTTP/1.1\r\nHost: {0}\r\nUser-Agent: fastchess-gui\r\n", target);
        if (!proxy.authorization.empty()) request += "Proxy-Authorization: " + proxy.authorization + "\r\n";
        request += "\r\n";
        if (!send(request)) return "the request could not be sent to the proxy " + proxy.host;

        // Its answer, read up to the end of its headers and no further: what follows is the server's.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
        std::string answer;
        while (answer.size() < 4 || answer.compare(answer.size() - 4, 4, "\r\n\r\n") != 0) {
            const int left = msLeft(deadline);
            if (left <= 0) return "no answer from the proxy " + proxy.host + " in time";
            if (answer.size() > 65536) return "not an HTTP answer from the proxy " + proxy.host;
            unsigned char c = 0;
            const int n     = mbedtls_net_recv_timeout(&net_, &c, 1, static_cast<uint32_t>(left));
            if (n == 1) {
                answer += static_cast<char>(c);
            } else if (n != MBEDTLS_ERR_SSL_TIMEOUT && n != MBEDTLS_ERR_SSL_WANT_READ) {
                return "the proxy " + proxy.host + " closed the connection";
            }
        }
        // "HTTP/1.1 200 Connection established"
        const auto line = answer.substr(0, answer.find("\r\n"));
        const auto code = line.find(' ');
        if (line.rfind("HTTP/", 0) != 0 || code == std::string::npos || line.size() < code + 4) {
            return "not an HTTP answer from the proxy " + proxy.host;
        }
        if (line[code + 1] != '2') {
            return fmt::format("the proxy {} refused the connection to {}: {}", proxy.host, url.host,
                               line.substr(code + 1));
        }
        return {};
    }

    mbedtls_net_context net_;
    mbedtls_ssl_context ssl_;
    mbedtls_ssl_config conf_;
    mbedtls_x509_crt roots_;
    bool tls_            = false;
    uint32_t timeout_ms_ = 20000;
};

}  // namespace

std::string unchunk(std::string_view body) {
    std::string out;
    std::size_t at = 0;
    while (at < body.size()) {
        const auto eol = body.find("\r\n", at);
        if (eol == std::string_view::npos) return std::string(body);
        std::size_t size = 0;
        try {
            size = std::stoul(std::string(body.substr(at, eol - at)), nullptr, 16);
        } catch (...) {
            return std::string(body);
        }
        if (size == 0) return out;
        if (eol + 2 + size > body.size()) return std::string(body);
        out.append(body.substr(eol + 2, size));
        at = eol + 2 + size + 2;  // the chunk, then its CRLF
    }
    return out;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields) {
    const auto encode = [](std::string_view s) {
        std::string out;
        for (const unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out += static_cast<char>(c);
            } else if (c == ' ') {
                out += '+';
            } else {
                out += fmt::format("%{:02X}", c);
            }
        }
        return out;
    };
    std::string out;
    for (const auto& [name, value] : fields) {
        if (!out.empty()) out += '&';
        out += encode(name);
        out += '=';
        out += encode(value);
    }
    return out;
}

bool noProxy(std::string_view list, std::string_view host) {
    const auto bare = [](std::string text) {
        if (text.size() > 1 && text.front() == '[' && text.find(']') != std::string::npos) {
            return text.substr(1, text.find(']') - 1);  // [::1], [::1]:8080
        }
        if (std::count(text.begin(), text.end(), ':') == 1) text.erase(text.find(':'));  // the port of name:port
        while (!text.empty() && text.front() == '.') text.erase(0, 1);                   // .example.com
        while (!text.empty() && text.back() == '.') text.pop_back();                     // a fully qualified name
        return text;
    };
    const auto name = bare(lower(std::string(host)));
    std::size_t at  = 0;
    while (at < list.size()) {
        const auto end   = std::min(list.find_first_of(", \t", at), list.size());
        const auto entry = bare(lower(std::string(list.substr(at, end - at))));
        at               = end + 1;
        if (entry.empty()) continue;
        if (entry == "*" || entry == name) return true;
        if (name.size() > entry.size() && name.compare(name.size() - entry.size(), entry.size(), entry) == 0 &&
            name[name.size() - entry.size() - 1] == '.') {
            return true;
        }
    }
    return false;
}

std::optional<Proxy> proxyFor(std::string_view host, const Environment& environment) {
    // As curl: the lowercase name first, and a variable set empty is not set.
    const auto first = [&](const char* a, const char* b) -> std::optional<std::pair<std::string, const char*>> {
        for (const char* name : {a, b}) {
            if (auto value = environment(name); value && !value->empty()) return std::pair{*value, name};
        }
        return std::nullopt;
    };
    auto setting = first("https_proxy", "HTTPS_PROXY");
    if (!setting) setting = first("all_proxy", "ALL_PROXY");
    if (!setting) return std::nullopt;
    if (const auto passed = first("no_proxy", "NO_PROXY"); passed && noProxy(passed->first, host)) return std::nullopt;
    return parseProxy(setting->first, setting->second);
}

std::optional<Proxy> proxyFor(std::string_view host) { return proxyFor(host, environmentVariable); }

HttpResult httpPost(const HttpRequest& request) {
    HttpResult result;
    const auto url = parseUrl(request.url);
    if (!url) {
        result.error = "not an http or https URL: " + request.url;
        return result;
    }

    // HTTPS only: plain HTTP is for the tests, on this machine.
    const auto proxy = url->tls ? proxyFor(url->host) : std::nullopt;
    if (proxy && !proxy->error.empty()) {
        result.error = proxy->error;
        return result;
    }
    Connection connection;
    if (auto error = connection.open(*url, proxy, request.timeout_s * 1000); !error.empty()) {
        result.error = std::move(error);
        return result;
    }
    std::string head =
        fmt::format("POST {} HTTP/1.1\r\nHost: {}\r\nUser-Agent: fastchess-gui\r\n", url->path, url->host);
    if (!request.token.empty()) head += "Authorization: Bearer " + request.token + "\r\n";
    if (!request.content_type.empty()) head += "Content-Type: " + request.content_type + "\r\n";
    head += fmt::format("Content-Length: {}\r\nConnection: close\r\n\r\n", request.body.size());
    if (!connection.send(head) || !connection.send(request.body)) {
        result.error = "the request could not be sent to " + url->host;
        return result;
    }
    std::string answer;
    if (!connection.receiveAll(answer) && answer.empty()) {
        result.error = "no answer from " + url->host + " in time";
        return result;
    }

    // "HTTP/1.1 200 OK", headers, a blank line, the body.
    const auto headers_end = answer.find("\r\n\r\n");
    const auto space       = answer.find(' ');
    if (answer.rfind("HTTP/", 0) != 0 || headers_end == std::string::npos || space == std::string::npos) {
        result.error = "not an HTTP answer from " + url->host;
        return result;
    }
    try {
        result.status = std::stoi(answer.substr(space + 1, 3));
    } catch (...) {
        result.error = "not an HTTP answer from " + url->host;
        return result;
    }
    const auto headers = lower(answer.substr(0, headers_end));
    result.body        = answer.substr(headers_end + 4);
    if (headers.find("\r\ntransfer-encoding: chunked") != std::string::npos) result.body = unchunk(result.body);
    return result;
}

}  // namespace fcg
