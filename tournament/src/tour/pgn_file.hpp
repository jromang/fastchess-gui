#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    // With `kept`, the places of the games the journal knows about, text outside them is removed first: cut past
    // the last one, blanked between them (a crash between a game and its journal line, a game voided while it was
    // written), so that another PGN viewer shows only the journal's games. A file shorter than the end of the last
    // one (its end lost by a power cut) is filled up with blank lines to it, so that new games do not land on the
    // place of journaled ones; missing() tells how much was lost. Without `between`, text between them stays.
    explicit PgnFile(std::filesystem::path path, std::optional<std::vector<PgnSpan>> kept = std::nullopt,
                     bool between = true);
    ~PgnFile();

    PgnFile(const PgnFile&)            = delete;
    PgnFile& operator=(const PgnFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] uint64_t missing() const noexcept { return missing_; }

    // Thread safe. The game is on the disk when it returns. Throws when it cannot be written (disk full...);
    // the file then takes no more games, as the end of it is not known any more.
    PgnSpan append(const std::string& pgn);

    // Overwrites a game append() wrote with blank lines, in place and synced: its game will never be journaled.
    // The other games keep their places. Thread safe. False when it cannot (the next opening blanks it).
    bool blank(const PgnSpan& span);

   private:
    std::filesystem::path path_;
    std::FILE* file_  = nullptr;
    uint64_t size_    = 0;
    uint64_t missing_ = 0;
    std::string error_;  // why the file takes no more games
    std::mutex mutex_;
};

// Appends `text` to a file other tournaments may write as well, in one write synced to the disk, under a lock of
// the process; the file is not kept open. `before`, when given, is called first with the size of the file, where
// the text goes unless another program writes there meanwhile; when it throws, nothing is written. Throws when the
// file cannot be written.
void appendShared(const std::filesystem::path& file, const std::string& text,
                  const std::function<void(uint64_t at)>& before = {});

// For the tests: when set, appendShared() writes only as many bytes of its text as this returns for its size, and
// fails (a disk full).
inline std::function<std::size_t(std::size_t size)> appendSharedCut;

// Is `text` in `file`, where an appendShared() that a crash may have interrupted was writing it (`at`, the size
// before() was given)? It is when the bytes at `at` are the text, or a little further on (another program wrote
// meanwhile). A start of it left at the end of the file (a power cut in the middle of the write) is completed with
// the rest, and then it is too. Nothing is ever cut: the file may hold other programs' games. Under the lock of
// appendShared(). Throws when the rest cannot be written.
[[nodiscard]] bool settleShared(const std::filesystem::path& file, uint64_t at, const std::string& text);

// Reads one game back.
[[nodiscard]] std::optional<std::string> readSpan(const std::filesystem::path& file, const PgnSpan& span);

// A tag value as PGN writes it between the quotes: its quotes and backslashes escaped.
[[nodiscard]] std::string pgnTagValue(std::string_view value);

}  // namespace fcg::tour
