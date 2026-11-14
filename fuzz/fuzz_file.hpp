#pragma once

// For the readers that take a file name: the input written to a file of this process's own, the same one each time.

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace fcg::fuzz {

inline std::filesystem::path inputFile(const uint8_t* data, std::size_t size, const std::string& extension) {
    static const auto file =
        std::filesystem::temp_directory_path() / ("fcg-fuzz-" + std::to_string(getpid()) + extension);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return file;
}

}  // namespace fcg::fuzz
