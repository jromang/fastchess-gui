#pragma once

// HTTPS requests through the system's curl (every Linux distribution, Windows 10 and later): its certificates and
// its proxy settings, and no TLS library to ship. The token goes in a curl config file readable by its owner only,
// removed after the request, never on a command line.

#include <filesystem>
#include <string>
#include <vector>

namespace fcg {

struct HttpResult {
    int status = 0;  // HTTP status, 0 when there was no answer
    std::string body;
    std::string error;  // curl's message when there was no answer
};

struct HttpRequest {
    std::string url;
    std::string token;         // sent as "Authorization: Bearer <token>" when not empty
    std::string content_type;  // of the body
    std::string body;
    int timeout_s = 20;
};

// POSTs `request`. `tmp_dir` holds the config and body files while curl runs.
[[nodiscard]] HttpResult httpPost(const HttpRequest& request, const std::filesystem::path& tmp_dir);

// Whether curl can be run here.
[[nodiscard]] bool curlAvailable();

// application/x-www-form-urlencoded, from (name, value) pairs.
[[nodiscard]] std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields);

}  // namespace fcg
