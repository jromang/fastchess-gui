#pragma once

// Files that survive a crash or a power loss: data synced to the disk before anything refers to it, whole files
// replaced by a rename, and the lock that keeps a tournament folder to one program at a time.

#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fcg::tour {

// fopen, with the UTF-16 path on Windows. nullptr when it fails.
[[nodiscard]] std::FILE* openFile(const std::filesystem::path& file, const char* mode);

// Flushes `file` and asks the system to put its data on the disk. False when that fails.
[[nodiscard]] bool syncFile(std::FILE* file);

// Replaces `file` with `text`: written to a temporary file next to it, synced, then renamed over it, and the
// rename synced. After a crash the file is the old one or the new one, never a mix or empty. The new file keeps
// the permissions of the old one (POSIX). With `backup`, the old content is first kept as <file>.bak the same way.
// Throws std::runtime_error with the reason.
void replaceFile(const std::filesystem::path& file, std::string_view text, bool backup = false);

// Another program holds the lock of a tournament folder.
class FolderBusy : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

// Held while a tournament folder is open for writing, by one program at a time (tournament.lock). The system
// releases it when the program ends, even by a crash or a power loss. The locks of one folder in one program share
// the system's: the window replaces the tournament it shows by the same one, resumed.
class FolderLock {
   public:
    // Throws FolderBusy when another program holds it, std::runtime_error when the lock file cannot be made.
    explicit FolderLock(const std::filesystem::path& dir);
    ~FolderLock();

    FolderLock(const FolderLock&)            = delete;
    FolderLock& operator=(const FolderLock&) = delete;

   private:
    class Held;  // the system's lock
    std::shared_ptr<Held> held_;
};

}  // namespace fcg::tour
