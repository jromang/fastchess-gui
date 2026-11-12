#pragma once

// HTTP and HTTPS POST requests, for the lichess broadcasts: Mbed TLS, linked in, trusting only the root
// certificates of tls_roots.cpp, and a client of the little HTTP/1.1 they need. HTTPS goes through the proxy the
// environment names, if any.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fcg {

struct HttpResult {
    int status = 0;  // HTTP status, 0 when there was no answer
    std::string body;
    std::string error;  // why there was no answer
};

struct HttpRequest {
    std::string url;           // http:// or https://
    std::string token;         // sent as "Authorization: Bearer <token>" when not empty
    std::string content_type;  // of the body
    std::string body;
    int timeout_s = 20;
};

[[nodiscard]] HttpResult httpPost(const HttpRequest& request);

// application/x-www-form-urlencoded, from (name, value) pairs.
[[nodiscard]] std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields);

// A body sent with Transfer-Encoding: chunked, put back together; the text as it is when it is not one.
[[nodiscard]] std::string unchunk(std::string_view body);

// The proxy an HTTPS connection to `host` goes through, from the environment as curl reads it: https_proxy or
// HTTPS_PROXY, otherwise all_proxy or ALL_PROXY; none when no_proxy or NO_PROXY names the host. An http:// proxy,
// with a user and a password or not, through which the connection is tunnelled with CONNECT.
struct Proxy {
    std::string host;
    std::string port;
    std::string authorization;  // the value of Proxy-Authorization, empty without a user
    std::string variable;       // the variable it comes from, to name it in messages
    std::string error;          // why it cannot be used (a proxy set is not passed by for all that)
};
using Environment = std::function<std::optional<std::string>(const char* name)>;
[[nodiscard]] std::optional<Proxy> proxyFor(std::string_view host, const Environment& environment);
// From the environment of the program.
[[nodiscard]] std::optional<Proxy> proxyFor(std::string_view host);

// True when `list`, as NO_PROXY has it ("*", or names and addresses separated by commas), names `host`: a name
// also covers the names under it.
[[nodiscard]] bool noProxy(std::string_view list, std::string_view host);

}  // namespace fcg
