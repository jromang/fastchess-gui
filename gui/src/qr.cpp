#include "qr.hpp"

#include <qrcodegen.hpp>

namespace fcg {

QrCode makeQr(const std::string& text) {
    QrCode out;
    try {
        const auto qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        out.size      = qr.getSize();
        out.dark.resize(static_cast<std::size_t>(out.size * out.size));
        for (int y = 0; y < out.size; ++y) {
            for (int x = 0; x < out.size; ++x) out.dark[static_cast<std::size_t>(y * out.size + x)] = qr.getModule(x, y);
        }
    } catch (const qrcodegen::data_too_long&) {
        out = {};
    }
    return out;
}

}  // namespace fcg
