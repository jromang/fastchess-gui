#pragma once

// HTTP and HTTPS POST requests, for the lichess broadcasts: Mbed TLS, linked in, trusting only the root
// certificates of tls_roots.cpp, and a client of the little HTTP/1.1 they need.

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

}  // namespace fcg
