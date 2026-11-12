#pragma once

// journal.jsonl: what happened in a tournament, one JSON object per line, appended and synced to disk as it
// happens. Never rewritten, so a crash can at worst cut the last line short.

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <json.hpp>

namespace fcg::tour {

class Journal {
   public:
    // Reads every complete line. A last line without its newline (a power failure in the middle of a write) is
    // dropped, and with `repair` cut from the file. A line that is not JSON throws std::runtime_error with its
    // number; with `damaged`, it is skipped instead and described there.
    [[nodiscard]] static std::vector<nlohmann::json> read(const std::filesystem::path& file, bool repair = true,
                                                          std::vector<std::string>* damaged = nullptr);

    // Opens the file for appending, creating it if needed. Throws when it cannot. A last line left without its
    // newline gets one, so that the next line is not glued to it.
    explicit Journal(const std::filesystem::path& file);
    ~Journal();

    Journal(const Journal&)            = delete;
    Journal& operator=(const Journal&) = delete;

    // Writes one line and syncs it to disk. Throws when the write fails, and from then on: what was written of
    // that line is not known. Thread safe.
    void append(const nlohmann::json& event);

   private:
    std::FILE* file_ = nullptr;
    bool failed_     = false;
    std::mutex mutex_;
};

}  // namespace fcg::tour
