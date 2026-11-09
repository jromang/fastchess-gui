#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>

namespace fcg::tour {

// A byte range inside a PGN file.
struct PgnSpan {
    uint64_t offset = 0;
    uint32_t length = 0;

    [[nodiscard]] uint64_t end() const noexcept { return offset + length; }
};

// Append-only PGN file shared by the tournament threads. Remembers nothing itself:
// append() returns where the game went, and the caller keeps that.
class PgnFile {
   public:
    // With `keep`, the file is first cut to that many bytes: games written after the last one the journal knows
    // about are removed.
    explicit PgnFile(std::filesystem::path path, std::optional<uint64_t> keep = std::nullopt);

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    // Thread safe.
    PgnSpan append(const std::string& pgn);

   private:
    std::filesystem::path path_;
    std::ofstream out_;
    uint64_t size_ = 0;
    std::mutex mutex_;
};

// Reads one game back.
[[nodiscard]] std::optional<std::string> readSpan(const std::filesystem::path& file, const PgnSpan& span);

}  // namespace fcg::tour
