#pragma once

// journal.jsonl: what happened in a tournament, one JSON object per line, appended and synced to disk as it
// happens. Never rewritten, so a crash can at worst cut the last line short.

#include <cstdio>
#include <filesystem>
#include <vector>

#include <json.hpp>

namespace fcg::tour {

class Journal {
   public:
    // Reads every complete line. A last line without its newline (a power failure in the middle of a write) is
    // dropped, and with `repair` cut from the file. Throws std::runtime_error, with the line number, on a line that
    // is not JSON.
    [[nodiscard]] static std::vector<nlohmann::json> read(const std::filesystem::path& file, bool repair = true);

    // Opens the file for appending, creating it if needed. Throws when it cannot.
    explicit Journal(const std::filesystem::path& file);
    ~Journal();

    Journal(const Journal&)            = delete;
    Journal& operator=(const Journal&) = delete;

    // Writes one line and syncs it to disk. Throws when the write fails. Not thread safe.
    void append(const nlohmann::json& event);

   private:
    std::FILE* file_ = nullptr;
};

}  // namespace fcg::tour
