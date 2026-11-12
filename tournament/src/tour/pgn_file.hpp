#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

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
    // about are removed. A file shorter than that (its end lost by a power cut) is filled up with blank lines to
    // it, so that new games do not land on the place of journaled ones; missing() tells how much was lost.
    explicit PgnFile(std::filesystem::path path, std::optional<uint64_t> keep = std::nullopt);
    ~PgnFile();

    PgnFile(const PgnFile&)            = delete;
    PgnFile& operator=(const PgnFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] uint64_t missing() const noexcept { return missing_; }

    // Thread safe. The game is on the disk when it returns. Throws when it cannot be written (disk full...);
    // the file then takes no more games, as the end of it is not known any more.
    PgnSpan append(const std::string& pgn);

   private:
    std::filesystem::path path_;
    std::FILE* file_  = nullptr;
    uint64_t size_    = 0;
    uint64_t missing_ = 0;
    std::string error_;  // why the file takes no more games
    std::mutex mutex_;
};

// Appends `text` to a file other tournaments may write as well, in one write synced to the disk, under a lock of
// the process; the file is not kept open. Throws when it cannot be written.
void appendShared(const std::filesystem::path& file, const std::string& text);

// Reads one game back.
[[nodiscard]] std::optional<std::string> readSpan(const std::filesystem::path& file, const PgnSpan& span);

// A tag value as PGN writes it between the quotes: its quotes and backslashes escaped.
[[nodiscard]] std::string pgnTagValue(std::string_view value);

}  // namespace fcg::tour
