#include "https.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <fmt/include/fmt/format.h>

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

    // An error message, empty once connected (and the server's certificate checked).
    std::string open(const Url& url, int timeout_ms) {
        timeout_ms_ = static_cast<uint32_t>(timeout_ms);
        if (const int rc = mbedtls_net_connect(&net_, url.host.c_str(), url.port.c_str(), MBEDTLS_NET_PROTO_TCP)) {
            return "cannot connect to " + url.host + ": " + mbedError(rc);
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
        while (!data.empty()) {
            const auto* p = reinterpret_cast<const unsigned char*>(data.data());
            const int n   = tls_ ? mbedtls_ssl_write(&ssl_, p, data.size()) : mbedtls_net_send(&net_, p, data.size());
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
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
    mbedtls_net_context net_;
    mbedtls_ssl_context ssl_;
    mbedtls_ssl_config conf_;
    mbedtls_x509_crt roots_;
    bool tls_            = false;
    uint32_t timeout_ms_ = 20000;
};

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

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

HttpResult httpPost(const HttpRequest& request) {
    HttpResult result;
    const auto url = parseUrl(request.url);
    if (!url) {
        result.error = "not an http or https URL: " + request.url;
        return result;
    }

    Connection connection;
    if (auto error = connection.open(*url, request.timeout_s * 1000); !error.empty()) {
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
