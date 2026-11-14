#pragma once

// The test program's own path: the mock engines are built next to it, and the tests start it again as the WinBoard
// adapter or the book adapter.

#include <filesystem>
#include <string>

#ifdef _WIN32
#    include <windows.h>
#elif defined(__APPLE__)
#    include <mach-o/dyld.h>

#    include <cstdint>
#    include <cstring>
#endif

namespace fcg::test {

inline std::filesystem::path selfPath() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    const auto n = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::string(buffer, n));
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    buffer.resize(std::strlen(buffer.c_str()));
    return std::filesystem::canonical(buffer);
#else
    return std::filesystem::read_symlink("/proc/self/exe");
#endif
}

}  // namespace fcg::test
