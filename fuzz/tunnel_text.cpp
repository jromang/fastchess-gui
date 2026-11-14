// The tunnel's handling of what the server sends: its text made printable, the public address found in it, the
// lines of libssh2's trace that carry it, and --tunnel's value.

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ssh_tunnel.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    // Nothing that could move a terminal's cursor: no C0 control, no DEL, no C1 control (U+0080 to U+009F, 0xC2 0x80
    // to 0xC2 0x9F in UTF-8).
    const auto printable = fcg::printableText(text);
    for (std::size_t i = 0; i < printable.size(); ++i) {
        const auto c = static_cast<unsigned char>(printable[i]);
        if (c < 0x20 || c == 0x7F) std::abort();
        if (c == 0xC2 && i + 1 < printable.size() && static_cast<unsigned char>(printable[i + 1]) < 0xA0) std::abort();
    }

    // An address the page can show and the QR code can carry: https://, then a host and a port only.
    if (const auto url = fcg::findPublicUrl(text)) {
        if (!url->starts_with("https://") || url->size() <= 8) std::abort();
        for (const char c : url->substr(8)) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != ':') std::abort();
        }
    }

    const auto trace = fcg::readTrace(text);
    if (trace.text.size() > 256 ||
        (trace.error == 0 && !trace.text.empty() && text.find("Failure Event: ") == std::string_view::npos)) {
        std::abort();
    }

    std::string error;
    (void)fcg::parseTunnel(std::string(text), error);
    return 0;
}
