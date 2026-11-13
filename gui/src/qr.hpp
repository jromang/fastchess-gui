#pragma once

// QR codes, for a phone to open the web page's public address: Nayuki's QR Code generator.

#include <string>
#include <vector>

namespace fcg {

struct QrCode {
    int size = 0;            // modules per side, 0 when the text does not fit in one
    std::vector<bool> dark;  // row by row

    [[nodiscard]] bool at(int x, int y) const { return dark[static_cast<std::size_t>(y * size + x)]; }
};

// The smallest code of `text` with medium error correction (a better one when it fits in the same size).
[[nodiscard]] QrCode makeQr(const std::string& text);

}  // namespace fcg
