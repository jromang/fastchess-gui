#include "engine_text.hpp"

#include <algorithm>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <windows.h>
#endif

namespace fcg::tour {

#ifdef _WIN32
namespace {

// The code page engines read, the system's: GetACP() says UTF-8 in this program, because of its manifest. 65001
// when the system itself is set to UTF-8 (the "Beta: Unicode UTF-8" option).
UINT systemCodePage() {
    static const UINT cp = [] {
        wchar_t value[16] = {};
        DWORD size        = sizeof(value);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage", L"ACP",
                         RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS) {
            if (const auto n = static_cast<UINT>(wcstoul(value, nullptr, 10))) return n;
        }
        return UINT{1252};
    }();
    return cp;
}

std::wstring wide(const std::string& utf8) {
    const int n =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

// Empty when a character has no place in the code page.
std::string narrow(const std::wstring& text, UINT cp) {
    BOOL lost   = FALSE;
    const int n = WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                      nullptr, &lost);
    if (n <= 0 || lost) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr,
                        &lost);
    return lost ? std::string() : out;
}

}  // namespace
#endif

std::string forEngine(const std::string& utf8) {
#ifdef _WIN32
    const bool ascii =
        std::all_of(utf8.begin(), utf8.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    const UINT cp = systemCodePage();
    if (ascii || cp == CP_UTF8) return utf8;

    const auto text = wide(utf8);
    if (text.empty()) return utf8;  // not UTF-8: sent as it is
    if (auto out = narrow(text, cp); !out.empty()) return out;

    // A path with a character the code page lacks (Cyrillic on a Western system...): its short name, all ASCII.
    if (const DWORD n = GetShortPathNameW(text.c_str(), nullptr, 0); n > 0) {
        std::wstring brief(n, L'\0');
        const DWORD written = GetShortPathNameW(text.c_str(), brief.data(), n);
        brief.resize(written);
        if (written > 0 && brief != text) {
            if (auto out = narrow(brief, cp); !out.empty()) return out;
        }
    }
#endif
    return utf8;
}

std::vector<std::pair<std::string, std::string>> forEngine(std::vector<std::pair<std::string, std::string>> options) {
    for (auto& [name, value] : options) value = forEngine(value);
    return options;
}

}  // namespace fcg::tour
